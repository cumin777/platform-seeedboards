/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC demo: reads ADC1 channels 0-3 (PA0-PA3, XIAO D0-D3).
 * Adapted from Zephyr samples/drivers/adc/adc_dt.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
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

int main(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_channels[i].dev)) {
			printk("ADC controller %s not ready\n",
			       adc_channels[i].dev->name);
			return 0;
		}
	}

	uint16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};

	printk("ADC demo: reading %d channels every 1s\n", (int)ARRAY_SIZE(adc_channels));

	while (1) {
		for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
			int err = adc_sequence_init_dt(&adc_channels[i], &sequence);
			if (err) {
				printk("ADC sequence init failed: %d\n", err);
				continue;
			}

			err = adc_read(adc_channels[i].dev, &sequence);
			if (err) {
				printk("ADC read err ch%d: %d\n", (int)i, err);
			} else {
				int32_t val_mv = buf;
				adc_raw_to_millivolts_dt(&adc_channels[i], &val_mv);
				printk("ch%d raw=%4d  %4dmV  ", (int)i, buf, val_mv);
			}
		}
		printk("\n");
		k_msleep(1000);
	}

	return 0;
}
