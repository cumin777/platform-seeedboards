/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO demo: toggle D0 (PA0) as output and read D1 (PA1) as input.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define OUTPUT_NODE DT_ALIAS(gpioout)
#define INPUT_NODE  DT_ALIAS(gpioin)

static const struct gpio_dt_spec output_pin = GPIO_DT_SPEC_GET(OUTPUT_NODE, gpios);
static const struct gpio_dt_spec input_pin  = GPIO_DT_SPEC_GET(INPUT_NODE, gpios);

int main(void)
{
	if (!gpio_is_ready_dt(&output_pin) || !gpio_is_ready_dt(&input_pin)) {
		printk("GPIO not ready\n");
		return 0;
	}

	gpio_pin_configure_dt(&output_pin, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&input_pin, GPIO_INPUT);

	printk("GPIO demo: toggling D0, reading D1 every 500ms\n");

	bool val = false;
	while (1) {
		gpio_pin_set_dt(&output_pin, (int)val);
		int in = gpio_pin_get_dt(&input_pin);
		printk("D0=%d  D1=%d\n", (int)val, in);
		val = !val;
		k_msleep(500);
	}

	return 0;
}
