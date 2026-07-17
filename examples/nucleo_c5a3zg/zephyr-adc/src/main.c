/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC demo: reads Nucleo C5A3ZG PA0-PA3 (ADC1_IN0-ADC1_IN3).
 * Adapted from Zephyr samples/drivers/adc/adc_dt.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA_FOR_INPUTS(node_id, prop, idx) \
	COND_CODE_1(DT_PHA_HAS_CELL_AT_IDX(node_id, prop, idx, input), \
		    (ADC_DT_SPEC_GET_BY_IDX(node_id, idx),), ())

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA_FOR_INPUTS)
};

static const char *const adc_labels[] = {
	"PA0",
	"PA1",
	"PA2",
	"PA3",
};

#define ADC_AVG_SAMPLES 8
#define ADC_CHANNEL_COUNT ARRAY_SIZE(adc_channels)
#define HEARTBEAT_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec heartbeat_led =
	GPIO_DT_SPEC_GET(HEARTBEAT_NODE, gpios);
static bool heartbeat_ready;

static void heartbeat_init(void)
{
	if (!gpio_is_ready_dt(&heartbeat_led)) {
		printk("Heartbeat LED not ready\n");
		return;
	}

	int err = gpio_pin_configure_dt(&heartbeat_led, GPIO_OUTPUT_INACTIVE);

	if (err) {
		printk("Heartbeat LED setup failed: %d\n", err);
		return;
	}

	heartbeat_ready = true;
}

static void heartbeat_toggle(void)
{
	if (heartbeat_ready) {
		(void)gpio_pin_toggle_dt(&heartbeat_led);
	}
}

static void halt_with_fast_blink(void)
{
	while (1) {
		heartbeat_toggle();
		k_msleep(200);
	}
}

static int read_adc_one(const struct adc_dt_spec *adc, uint16_t *raw_avg,
			int32_t *mv)
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

	/* Drop the first conversion after selecting this ADC mux input. */
	err = adc_read(adc->dev, &sequence);
	if (err) {
		return err;
	}

	for (int i = 0; i < ADC_AVG_SAMPLES; i++) {
		err = adc_read(adc->dev, &sequence);
		if (err) {
			return err;
		}

		raw_sum += raw;
	}

	*raw_avg = raw_sum / ADC_AVG_SAMPLES;
	*mv = *raw_avg;
	err = adc_raw_to_millivolts_dt(adc, mv);
	if (err) {
		return err;
	}

	return 0;
}

int main(void)
{
	BUILD_ASSERT(ADC_CHANNEL_COUNT == ARRAY_SIZE(adc_labels),
		     "ADC channel count must match PA0-PA3 labels");

	heartbeat_init();

	for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_channels[i].dev)) {
			printk("ADC controller %s not ready\n",
			       adc_channels[i].dev->name);
			halt_with_fast_blink();
		}

		if (adc_channels[i].dev != adc_channels[0].dev) {
			printk("ADC channels must use the same controller\n");
			halt_with_fast_blink();
		}

		if (adc_channels[i].channel_id != i) {
			printk("ADC channel order mismatch: %s uses ch%d\n",
			       adc_labels[i], adc_channels[i].channel_id);
			halt_with_fast_blink();
		}

		if ((adc_channels[i].resolution != adc_channels[0].resolution) ||
		    (adc_channels[i].oversampling != adc_channels[0].oversampling)) {
			printk("ADC channels must use the same resolution and oversampling\n");
			halt_with_fast_blink();
		}

		int err = adc_channel_setup_dt(&adc_channels[i]);
		if (err) {
			printk("%s channel %d setup failed: %d\n",
			       adc_labels[i], adc_channels[i].channel_id, err);
			halt_with_fast_blink();
		}
	}

	printk("ADC demo: Nucleo PA0-PA3, ADC1, %d-sample avg, LED heartbeat\n",
	       ADC_AVG_SAMPLES);

	while (1) {
		uint16_t raw[ADC_CHANNEL_COUNT];
		int32_t mv[ADC_CHANNEL_COUNT];

		for (size_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
			int err = read_adc_one(&adc_channels[i], &raw[i], &mv[i]);

			if (err) {
				printk("%s=ERR%d  ", adc_labels[i], err);
				continue;
			}

			printk("%s=%4d/%4dmV  ", adc_labels[i], raw[i], mv[i]);
		}
		printk("\n");
		heartbeat_toggle();
		k_msleep(1000);
	}

	return 0;
}
