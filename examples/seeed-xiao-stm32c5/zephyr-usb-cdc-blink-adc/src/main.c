/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal USB CDC + blink + ADC bring-up sample for XIAO STM32C5.
 * USB output follows the zephyr-usb-cdc-echo-1m pattern and is not the
 * Zephyr console, so LED blink is independent of USB CDC state.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>

#define LED0_NODE DT_ALIAS(led0)
#define BAT_EN_NODE DT_ALIAS(baten)
#define PWM_A4_NODE DT_ALIAS(pwm_a4)
#define HEATER_NODE DT_ALIAS(imu_heater)
#define IMU_NODE DT_ALIAS(imu0)
#define EXT_FLASH_NODE DT_NODELABEL(ext_flash)

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No ADC io-channels specified"
#endif

#define DT_SPEC_AND_COMMA_FOR_INPUTS(node_id, prop, idx) \
	COND_CODE_1(DT_PHA_HAS_CELL_AT_IDX(node_id, prop, idx, input), \
		    (ADC_DT_SPEC_GET_BY_IDX(node_id, idx),), ())

#define ADC_AVG_SAMPLES 8
#define BATTERY_ADC_CHANNEL 4
#define BATTERY_ADC_RESOLUTION 12
#define BATTERY_ADC_VREF_MV 3300
#define BATTERY_AVG_SAMPLES 16
#define BATTERY_DIVIDER_NUM 2
#define BATTERY_DIVIDER_DEN 1
#define BATTERY_ENABLE_SETTLE_MS 5
#define LED_PERIOD_MS 500
#define PRINT_PERIOD_MS 5000
#define A4_PWM_DUTY_PERCENT 50U
#define IMU_TARGET_TEMPERATURE_MC 40000
#define IMU_CONTROL_INTERVAL_MS 500
#define IMU_DUTY_MIN_PERMILLE 0
#define IMU_DUTY_MAX_PERMILLE 600
#define IMU_DUTY_STEP_PERMILLE 50
#define IMU_FILTER_SHIFT 2
#define IMU_KP_PER_MILLE_PER_C 25
#define IMU_KI_PER_MILLE_PER_C_S 2
#define IMU_INTEGRAL_LIMIT_MC_S 180000
#define FLASH_TEST_OFFSET 0x00100000
#define FLASH_TEST_LEN 256
#define TX_RING_BUF_SIZE 2048
#define PRINT_BUF_SIZE 512
#define LSM6DSL_REG_WHO_AM_I 0x0f
#define LSM6DSL_REG_CTRL1_XL 0x10
#define LSM6DSL_REG_OUT_TEMP_L 0x20
#define LSM6DSL_WHO_AM_I_EXPECTED 0x6a
#define LSM6DSL_CTRL1_XL_ODR_MASK 0xf0
#define LSM6DSL_CTRL1_XL_ODR_12_5HZ 0x10

static const struct device *const cdc_dev =
	DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec bat_en = GPIO_DT_SPEC_GET(BAT_EN_NODE, gpios);
static const struct pwm_dt_spec pwm_a4 = PWM_DT_SPEC_GET(PWM_A4_NODE);
static const struct pwm_dt_spec heater_pwm = PWM_DT_SPEC_GET(HEATER_NODE);
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(IMU_NODE);
static const struct device *const battery_adc_dev =
	DEVICE_DT_GET(DT_NODELABEL(adc1));
static const struct adc_channel_cfg battery_adc_cfg = {
	.gain = ADC_GAIN_1,
	.reference = ADC_REF_INTERNAL,
	.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 289),
	.channel_id = BATTERY_ADC_CHANNEL,
};
static const struct device *const ext_flash = DEVICE_DT_GET(EXT_FLASH_NODE);

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

static uint8_t tx_ring_buffer[TX_RING_BUF_SIZE];
static struct ring_buf tx_ringbuf;
static uint32_t tx_bytes;
static uint32_t rx_bytes;
static uint32_t drop_bytes;
static uint32_t led_toggle_count;
static bool led_ready;
static bool adc_ready;
static bool pwm_ready;
static bool heater_pwm_ready;
static bool battery_ready;
static bool flash_ready;
static uint32_t heater_pwm_duty_permille;
static int heater_pwm_ret;
static int imu_init_ret;
static uint8_t imu_init_ctrl1_xl;
static struct flash_pages_info flash_page;
static uint8_t flash_write_buf[FLASH_TEST_LEN];
static uint8_t flash_read_buf[FLASH_TEST_LEN];
static uint8_t flash_jedec_id[3];
static int flash_jedec_ret;

struct flash_cycle_result {
	uint32_t cycle;
	int erase_ret;
	int write_ret;
	int read_ret;
	int verify_ret;
	size_t mismatch_offset;
	uint8_t expected;
	uint8_t actual;
};

struct imu_temp_result {
	bool bus_ready;
	int ret;
	uint8_t who_am_i;
	uint8_t ctrl1_xl;
	int16_t raw_temperature;
	int32_t temperature_mc;
};

struct imu_pid_state {
	int32_t integral_mc_s;
	int32_t filtered_temperature_mc;
	uint32_t previous_duty_permille;
	bool filter_ready;
};

struct imu_pid_report {
	struct imu_temp_result temperature;
	int32_t filtered_temperature_mc;
	int32_t error_mc;
	int32_t integral_mc_s;
	uint32_t duty_permille;
	uint32_t sample_count;
	int control_ret;
};

static struct k_thread led_thread_data;
static struct imu_pid_state imu_pid;
static struct imu_pid_report imu_pid_status;
K_THREAD_STACK_DEFINE(led_stack, 512);

static uint32_t cdc_tx_put(const uint8_t *buf, uint32_t len)
{
	uint32_t key;
	uint32_t stored;

	key = irq_lock();
	stored = ring_buf_put(&tx_ringbuf, buf, len);
	if (stored < len) {
		drop_bytes += len - stored;
	}
	irq_unlock(key);

	if (stored > 0U) {
		uart_irq_tx_enable(cdc_dev);
	}

	return stored;
}

static void cdc_printf(const char *fmt, ...)
{
	char buf[PRINT_BUF_SIZE];
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (len < 0) {
		return;
	}

	if (len >= (int)sizeof(buf)) {
		len = sizeof(buf) - 1;
	}

	(void)cdc_tx_put((const uint8_t *)buf, (uint32_t)len);
}

static void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t buf[64];
	int len;

	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			len = uart_fifo_read(dev, buf, sizeof(buf));
			if (len > 0) {
				rx_bytes += len;
			}
		}

		if (uart_irq_tx_ready(dev)) {
			len = ring_buf_get(&tx_ringbuf, buf, sizeof(buf));
			if (len == 0) {
				uart_irq_tx_disable(dev);
				continue;
			}

			len = uart_fifo_fill(dev, buf, len);
			if (len > 0) {
				tx_bytes += len;
			}
		}
	}
}

static bool cdc_get_line_state(uint32_t *dtr, uint32_t *baudrate)
{
	if (!device_is_ready(cdc_dev)) {
		return false;
	}

	(void)uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_DTR, dtr);
	(void)uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_BAUD_RATE, baudrate);

	return true;
}

static int read_adc_average(const struct adc_dt_spec *adc, uint16_t *raw_avg,
			    int32_t *mv)
{
	uint16_t raw;
	uint32_t raw_sum = 0;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int ret;

	ret = adc_sequence_init_dt(adc, &sequence);
	if (ret < 0) {
		return ret;
	}

	ret = adc_read(adc->dev, &sequence);
	if (ret < 0) {
		return ret;
	}

	for (int i = 0; i < ADC_AVG_SAMPLES; i++) {
		ret = adc_read(adc->dev, &sequence);
		if (ret < 0) {
			return ret;
		}

		raw_sum += raw;
	}

	*raw_avg = raw_sum / ADC_AVG_SAMPLES;
	*mv = *raw_avg;
	return adc_raw_to_millivolts_dt(adc, mv);
}

static int read_battery_average(uint16_t *raw_avg, int32_t *adc_mv,
				int32_t *battery_mv)
{
	uint16_t raw;
	uint32_t raw_sum = 0;
	struct adc_sequence sequence = {
		.channels = BIT(BATTERY_ADC_CHANNEL),
		.buffer = &raw,
		.buffer_size = sizeof(raw),
		.resolution = BATTERY_ADC_RESOLUTION,
	};
	int ret;

	ret = gpio_pin_set_dt(&bat_en, 1);
	if (ret < 0) {
		return ret;
	}

	k_msleep(BATTERY_ENABLE_SETTLE_MS);

	ret = adc_read(battery_adc_dev, &sequence);
	if (ret < 0) {
		(void)gpio_pin_set_dt(&bat_en, 0);
		return ret;
	}

	for (int i = 0; i < BATTERY_AVG_SAMPLES; i++) {
		ret = adc_read(battery_adc_dev, &sequence);
		if (ret < 0) {
			(void)gpio_pin_set_dt(&bat_en, 0);
			return ret;
		}

		raw_sum += raw;
	}

	(void)gpio_pin_set_dt(&bat_en, 0);

	*raw_avg = raw_sum / BATTERY_AVG_SAMPLES;
	*adc_mv = *raw_avg;
	ret = adc_raw_to_millivolts(BATTERY_ADC_VREF_MV, ADC_GAIN_1,
				    BATTERY_ADC_RESOLUTION, adc_mv);
	if (ret < 0) {
		return ret;
	}

	*battery_mv = (*adc_mv * BATTERY_DIVIDER_NUM) / BATTERY_DIVIDER_DEN;
	return 0;
}

static int init_imu_temperature(void)
{
	uint8_t who_am_i;
	uint8_t ctrl1_xl;
	int ret;

	imu_init_ret = -ENODEV;
	imu_init_ctrl1_xl = 0;

	if (!i2c_is_ready_dt(&imu_i2c)) {
		return imu_init_ret;
	}

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_WHO_AM_I, &who_am_i);
	if (ret < 0) {
		imu_init_ret = ret;
		return ret;
	}

	if (who_am_i != LSM6DSL_WHO_AM_I_EXPECTED) {
		imu_init_ret = -EIO;
		return imu_init_ret;
	}

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL1_XL, &ctrl1_xl);
	if (ret < 0) {
		imu_init_ret = ret;
		return ret;
	}

	if ((ctrl1_xl & LSM6DSL_CTRL1_XL_ODR_MASK) == 0U) {
		ctrl1_xl = (ctrl1_xl & ~LSM6DSL_CTRL1_XL_ODR_MASK) |
			   LSM6DSL_CTRL1_XL_ODR_12_5HZ;
		ret = i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL1_XL,
					    ctrl1_xl);
		if (ret < 0) {
			imu_init_ret = ret;
			return ret;
		}
	}

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL1_XL,
				   &imu_init_ctrl1_xl);
	if (ret < 0) {
		imu_init_ret = ret;
		return ret;
	}

	imu_init_ret = 0;
	return 0;
}

static void read_imu_temperature(struct imu_temp_result *result)
{
	uint8_t temp_l;
	uint8_t temp_h;
	int ret;

	memset(result, 0, sizeof(*result));
	result->ret = -ENODEV;

	result->bus_ready = i2c_is_ready_dt(&imu_i2c);
	if (!result->bus_ready) {
		return;
	}

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_WHO_AM_I,
				   &result->who_am_i);
	if (ret < 0) {
		result->ret = ret;
		return;
	}

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL1_XL,
				   &result->ctrl1_xl);
	if (ret < 0) {
		result->ret = ret;
		return;
	}

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_OUT_TEMP_L, &temp_l);
	if (ret < 0) {
		result->ret = ret;
		return;
	}

	ret = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_OUT_TEMP_L + 1,
				   &temp_h);
	if (ret < 0) {
		result->ret = ret;
		return;
	}

	result->raw_temperature =
		(int16_t)((uint16_t)temp_l | ((uint16_t)temp_h << 8));
	result->temperature_mc =
		25000 + (((int32_t)result->raw_temperature * 1000) / 256);
	result->ret = 0;
}

static bool init_led(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		return false;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	return ret == 0;
}

static bool init_a4_pwm(void)
{
	uint32_t pulse_ns = (pwm_a4.period * A4_PWM_DUTY_PERCENT) / 100U;
	int ret;

	if (!pwm_is_ready_dt(&pwm_a4)) {
		return false;
	}

	ret = pwm_set_dt(&pwm_a4, pwm_a4.period, pulse_ns);
	return ret == 0;
}

static int set_heater_pwm_duty(uint32_t duty_permille)
{
	uint32_t pulse_ns = (heater_pwm.period * duty_permille) / 1000U;
	int ret;

	if (!heater_pwm_ready) {
		heater_pwm_ret = -ENODEV;
		return -ENODEV;
	}

	ret = pwm_set_dt(&heater_pwm, heater_pwm.period, pulse_ns);
	heater_pwm_duty_permille = duty_permille;
	heater_pwm_ret = ret;
	return ret;
}

static bool init_heater_pwm(void)
{
	if (!pwm_is_ready_dt(&heater_pwm)) {
		heater_pwm_ret = -ENODEV;
		return false;
	}

	heater_pwm_ready = true;
	return set_heater_pwm_duty(IMU_DUTY_MIN_PERMILLE) == 0;
}

static uint32_t apply_imu_duty_slew_limit(struct imu_pid_state *pid,
					  uint32_t duty_permille)
{
	uint32_t previous = pid->previous_duty_permille;

	if (duty_permille > previous + IMU_DUTY_STEP_PERMILLE) {
		duty_permille = previous + IMU_DUTY_STEP_PERMILLE;
	} else if (previous > duty_permille + IMU_DUTY_STEP_PERMILLE) {
		duty_permille = previous - IMU_DUTY_STEP_PERMILLE;
	}

	pid->previous_duty_permille = duty_permille;
	return duty_permille;
}

static uint32_t update_imu_pi(struct imu_pid_state *pid, int32_t temperature_mc,
			      int32_t *filtered_temperature_mc,
			      int32_t *error_mc)
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
			IMU_FILTER_SHIFT;
	}

	*filtered_temperature_mc = pid->filtered_temperature_mc;
	*error_mc = IMU_TARGET_TEMPERATURE_MC - pid->filtered_temperature_mc;

	pid->integral_mc_s += (*error_mc * IMU_CONTROL_INTERVAL_MS) / 1000;
	pid->integral_mc_s = CLAMP(pid->integral_mc_s,
				   -IMU_INTEGRAL_LIMIT_MC_S,
				   IMU_INTEGRAL_LIMIT_MC_S);

	p_term = (IMU_KP_PER_MILLE_PER_C * *error_mc) / 1000;
	i_term = (IMU_KI_PER_MILLE_PER_C_S * pid->integral_mc_s) / 1000;
	output_permille = CLAMP(p_term + i_term, IMU_DUTY_MIN_PERMILLE,
				IMU_DUTY_MAX_PERMILLE);

	return apply_imu_duty_slew_limit(pid, (uint32_t)output_permille);
}

static void run_imu_pid_step(void)
{
	struct imu_temp_result temperature;
	uint32_t duty_permille;

	read_imu_temperature(&temperature);
	imu_pid_status.temperature = temperature;

	if (temperature.ret < 0 ||
	    temperature.who_am_i != LSM6DSL_WHO_AM_I_EXPECTED) {
		imu_pid.previous_duty_permille = 0;
		imu_pid_status.control_ret = temperature.ret < 0 ?
					     temperature.ret : -EIO;
		imu_pid_status.duty_permille = 0;
		(void)set_heater_pwm_duty(0);
		return;
	}

	duty_permille = update_imu_pi(&imu_pid, temperature.temperature_mc,
				      &imu_pid_status.filtered_temperature_mc,
				      &imu_pid_status.error_mc);

	imu_pid_status.control_ret = set_heater_pwm_duty(duty_permille);
	if (imu_pid_status.control_ret < 0) {
		duty_permille = 0;
	}

	imu_pid_status.integral_mc_s = imu_pid.integral_mc_s;
	imu_pid_status.duty_permille = duty_permille;
	imu_pid_status.sample_count++;
}

static bool init_adc(void)
{
	BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ARRAY_SIZE(adc_labels),
		     "ADC channel count must match A0-A3 labels");

	for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
		int ret;

		if (!device_is_ready(adc_channels[i].dev)) {
			return false;
		}

		ret = adc_channel_setup_dt(&adc_channels[i]);
		if (ret < 0) {
			return false;
		}
	}

	return true;
}

static bool init_battery(void)
{
	int ret;

	if (!device_is_ready(battery_adc_dev)) {
		return false;
	}

	if (!gpio_is_ready_dt(&bat_en)) {
		return false;
	}

	ret = gpio_pin_configure_dt(&bat_en, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return false;
	}

	ret = adc_channel_setup(battery_adc_dev, &battery_adc_cfg);
	return ret == 0;
}

static bool init_flash(void)
{
	int ret;

	if (!device_is_ready(ext_flash)) {
		return false;
	}

	flash_jedec_ret = flash_read_jedec_id(ext_flash, flash_jedec_id);

	ret = flash_get_page_info_by_offs(ext_flash, FLASH_TEST_OFFSET,
					  &flash_page);
	return ret == 0;
}

static void run_flash_cycle(uint32_t cycle, struct flash_cycle_result *result)
{
	memset(result, 0, sizeof(*result));
	result->cycle = cycle;

	for (size_t i = 0; i < sizeof(flash_write_buf); i++) {
		flash_write_buf[i] = (uint8_t)(0xa5 ^ i ^ cycle);
		flash_read_buf[i] = 0;
	}

	result->erase_ret = flash_erase(ext_flash, flash_page.start_offset,
					flash_page.size);
	if (result->erase_ret != 0) {
		return;
	}

	result->write_ret = flash_write(ext_flash, FLASH_TEST_OFFSET,
					flash_write_buf,
					sizeof(flash_write_buf));
	if (result->write_ret != 0) {
		return;
	}

	result->read_ret = flash_read(ext_flash, FLASH_TEST_OFFSET,
				      flash_read_buf, sizeof(flash_read_buf));
	if (result->read_ret != 0) {
		return;
	}

	result->verify_ret = memcmp(flash_write_buf, flash_read_buf,
				    sizeof(flash_write_buf));
	if (result->verify_ret == 0) {
		return;
	}

	for (size_t i = 0; i < sizeof(flash_write_buf); i++) {
		if (flash_write_buf[i] != flash_read_buf[i]) {
			result->mismatch_offset = i;
			result->expected = flash_write_buf[i];
			result->actual = flash_read_buf[i];
			return;
		}
	}
}

static bool init_cdc(void)
{
	if (!device_is_ready(cdc_dev)) {
		return false;
	}

	ring_buf_init(&tx_ringbuf, sizeof(tx_ring_buffer), tx_ring_buffer);
	(void)uart_line_ctrl_set(cdc_dev, UART_LINE_CTRL_DCD, 1);
	(void)uart_line_ctrl_set(cdc_dev, UART_LINE_CTRL_DSR, 1);

	uart_irq_callback_user_data_set(cdc_dev, serial_cb, NULL);
	uart_irq_rx_enable(cdc_dev);

	return true;
}

static void led_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		if (led_ready) {
			(void)gpio_pin_toggle_dt(&led);
			led_toggle_count++;
		}

		k_msleep(LED_PERIOD_MS);
	}
}

static void print_status(uint32_t loop_count, uint32_t dtr, uint32_t baudrate,
			 const struct flash_cycle_result *flash_result)
{
	cdc_printf("\r\n");
	cdc_printf("========== XIAO STM32C5 BRING-UP | tick %u ==========\r\n",
		   loop_count);

	cdc_printf("[USB]\r\n");
	cdc_printf("  CDC ACM     : ready, dtr=%u, baud=%u\r\n", dtr, baudrate);
	cdc_printf("  Counters    : tx=%u bytes, rx=%u bytes, drop=%u bytes\r\n",
		   tx_bytes, rx_bytes, drop_bytes);

	cdc_printf("[LED]\r\n");
	cdc_printf("  User LED    : %s, blink=%u ms, toggles=%u\r\n",
		   led_ready ? "ready" : "not-ready", LED_PERIOD_MS,
		   led_toggle_count);

	cdc_printf("[ADC A0-A3]\r\n");

	if (adc_ready) {
		for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
			uint16_t raw;
			int32_t mv;
			int ret = read_adc_average(&adc_channels[i], &raw, &mv);

			if (ret < 0) {
				cdc_printf("  %-8s : read failed (%d)\r\n",
					   adc_labels[i], ret);
			} else {
				cdc_printf("  %-8s : raw=%4u, voltage=%4d mV\r\n",
					   adc_labels[i], raw, mv);
			}
		}
	} else {
		cdc_printf("  Status      : not-ready\r\n");
	}

	cdc_printf("[PWM]\r\n");
	if (pwm_ready) {
		cdc_printf("  A4/D4/PB7   : TIM4_CH2, period=%u ns, duty=%u%%\r\n",
			   pwm_a4.period, A4_PWM_DUTY_PERCENT);
	} else {
		cdc_printf("  A4/D4/PB7   : not-ready\r\n");
	}

	cdc_printf("[HEATER PWM]\r\n");
	if (heater_pwm_ready) {
		cdc_printf("  PA8/TIM1_CH1: period=%u ns, duty=%u.%u%%, set_ret=%d\r\n",
			   heater_pwm.period, heater_pwm_duty_permille / 10,
			   heater_pwm_duty_permille % 10,
			   heater_pwm_ret);
		cdc_printf("  Control     : PI temperature compensation, interval=%u ms\r\n",
			   IMU_CONTROL_INTERVAL_MS);
	} else {
		cdc_printf("  PA8/TIM1_CH1: not-ready, set_ret=%d\r\n",
			   heater_pwm_ret);
	}

	cdc_printf("[IMU TEMP]\r\n");
	{
		struct imu_temp_result *imu_temp = &imu_pid_status.temperature;

		if (imu_temp->ret < 0) {
			cdc_printf("  I2C         : %s, read failed (%d)\r\n",
				   imu_temp->bus_ready ? "ready" : "not-ready",
				   imu_temp->ret);
		} else {
			int32_t temp_abs = imu_temp->temperature_mc < 0 ?
					   -imu_temp->temperature_mc :
					   imu_temp->temperature_mc;
			int32_t filtered_abs =
				imu_pid_status.filtered_temperature_mc < 0 ?
				-imu_pid_status.filtered_temperature_mc :
				imu_pid_status.filtered_temperature_mc;
			int32_t error_abs = imu_pid_status.error_mc < 0 ?
					    -imu_pid_status.error_mc :
					    imu_pid_status.error_mc;

			cdc_printf("  I2C         : ready, polled register read\r\n");
			cdc_printf("  Init        : ret=%d, CTRL1_XL=0x%02x\r\n",
				   imu_init_ret, imu_init_ctrl1_xl);
			cdc_printf("  Registers   : WHO_AM_I=0x%02x (expect 0x%02x), CTRL1_XL=0x%02x\r\n",
				   imu_temp->who_am_i,
				   LSM6DSL_WHO_AM_I_EXPECTED,
				   imu_temp->ctrl1_xl);
			cdc_printf("  Temperature : raw=%d LSB, %s%d.%03d C\r\n",
				   imu_temp->raw_temperature,
				   imu_temp->temperature_mc < 0 ? "-" : "",
				   temp_abs / 1000, temp_abs % 1000);
			cdc_printf("  Filtered    : %s%d.%03d C, target=%d.%03d C\r\n",
				   imu_pid_status.filtered_temperature_mc < 0 ?
				   "-" : "", filtered_abs / 1000,
				   filtered_abs % 1000,
				   IMU_TARGET_TEMPERATURE_MC / 1000,
				   IMU_TARGET_TEMPERATURE_MC % 1000);
			cdc_printf("  Error       : %s%d.%03d C, samples=%u\r\n",
				   imu_pid_status.error_mc < 0 ? "-" : "",
				   error_abs / 1000, error_abs % 1000,
				   imu_pid_status.sample_count);
			cdc_printf("  PI          : kp=%u, ki=%u, integral=%d mC*s, control_ret=%d\r\n",
				   IMU_KP_PER_MILLE_PER_C,
				   IMU_KI_PER_MILLE_PER_C_S,
				   imu_pid_status.integral_mc_s,
				   imu_pid_status.control_ret);
		}
	}

	cdc_printf("[BAT]\r\n");
	if (battery_ready) {
		uint16_t raw;
		int32_t adc_mv;
		int32_t battery_mv;
		int ret = read_battery_average(&raw, &adc_mv, &battery_mv);

		if (ret < 0) {
			cdc_printf("  Battery     : read failed (%d)\r\n", ret);
		} else {
			cdc_printf("  Sense path  : BAT_EN/PE2 + BAT_Reading/PA4 ADC1_IN4\r\n");
			cdc_printf("  Reading     : raw=%4u, adc=%4d mV, battery=%4d mV (%d.%03d V)\r\n",
				   raw, adc_mv, battery_mv, battery_mv / 1000,
				   battery_mv % 1000);
		}
	} else {
		cdc_printf("  Battery     : not-ready\r\n");
	}

	cdc_printf("[FLASH]\r\n");
	if (flash_ready && flash_result != NULL) {
		cdc_printf("  Device      : %s\r\n", ext_flash->name);
		if (flash_jedec_ret == 0) {
			cdc_printf("  JEDEC ID    : %02x %02x %02x\r\n",
				   flash_jedec_id[0], flash_jedec_id[1],
				   flash_jedec_id[2]);
		} else {
			cdc_printf("  JEDEC ID    : read failed (%d)\r\n",
				   flash_jedec_ret);
		}
		cdc_printf("  Region      : offset=0x%08x, len=%u, erase_page=0x%08lx/%zu\r\n",
			   FLASH_TEST_OFFSET, FLASH_TEST_LEN,
			   (unsigned long)flash_page.start_offset,
			   flash_page.size);
		cdc_printf("  Cycle       : %u\r\n", flash_result->cycle);

		if (flash_result->erase_ret != 0) {
			cdc_printf("  Result      : erase failed (%d)\r\n",
				   flash_result->erase_ret);
		} else if (flash_result->write_ret != 0) {
			cdc_printf("  Result      : erase=OK, write failed (%d)\r\n",
				   flash_result->write_ret);
		} else if (flash_result->read_ret != 0) {
			cdc_printf("  Result      : erase=OK, write=OK, read failed (%d)\r\n",
				   flash_result->read_ret);
		} else if (flash_result->verify_ret != 0) {
			cdc_printf("  Result      : erase=OK, write=OK, read=OK, verify failed at +0x%zx (0x%02x != 0x%02x)\r\n",
				   flash_result->mismatch_offset,
				   flash_result->expected, flash_result->actual);
		} else {
			cdc_printf("  Result      : erase=OK, write=OK, read=OK, verify=OK\r\n");
		}
	} else {
		cdc_printf("  External    : not-ready\r\n");
	}

	cdc_printf("======================================================\r\n");
}

int main(void)
{
	uint32_t report_count = 0;
	uint32_t control_tick = 0;
	uint32_t flash_cycle = 0;
	bool cdc_ready;

	led_ready = init_led();
	(void)k_thread_create(&led_thread_data, led_stack,
			      K_THREAD_STACK_SIZEOF(led_stack),
			      led_thread, NULL, NULL, NULL,
			      7, 0, K_NO_WAIT);

	adc_ready = init_adc();
	pwm_ready = init_a4_pwm();
	heater_pwm_ready = init_heater_pwm();
	battery_ready = init_battery();
	flash_ready = init_flash();
	cdc_ready = init_cdc();
	(void)init_imu_temperature();

	while (true) {
		run_imu_pid_step();

		if ((control_tick %
		     (PRINT_PERIOD_MS / IMU_CONTROL_INTERVAL_MS)) == 0U) {
			uint32_t dtr = 0U;
			uint32_t baudrate = 0U;
			struct flash_cycle_result flash_result;
			const struct flash_cycle_result *flash_report = NULL;

			if (flash_ready) {
				run_flash_cycle(flash_cycle, &flash_result);
				flash_report = &flash_result;
				flash_cycle++;
			}

			if (cdc_ready && cdc_get_line_state(&dtr, &baudrate)) {
				print_status(report_count, dtr, baudrate,
					     flash_report);
			}

			report_count++;
		}

		control_tick++;
		k_msleep(IMU_CONTROL_INTERVAL_MS);
	}

	return 0;
}
