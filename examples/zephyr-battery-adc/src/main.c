/*
 * Battery ADC sample for XIAO nRF54L15.
 *
 * Reads battery voltage via ADC channel 7 (AIN7) with a voltage divider,
 * so the raw reading is multiplied by 2 for the actual voltage.
 * Prints the result on UART20 every second.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <inttypes.h>
#include <stdint.h>

LOG_MODULE_REGISTER(battery_adc, CONFIG_LOG_DEFAULT_LEVEL);

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA)
};

static const struct device *const vbat_reg =
	DEVICE_DT_GET(DT_NODELABEL(vbat_pwr));

/* Battery ADC is on channel 7 (AIN7) */
#define VBAT_ADC_CHANNEL 7

/* Voltage divider ratio: actual voltage = ADC reading * 2 */
#define VOLTAGE_DIVIDER_RATIO 2

#define READ_INTERVAL_MS 1000

int main(void)
{
	int err;
	uint16_t buf;
	int32_t val_mv;

	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};

	if (!device_is_ready(vbat_reg)) {
		LOG_ERR("VBAT regulator not ready");
		return 0;
	}

	if (!adc_is_ready_dt(&adc_channels[VBAT_ADC_CHANNEL])) {
		LOG_ERR("ADC controller not ready");
		return 0;
	}

	err = adc_channel_setup_dt(&adc_channels[VBAT_ADC_CHANNEL]);
	if (err < 0) {
		LOG_ERR("Could not setup ADC channel #%d (%d)",
			VBAT_ADC_CHANNEL, err);
		return 0;
	}

	LOG_INF("Battery ADC sample started");

	while (1) {
		/* Enable the battery measurement circuit */
		regulator_enable(vbat_reg);
		k_sleep(K_MSEC(100));

		/* Read ADC */
		(void)adc_sequence_init_dt(&adc_channels[VBAT_ADC_CHANNEL],
					   &sequence);
		err = adc_read_dt(&adc_channels[VBAT_ADC_CHANNEL], &sequence);
		if (err < 0) {
			LOG_ERR("ADC read failed (%d)", err);
			regulator_disable(vbat_reg);
			k_sleep(K_MSEC(READ_INTERVAL_MS));
			continue;
		}

		/* Convert raw value to millivolts */
		if (adc_channels[VBAT_ADC_CHANNEL].channel_cfg.differential) {
			val_mv = (int32_t)((int16_t)buf);
		} else {
			val_mv = (int32_t)buf;
		}

		err = adc_raw_to_millivolts_dt(&adc_channels[VBAT_ADC_CHANNEL],
					       &val_mv);
		if (err < 0) {
			LOG_WRN("Conversion to mV not supported");
		} else {
			/* Compensate for voltage divider */
			int32_t bat_mv = val_mv * VOLTAGE_DIVIDER_RATIO;

			printf("Battery voltage: %" PRId32 " mV (%.3f V)\r\n",
			       bat_mv, bat_mv / 1000.0);
		}

		/* Disable to save power between readings */
		regulator_disable(vbat_reg);

		k_sleep(K_MSEC(READ_INTERVAL_MS));
	}

	return 0;
}
