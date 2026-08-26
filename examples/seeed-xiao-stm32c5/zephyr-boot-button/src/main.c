/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 BOOT/PH2 user-button example.
 * The LED follows the button state: it is on while the button is pressed.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define BUTTON_NODE DT_ALIAS(sw0)
#define LED_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS(BUTTON_NODE, okay)
#error "sw0 devicetree alias is not defined"
#endif

#if !DT_NODE_HAS_STATUS(LED_NODE, okay)
#error "led0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

int main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button) || !gpio_is_ready_dt(&led)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return 0;
	}

	printk("Press BOOT/PH2 to turn the LED on.\n");

	while (1) {
		int pressed = gpio_pin_get_dt(&button);

		if (pressed < 0) {
			return 0;
		}

		ret = gpio_pin_set_dt(&led, pressed != 0);
		if (ret < 0) {
			return 0;
		}

		k_msleep(10);
	}
}
