/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Full-feature firmware for XIAO STM32C5.
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define CONSOLE_NODE DT_CHOSEN(zephyr_console)
#define LED0_NODE DT_ALIAS(led0)
#define BAT_EN_NODE DT_ALIAS(baten)
#define PWM_A4_NODE DT_ALIAS(pwm_a4)
#define IMU_NODE DT_ALIAS(imu0)
#define HEATER_NODE DT_ALIAS(imu_heater)
#define EXT_FLASH_NODE DT_NODELABEL(ext_flash)

#define ADC_AVG_SAMPLES 8
#define XIAO_ADC_COUNT 4
#define BATTERY_ADC_CHANNEL 4
#define BATTERY_ADC_RESOLUTION 12
#define BATTERY_ADC_VREF_MV 3300
#define BATTERY_AVG_SAMPLES 16
#define BATTERY_DIVIDER_NUM 2
#define BATTERY_DIVIDER_DEN 1
#define BATTERY_ENABLE_SETTLE_MS 5

#define LED_PERIOD_MS 500
#define ADC_LOG_INTERVAL_MS 1000

#define A4_PWM_DUTY_PERCENT 50U

#define LSM6DSL_REG_WHO_AM_I 0x0f
#define LSM6DSL_REG_CTRL1_XL 0x10
#define LSM6DSL_REG_OUT_TEMP_L 0x20
#define LSM6DSL_EXPECTED_WHO_AM_I 0x6a

#define TARGET_TEMPERATURE_MC 40000
#define CONTROL_INTERVAL_MS 500
#define PID_LOG_INTERVAL_MS 1000
#define IMU_DATA_LOG_INTERVAL_MS 1000
#define IRQ_LOG_INTERVAL_MS 1000
#define IMU_LOOP_WAIT_MS 50

#define DUTY_MIN_PERMILLE 0
#define DUTY_MAX_PERMILLE 600
#define DUTY_STEP_PERMILLE 50
#define FILTER_SHIFT 2

#define KP_PER_MILLE_PER_C 25
#define KI_PER_MILLE_PER_C_S 2
#define INTEGRAL_LIMIT_MC_S 180000

#define IMU_ODR_HZ 104
#define IMU_PRINT_DECIMALS 6

#define FLASH_TEST_OFFSET 0x00100000
#define FLASH_TEST_LEN 256
#define FLASH_INTERVAL_MS 2000

BUILD_ASSERT(DT_NODE_HAS_COMPAT(CONSOLE_NODE, zephyr_cdc_acm_uart),
	     "Console must be the USB CDC ACM UART");

#define DT_SPEC_AND_COMMA_FOR_INPUTS(node_id, prop, idx) \
	COND_CODE_1(DT_PHA_HAS_CELL_AT_IDX(node_id, prop, idx, input), \
		    (ADC_DT_SPEC_GET_BY_IDX(node_id, idx),), ())

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA_FOR_INPUTS)
};

static const char *const adc_labels[] = {
	"A0/D0",
	"A1/D1",
	"A2/D2",
	"A3/D3",
};

static const struct device *const console_dev = DEVICE_DT_GET(CONSOLE_NODE);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec bat_en = GPIO_DT_SPEC_GET(BAT_EN_NODE, gpios);
static const struct device *const battery_adc_dev =
	DEVICE_DT_GET(DT_NODELABEL(adc1));
static const struct adc_channel_cfg battery_adc_cfg = {
	.gain = ADC_GAIN_1,
	.reference = ADC_REF_INTERNAL,
	.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 289),
	.channel_id = BATTERY_ADC_CHANNEL,
};
static const struct pwm_dt_spec pwm_a4 = PWM_DT_SPEC_GET(PWM_A4_NODE);
static const struct device *const imu = DEVICE_DT_GET(IMU_NODE);
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(IMU_NODE);
static const struct pwm_dt_spec heater = PWM_DT_SPEC_GET(HEATER_NODE);
static const struct device *const ext_flash = DEVICE_DT_GET(EXT_FLASH_NODE);

static K_MUTEX_DEFINE(print_mutex);
static K_SEM_DEFINE(imu_drdy_sem, 0, 1);
static atomic_t imu_irq_count;

static struct k_thread led_thread_data;
static struct k_thread adc_thread_data;
static struct k_thread imu_thread_data;
static struct k_thread flash_thread_data;

K_THREAD_STACK_DEFINE(led_stack, 512);
K_THREAD_STACK_DEFINE(adc_stack, 1536);
K_THREAD_STACK_DEFINE(imu_stack, 3072);
K_THREAD_STACK_DEFINE(flash_stack, 1536);

struct pid_state {
	int32_t integral_mc_s;
	int32_t filtered_temperature_mc;
	uint32_t previous_duty_permille;
	bool filter_ready;
};

static uint8_t flash_write_buf[FLASH_TEST_LEN];
static uint8_t flash_read_buf[FLASH_TEST_LEN];
static struct flash_pages_info flash_page;

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
	return (int32_t)(now_ms - deadline_ms) >= 0;
}

static int32_t abs_i32(int32_t value)
{
	return value < 0 ? -value : value;
}

static void print_lock(void)
{
	k_mutex_lock(&print_mutex, K_FOREVER);
}

static void print_unlock(void)
{
	k_mutex_unlock(&print_mutex);
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

static void print_sensor_value(const struct sensor_value *value)
{
	int32_t micro_value = (value->val1 * 1000000) + value->val2;
	int32_t abs_value = abs_i32(micro_value);

	printk("%s%d.%0*d",
	       micro_value < 0 ? "-" : "",
	       abs_value / 1000000,
	       IMU_PRINT_DECIMALS,
	       abs_value % 1000000);
}

static void wait_for_usb_console(void)
{
	uint32_t dtr = 0;

	if (!device_is_ready(console_dev)) {
		return;
	}

	(void)uart_line_ctrl_set(console_dev, UART_LINE_CTRL_DCD, 1);
	(void)uart_line_ctrl_set(console_dev, UART_LINE_CTRL_DSR, 1);

	while (!dtr) {
		(void)uart_line_ctrl_get(console_dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}
}

static int read_adc_average(const struct adc_dt_spec *adc, int samples,
			    uint16_t *raw_avg, int32_t *mv)
{
	uint16_t raw;
	uint32_t raw_sum = 0;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int err = adc_sequence_init_dt(adc, &sequence);

	if (err) {
		return err;
	}

	err = adc_read(adc->dev, &sequence);
	if (err) {
		return err;
	}

	for (int i = 0; i < samples; i++) {
		err = adc_read(adc->dev, &sequence);
		if (err) {
			return err;
		}

		raw_sum += raw;
	}

	*raw_avg = raw_sum / samples;
	*mv = *raw_avg;
	err = adc_raw_to_millivolts_dt(adc, mv);
	if (err) {
		return err;
	}

	return 0;
}

static int read_battery_adc_average(uint16_t *raw_avg, int32_t *mv)
{
	uint16_t raw;
	uint32_t raw_sum = 0;
	struct adc_sequence sequence = {
		.channels = BIT(BATTERY_ADC_CHANNEL),
		.buffer = &raw,
		.buffer_size = sizeof(raw),
		.resolution = BATTERY_ADC_RESOLUTION,
	};
	int err;

	err = adc_read(battery_adc_dev, &sequence);
	if (err) {
		return err;
	}

	for (int i = 0; i < BATTERY_AVG_SAMPLES; i++) {
		err = adc_read(battery_adc_dev, &sequence);
		if (err) {
			return err;
		}

		raw_sum += raw;
	}

	*raw_avg = raw_sum / BATTERY_AVG_SAMPLES;
	*mv = *raw_avg;
	return adc_raw_to_millivolts(BATTERY_ADC_VREF_MV, ADC_GAIN_1,
				     BATTERY_ADC_RESOLUTION, mv);
}

static bool init_led(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		printk("[INIT] user LED GPIO not ready\n");
		return false;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("[INIT] user LED configure failed: %d\n", ret);
		return false;
	}

	printk("[INIT] user LED blink ready\n");
	return true;
}

static bool init_a4_pwm(void)
{
	uint32_t pulse_ns = (pwm_a4.period * A4_PWM_DUTY_PERCENT) / 100U;
	int ret;

	if (!pwm_is_ready_dt(&pwm_a4)) {
		printk("[INIT] A4/D4 PWM device not ready\n");
		return false;
	}

	ret = pwm_set_dt(&pwm_a4, pwm_a4.period, pulse_ns);
	if (ret < 0) {
		printk("[INIT] A4/D4 PWM set failed: %d\n", ret);
		return false;
	}

	printk("[INIT] A4/D4/PB7 PWM ready: period=%u ns duty=%u%%\n",
	       pwm_a4.period, A4_PWM_DUTY_PERCENT);
	return true;
}

static bool init_adc_inputs(void)
{
	BUILD_ASSERT(XIAO_ADC_COUNT == ARRAY_SIZE(adc_labels),
		     "XIAO ADC channel count must match labels");
	BUILD_ASSERT(ARRAY_SIZE(adc_channels) == XIAO_ADC_COUNT,
		     "ADC list must include A0-A3");

	for (size_t i = 0; i < XIAO_ADC_COUNT; i++) {
		int ret;

		if (!device_is_ready(adc_channels[i].dev)) {
			printk("[INIT] ADC controller %s not ready\n",
			       adc_channels[i].dev->name);
			return false;
		}

		ret = adc_channel_setup_dt(&adc_channels[i]);
		if (ret < 0) {
			printk("[INIT] %s ADC setup failed: %d\n",
			       adc_labels[i], ret);
			return false;
		}
	}

	printk("[INIT] A0-A3 ADC ready\n");
	return true;
}

static bool init_battery(void)
{
	int ret;

	if (!device_is_ready(battery_adc_dev)) {
		printk("[INIT] battery ADC controller %s not ready\n",
		       battery_adc_dev->name);
		return false;
	}

	if (!gpio_is_ready_dt(&bat_en)) {
		printk("[INIT] BAT_EN GPIO not ready\n");
		return false;
	}

	ret = gpio_pin_configure_dt(&bat_en, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("[INIT] BAT_EN configure failed: %d\n", ret);
		return false;
	}

	ret = adc_channel_setup(battery_adc_dev, &battery_adc_cfg);
	if (ret < 0) {
		printk("[INIT] battery ADC setup failed: %d\n", ret);
		return false;
	}

	printk("[INIT] battery voltage ready: BAT_EN=PE2 BAT_ADC=PA4/ADC1_IN4\n");
	return true;
}

static int read_who_am_i(uint8_t *who_am_i)
{
	return i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_WHO_AM_I, who_am_i);
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

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_OUT_TEMP_L + 1,
				   &temp_h);
	if (ret < 0) {
		return ret;
	}

	*raw_temperature = (int16_t)((uint16_t)temp_l |
				     ((uint16_t)temp_h << 8));
	return 0;
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

static int set_heater_duty(uint32_t duty_permille)
{
	uint32_t pulse = (heater.period * duty_permille) / 1000U;

	return pwm_set_dt(&heater, heater.period, pulse);
}

static uint32_t apply_duty_slew_limit(struct pid_state *pid,
				      uint32_t duty_permille)
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
			(temperature_mc - pid->filtered_temperature_mc) >>
			FILTER_SHIFT;
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
	output_permille = CLAMP(output_permille,
				DUTY_MIN_PERMILLE,
				DUTY_MAX_PERMILLE);

	return apply_duty_slew_limit(pid, output_permille);
}

static int configure_imu(void)
{
	struct sensor_value odr = {
		.val1 = IMU_ODR_HZ,
		.val2 = 0,
	};
	struct sensor_value accel_fs;
	struct sensor_value gyro_fs;
	int ret;

	sensor_g_to_ms2(4, &accel_fs);
	sensor_degrees_to_rad(500, &gyro_fs);

	ret = sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
			      SENSOR_ATTR_FULL_SCALE, &accel_fs);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ,
			      SENSOR_ATTR_FULL_SCALE, &gyro_fs);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	return ret;
}

static void imu_trigger_handler(const struct device *dev,
				const struct sensor_trigger *trigger)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(trigger);

	atomic_inc(&imu_irq_count);
	k_sem_give(&imu_drdy_sem);
}

static int configure_imu_interrupt(void)
{
	struct sensor_trigger trigger = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = SENSOR_CHAN_ACCEL_XYZ,
	};

	return sensor_trigger_set(imu, &trigger, imu_trigger_handler);
}

static bool init_imu(void)
{
	uint8_t who_am_i = 0;
	uint8_t ctrl1_xl = 0;
	int ret;

	if (!device_is_ready(imu)) {
		printk("[INIT] IMU device %s not ready\n", imu->name);
		return false;
	}

	if (!i2c_is_ready_dt(&imu_i2c)) {
		printk("[INIT] IMU I2C bus %s not ready\n", imu_i2c.bus->name);
		return false;
	}

	if (!pwm_is_ready_dt(&heater)) {
		printk("[INIT] IMU heater PWM not ready\n");
		return false;
	}

	ret = read_who_am_i(&who_am_i);
	if (ret < 0) {
		printk("[INIT] IMU WHO_AM_I read failed: %d\n", ret);
		return false;
	}

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL1_XL, &ctrl1_xl);
	if (ret < 0) {
		printk("[INIT] IMU CTRL1_XL read failed: %d\n", ret);
		return false;
	}

	ret = set_heater_duty(0);
	if (ret < 0) {
		printk("[INIT] IMU heater off failed: %d\n", ret);
		return false;
	}

	ret = configure_imu();
	if (ret < 0) {
		printk("[INIT] IMU configure failed: %d\n", ret);
		return false;
	}

	ret = configure_imu_interrupt();
	if (ret < 0) {
		printk("[INIT] IMU interrupt setup failed: %d; polling fallback\n",
		       ret);
	}

	printk("[INIT] IMU ready: WHO_AM_I=0x%02x%s CTRL1_XL=0x%02x\n",
	       who_am_i,
	       who_am_i == LSM6DSL_EXPECTED_WHO_AM_I ? " OK" : " unexpected",
	       ctrl1_xl);
	return true;
}

static bool init_flash(void)
{
	uint8_t jedec_id[3] = { 0 };
	int ret;

	if (!device_is_ready(ext_flash)) {
		printk("[INIT] external flash %s not ready\n", ext_flash->name);
		return false;
	}

	ret = flash_read_jedec_id(ext_flash, jedec_id);
	if (ret == 0) {
		printk("[INIT] external flash JEDEC ID: %02x %02x %02x\n",
		       jedec_id[0], jedec_id[1], jedec_id[2]);
	} else {
		printk("[INIT] external flash JEDEC ID read failed: %d\n", ret);
	}

	ret = flash_get_page_info_by_offs(ext_flash, FLASH_TEST_OFFSET,
					  &flash_page);
	if (ret != 0) {
		printk("[INIT] flash page info failed at 0x%x: %d\n",
		       FLASH_TEST_OFFSET, ret);
		return false;
	}

	printk("[INIT] external flash ready: test_offset=0x%x erase_page=%zu\n",
	       FLASH_TEST_OFFSET, flash_page.size);
	return true;
}

static void led_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		(void)gpio_pin_toggle_dt(&led);
		k_sleep(K_MSEC(LED_PERIOD_MS));
	}
}

static void adc_thread(void *p1, void *p2, void *p3)
{
	bool adc_ready = (bool)(uintptr_t)p1;
	bool battery_ready = (bool)(uintptr_t)p2;

	ARG_UNUSED(p3);

	while (true) {
		print_lock();

		if (adc_ready) {
			printk("[ADC] ");
			for (size_t i = 0; i < XIAO_ADC_COUNT; i++) {
				uint16_t raw;
				int32_t mv;
				int ret = read_adc_average(&adc_channels[i],
							   ADC_AVG_SAMPLES,
							   &raw, &mv);

				if (ret < 0) {
					printk("%s=err%d  ", adc_labels[i], ret);
				} else {
					printk("%s=%4u/%4d mV  ",
					       adc_labels[i], raw, mv);
				}
			}
			printk("\n");
		}

		if (battery_ready) {
			uint16_t raw;
			int32_t adc_mv;
			int32_t battery_mv;
			int ret;

			(void)gpio_pin_set_dt(&bat_en, 1);
			k_sleep(K_MSEC(BATTERY_ENABLE_SETTLE_MS));

			ret = read_battery_adc_average(&raw, &adc_mv);
			if (ret < 0) {
				printk("[BAT] read failed: %d\n", ret);
			} else {
				battery_mv = (adc_mv * BATTERY_DIVIDER_NUM) /
					     BATTERY_DIVIDER_DEN;
				printk("[BAT] raw=%u adc=%d mV battery=%d mV (%d.%03d V)\n",
				       raw, adc_mv, battery_mv,
				       battery_mv / 1000, battery_mv % 1000);
			}
		}

		print_unlock();
		k_sleep(K_MSEC(ADC_LOG_INTERVAL_MS));
	}
}

static int run_pid_control_step(struct pid_state *pid, bool print_log)
{
	int32_t temperature_mc;
	int32_t filtered_temperature_mc;
	int32_t error_mc;
	int16_t raw_temperature;
	uint32_t duty_permille;
	int ret;

	ret = read_imu_temperature_mc(&temperature_mc);
	if (ret < 0) {
		(void)set_heater_duty(0);
		if (print_log) {
			print_lock();
			printk("[PID] temperature read failed: %d; heater off\n",
			       ret);
			print_unlock();
		}
		return ret;
	}

	ret = read_raw_temperature(&raw_temperature);
	if (ret < 0) {
		raw_temperature = 0;
	}

	duty_permille = pi_update(pid, temperature_mc,
				  &filtered_temperature_mc, &error_mc);

	ret = set_heater_duty(duty_permille);
	if (ret < 0) {
		print_lock();
		printk("[PID] heater PWM set failed: %d\n", ret);
		print_unlock();
		return ret;
	}

	if (print_log) {
		print_lock();
		printk("[PID] ");
		print_mc("temp=", temperature_mc);
		printk(" ");
		print_mc("filtered=", filtered_temperature_mc);
		printk(" ");
		print_mc("target=", TARGET_TEMPERATURE_MC);
		printk(" ");
		print_mc("error=", error_mc);
		printk(" raw=%d duty=%u.%u%%\n",
		       raw_temperature, duty_permille / 10, duty_permille % 10);
		print_unlock();
	}

	return 0;
}

static int read_and_print_imu_sample(int32_t irq_count)
{
	struct sensor_value accel[3];
	struct sensor_value gyro[3];
	int ret;

	ret = sensor_sample_fetch(imu);
	if (ret < 0) {
		print_lock();
		printk("[IMU] sample fetch failed: %d\n", ret);
		print_unlock();
		return ret;
	}

	ret = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel);
	if (ret < 0) {
		print_lock();
		printk("[IMU] accel read failed: %d\n", ret);
		print_unlock();
		return ret;
	}

	ret = sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, gyro);
	if (ret < 0) {
		print_lock();
		printk("[IMU] gyro read failed: %d\n", ret);
		print_unlock();
		return ret;
	}

	print_lock();
	printk("[IMU] irq=%d accel(m/s^2) x=", irq_count);
	print_sensor_value(&accel[0]);
	printk(" y=");
	print_sensor_value(&accel[1]);
	printk(" z=");
	print_sensor_value(&accel[2]);
	printk(" gyro(rad/s) x=");
	print_sensor_value(&gyro[0]);
	printk(" y=");
	print_sensor_value(&gyro[1]);
	printk(" z=");
	print_sensor_value(&gyro[2]);
	printk("\n");
	print_unlock();

	return 0;
}

static void imu_thread(void *p1, void *p2, void *p3)
{
	struct pid_state pid = { 0 };
	uint32_t now_ms = k_uptime_get_32();
	uint32_t next_control_ms = now_ms;
	uint32_t next_pid_log_ms = now_ms;
	uint32_t next_imu_log_ms = now_ms;
	uint32_t next_irq_log_ms = now_ms + IRQ_LOG_INTERVAL_MS;
	uint32_t last_irq_seen_ms = now_ms;
	int32_t last_irq_log_count = 0;
	bool sample_pending = true;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		int ret = k_sem_take(&imu_drdy_sem, K_MSEC(IMU_LOOP_WAIT_MS));

		now_ms = k_uptime_get_32();

		if (ret == 0) {
			sample_pending = true;
			last_irq_seen_ms = now_ms;
		}

		if (time_reached(now_ms, next_control_ms)) {
			bool print_pid = time_reached(now_ms, next_pid_log_ms);

			(void)run_pid_control_step(&pid, print_pid);
			next_control_ms = now_ms + CONTROL_INTERVAL_MS;
			if (print_pid) {
				next_pid_log_ms = now_ms + PID_LOG_INTERVAL_MS;
			}
		}

		if (time_reached(now_ms, next_irq_log_ms)) {
			int32_t irq_count = atomic_get(&imu_irq_count);
			int32_t irq_delta = irq_count - last_irq_log_count;
			uint32_t irq_age_ms = now_ms - last_irq_seen_ms;

			print_lock();
			printk("[IRQ] data-ready total=%d delta=%d/%dms last=%u ms ago\n",
			       irq_count, irq_delta, IRQ_LOG_INTERVAL_MS,
			       irq_age_ms);
			print_unlock();

			last_irq_log_count = irq_count;
			next_irq_log_ms = now_ms + IRQ_LOG_INTERVAL_MS;

			if (irq_delta == 0) {
				sample_pending = true;
			}
		}

		if (sample_pending && time_reached(now_ms, next_imu_log_ms)) {
			(void)read_and_print_imu_sample(atomic_get(&imu_irq_count));
			sample_pending = false;
			next_imu_log_ms = now_ms + IMU_DATA_LOG_INTERVAL_MS;
		}
	}
}

static void flash_thread(void *p1, void *p2, void *p3)
{
	uint32_t cycle = 0;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		int ret;

		for (size_t i = 0; i < sizeof(flash_write_buf); i++) {
			flash_write_buf[i] = (uint8_t)(0xa5 ^ i ^ cycle);
			flash_read_buf[i] = 0;
		}

		ret = flash_erase(ext_flash, flash_page.start_offset,
				  flash_page.size);
		if (ret != 0) {
			print_lock();
			printk("[FLASH] cycle=%u erase failed: %d\n", cycle, ret);
			print_unlock();
			k_sleep(K_MSEC(FLASH_INTERVAL_MS));
			continue;
		}

		ret = flash_write(ext_flash, FLASH_TEST_OFFSET,
				  flash_write_buf, sizeof(flash_write_buf));
		if (ret != 0) {
			print_lock();
			printk("[FLASH] cycle=%u write failed: %d\n", cycle, ret);
			print_unlock();
			k_sleep(K_MSEC(FLASH_INTERVAL_MS));
			continue;
		}

		ret = flash_read(ext_flash, FLASH_TEST_OFFSET,
				 flash_read_buf, sizeof(flash_read_buf));
		if (ret != 0) {
			print_lock();
			printk("[FLASH] cycle=%u read failed: %d\n", cycle, ret);
			print_unlock();
			k_sleep(K_MSEC(FLASH_INTERVAL_MS));
			continue;
		}

		ret = memcmp(flash_write_buf, flash_read_buf,
			     sizeof(flash_write_buf));

		print_lock();
		if (ret == 0) {
			printk("[FLASH] cycle=%u verify OK offset=0x%x len=%u\n",
			       cycle, FLASH_TEST_OFFSET, FLASH_TEST_LEN);
		} else {
			for (size_t i = 0; i < sizeof(flash_write_buf); i++) {
				if (flash_write_buf[i] != flash_read_buf[i]) {
					printk("[FLASH] cycle=%u verify failed at +0x%zx: wrote 0x%02x read 0x%02x\n",
					       cycle, i, flash_write_buf[i],
					       flash_read_buf[i]);
					break;
				}
			}
		}
		print_unlock();

		cycle++;
		k_sleep(K_MSEC(FLASH_INTERVAL_MS));
	}
}

int main(void)
{
	bool led_ready;
	bool adc_ready;
	bool battery_ready;
	bool imu_ready;
	bool flash_ready;

	wait_for_usb_console();

	printk("\n");
	printk("====================================================\n");
	printk("  XIAO STM32C5 Full-Feature Firmware\n");
	printk("====================================================\n");
	printk("Console: USB CDC ACM, open host port at 1000000 baud\n");
	printk("ADC: A0-A3 once per second\n");
	printk("PWM: A4/D4/PB7 TIM4_CH2 1 kHz 50%% duty\n");
	printk("Battery: BAT_EN/PE2 + BAT_Reading/PA4 continuously printed\n");
	printk("IMU: LSM6DS3TR-C temp PI + six-axis + PID data\n");
	printk("Flash: external NOR continuous erase/write/read/verify\n");
	printk("\n");

	led_ready = init_led();
	(void)init_a4_pwm();
	adc_ready = init_adc_inputs();
	battery_ready = init_battery();
	imu_ready = init_imu();
	flash_ready = init_flash();

	if (led_ready) {
		(void)k_thread_create(&led_thread_data, led_stack,
				      K_THREAD_STACK_SIZEOF(led_stack),
				      led_thread, NULL, NULL, NULL,
				      7, 0, K_NO_WAIT);
	}

	if (adc_ready || battery_ready) {
		(void)k_thread_create(&adc_thread_data, adc_stack,
				      K_THREAD_STACK_SIZEOF(adc_stack),
				      adc_thread,
				      (void *)(uintptr_t)adc_ready,
				      (void *)(uintptr_t)battery_ready,
				      NULL, 6, 0, K_NO_WAIT);
	}

	if (imu_ready) {
		(void)k_thread_create(&imu_thread_data, imu_stack,
				      K_THREAD_STACK_SIZEOF(imu_stack),
				      imu_thread, NULL, NULL, NULL,
				      5, 0, K_NO_WAIT);
	}

	if (flash_ready) {
		(void)k_thread_create(&flash_thread_data, flash_stack,
				      K_THREAD_STACK_SIZEOF(flash_stack),
				      flash_thread, NULL, NULL, NULL,
				      7, 0, K_NO_WAIT);
	}

	printk("[INIT] runtime threads started\n");

	while (true) {
		k_sleep(K_SECONDS(60));
	}

	return 0;
}
