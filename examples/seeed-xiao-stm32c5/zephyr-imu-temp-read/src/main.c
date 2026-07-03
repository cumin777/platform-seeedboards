/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 IMU temperature readback.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define IMU_NODE DT_ALIAS(imu0)
#define SAMPLE_INTERVAL_MS 500
#define LSM6DSL_REG_WHO_AM_I 0x0f
#define LSM6DSL_REG_CTRL1_XL 0x10
#define LSM6DSL_REG_OUT_TEMP_L 0x20

BUILD_ASSERT(DT_NODE_HAS_STATUS(IMU_NODE, okay), "imu0 alias is not defined or not okay");

static const struct device *const imu = DEVICE_DT_GET(IMU_NODE);
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(IMU_NODE);

static int32_t abs_i32(int32_t value)
{
	return value < 0 ? -value : value;
}

static void print_temperature(int32_t temperature_mc)
{
	int32_t abs_temperature = abs_i32(temperature_mc);

	printk("IMU temperature: %s%d.%03d C\n",
	       temperature_mc < 0 ? "-" : "",
	       abs_temperature / 1000,
	       abs_temperature % 1000);
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

int main(void)
{
	printk("XIAO STM32C5 IMU temperature readback\n");
	printk("IMU device: %s\n", imu->name);

	if (!device_is_ready(imu)) {
		printk("IMU device is not ready\n");
		return 0;
	}

	if (!i2c_is_ready_dt(&imu_i2c)) {
		printk("IMU I2C bus is not ready\n");
		return 0;
	}

	print_imu_debug_registers();

	while (true) {
		int32_t temperature_mc;
		int16_t raw_temperature;
		int ret = read_imu_temperature_mc(&temperature_mc);

		if (ret < 0) {
			printk("Failed to read IMU temperature: %d\n", ret);
		} else {
			print_temperature(temperature_mc);
		}

		ret = read_raw_temperature(&raw_temperature);
		if (ret < 0) {
			printk("Failed to read raw IMU temperature: %d\n", ret);
		} else {
			printk("IMU raw temperature: %d (registers 0x20/0x21)\n",
			       raw_temperature);
		}

		k_msleep(SAMPLE_INTERVAL_MS);
	}
}
