/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 IMU heater PID control.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define IMU_NODE DT_ALIAS(imu0)
#define HEATER_NODE DT_ALIAS(imu_heater)
#define LSM6DSL_REG_WHO_AM_I 0x0f
#define LSM6DSL_REG_CTRL1_XL 0x10
#define LSM6DSL_REG_OUT_TEMP_L 0x20

#define TARGET_TEMPERATURE_MC 40000
#define CONTROL_INTERVAL_MS 500
#define DUTY_MIN_PERMILLE 0
#define DUTY_MAX_PERMILLE 600
#define DUTY_STEP_PERMILLE 50
#define FILTER_SHIFT 2

/*
 * Output is PWM duty in permille. Error is milli-degree C.
 * The heater and IMU temperature path is slow and noisy, so use a gentle PI loop.
 */
#define KP_PER_MILLE_PER_C 25
#define KI_PER_MILLE_PER_C_S 2
#define INTEGRAL_LIMIT_MC_S 180000

BUILD_ASSERT(DT_NODE_HAS_STATUS(IMU_NODE, okay), "imu0 alias is not defined or not okay");
BUILD_ASSERT(DT_NODE_HAS_STATUS(HEATER_NODE, okay), "imu-heater alias is not defined or not okay");

static const struct device *const imu = DEVICE_DT_GET(IMU_NODE);
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(IMU_NODE);
static const struct pwm_dt_spec heater = PWM_DT_SPEC_GET(HEATER_NODE);

struct pid_state {
	int32_t integral_mc_s;
	int32_t filtered_temperature_mc;
	uint32_t previous_duty_permille;
	bool filter_ready;
};

static int32_t abs_i32(int32_t value)
{
	return value < 0 ? -value : value;
}

static void print_mc(const char *label, int32_t value_mc)
{
	int32_t abs_value = abs_i32(value_mc);

	printk("%s%s%d.%03d C",
	       label,
	       value_mc < 0 ? "-" : "",
	       abs_value / 1000,
	       abs_value % 1000);
}

static int read_imu_temperature_mc(int32_t *temperature_mc)
{
	struct sensor_value temperature;
	int ret;

	ret = sensor_sample_fetch_chan(imu, SENSOR_CHAN_DIE_TEMP);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_channel_get(imu, SENSOR_CHAN_DIE_TEMP, &temperature);
	if (ret < 0) {
		return ret;
	}

	*temperature_mc = (temperature.val1 * 1000) + (temperature.val2 / 1000);
	return 0;
}

static int read_raw_temperature(int16_t *raw_temperature)
{
	uint8_t temp_l;
	uint8_t temp_h;
	int ret;

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_OUT_TEMP_L, &temp_l);
	if (ret < 0) {
		return ret;
	}

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_OUT_TEMP_L + 1, &temp_h);
	if (ret < 0) {
		return ret;
	}

	*raw_temperature = (int16_t)((uint16_t)temp_l | ((uint16_t)temp_h << 8));
	return 0;
}

static void print_imu_debug_registers(void)
{
	uint8_t who_am_i;
	uint8_t ctrl1_xl;
	int ret;

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_WHO_AM_I, &who_am_i);
	if (ret < 0) {
		printk("Failed to read WHO_AM_I: %d\n", ret);
		return;
	}

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL1_XL, &ctrl1_xl);
	if (ret < 0) {
		printk("Failed to read CTRL1_XL: %d\n", ret);
		return;
	}

	printk("IMU WHO_AM_I=0x%02x CTRL1_XL=0x%02x\n", who_am_i, ctrl1_xl);
}

static int set_heater_duty(uint32_t duty_permille)
{
	uint32_t pulse = (heater.period * duty_permille) / 1000U;

	return pwm_set_dt(&heater, heater.period, pulse);
}

static uint32_t apply_duty_slew_limit(struct pid_state *pid, uint32_t duty_permille)
{
	uint32_t previous = pid->previous_duty_permille;

	if (duty_permille > previous + DUTY_STEP_PERMILLE) {
		duty_permille = previous + DUTY_STEP_PERMILLE;
	} else if (previous > duty_permille + DUTY_STEP_PERMILLE) {
		duty_permille = previous - DUTY_STEP_PERMILLE;
	}

	pid->previous_duty_permille = duty_permille;
	return duty_permille;
}

static uint32_t pi_update(struct pid_state *pid, int32_t temperature_mc,
			  int32_t *filtered_temperature_mc, int32_t *error_mc)
{
	int32_t p_term;
	int32_t i_term;
	int32_t output_permille;

	if (!pid->filter_ready) {
		pid->filtered_temperature_mc = temperature_mc;
		pid->filter_ready = true;
	} else {
		pid->filtered_temperature_mc +=
			(temperature_mc - pid->filtered_temperature_mc) >> FILTER_SHIFT;
	}

	*filtered_temperature_mc = pid->filtered_temperature_mc;
	*error_mc = TARGET_TEMPERATURE_MC - pid->filtered_temperature_mc;

	pid->integral_mc_s += (*error_mc * CONTROL_INTERVAL_MS) / 1000;
	pid->integral_mc_s = CLAMP(pid->integral_mc_s,
				   -INTEGRAL_LIMIT_MC_S,
				   INTEGRAL_LIMIT_MC_S);

	p_term = (KP_PER_MILLE_PER_C * *error_mc) / 1000;
	i_term = (KI_PER_MILLE_PER_C_S * pid->integral_mc_s) / 1000;

	output_permille = p_term + i_term;
	output_permille = CLAMP(output_permille, DUTY_MIN_PERMILLE, DUTY_MAX_PERMILLE);

	return apply_duty_slew_limit(pid, output_permille);
}

int main(void)
{
	struct pid_state pid = { 0 };

	printk("XIAO STM32C5 IMU temperature PID control\n");
	printk("Target: %d.%03d C, heater PWM period: %u ns\n",
	       TARGET_TEMPERATURE_MC / 1000,
	       TARGET_TEMPERATURE_MC % 1000,
	       heater.period);

	if (!device_is_ready(imu)) {
		printk("IMU device is not ready\n");
		return 0;
	}

	if (!i2c_is_ready_dt(&imu_i2c)) {
		printk("IMU I2C bus is not ready\n");
		return 0;
	}

	if (!pwm_is_ready_dt(&heater)) {
		printk("Heater PWM device is not ready\n");
		return 0;
	}

	print_imu_debug_registers();

	int ret = set_heater_duty(0);

	if (ret < 0) {
		printk("Failed to disable heater PWM: %d\n", ret);
		return 0;
	}

	while (true) {
		int32_t temperature_mc;
		int32_t filtered_temperature_mc;
		int32_t error_mc;
		int16_t raw_temperature;
		uint32_t duty_permille;

		ret = read_imu_temperature_mc(&temperature_mc);
		if (ret < 0) {
			(void)set_heater_duty(0);
			printk("Failed to read IMU temperature: %d; heater off\n", ret);
			k_msleep(CONTROL_INTERVAL_MS);
			continue;
		}

		ret = read_raw_temperature(&raw_temperature);
		if (ret < 0) {
			raw_temperature = 0;
			printk("Failed to read raw IMU temperature: %d\n", ret);
		}

		duty_permille = pi_update(&pid, temperature_mc,
					  &filtered_temperature_mc, &error_mc);

		ret = set_heater_duty(duty_permille);
		if (ret < 0) {
			printk("Failed to set heater PWM: %d\n", ret);
			return 0;
		}

		print_mc("temp=", temperature_mc);
		printk(" ");
		print_mc("filtered=", filtered_temperature_mc);
		printk(" ");
		print_mc("target=", TARGET_TEMPERATURE_MC);
		printk(" ");
		print_mc("error=", error_mc);
		printk(" raw=%d duty=%u.%u%%\n",
		       raw_temperature, duty_permille / 10, duty_permille % 10);

		k_msleep(CONTROL_INTERVAL_MS);
	}
}
