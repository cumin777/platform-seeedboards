/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 low-power STOP test.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <cmsis_core.h>
#include <stm32_ll_cortex.h>
#include <stm32_ll_pwr.h>

#define LED0_NODE DT_ALIAS(led0)
#define PREPARE_SECONDS 5

#ifndef LOW_POWER_STOP_MODE
#define LOW_POWER_STOP_MODE LL_PWR_STOP1_MODE
#endif

#if LOW_POWER_STOP_MODE == LL_PWR_STOP0_MODE
#define LOW_POWER_STOP_NAME "STOP0"
#else
#define LOW_POWER_STOP_NAME "STOP1"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static void prepare_for_low_power(const char *mode)
{
	printk("XIAO STM32C5 low-power %s test\n", mode);
	printk("Device will run normally for %d seconds, then enter %s.\n",
	       PREPARE_SECONDS, mode);

	if (gpio_is_ready_dt(&led) && gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) == 0) {
		for (int remaining = PREPARE_SECONDS; remaining > 0; remaining--) {
			gpio_pin_toggle_dt(&led);
			printk("Entering %s in %d second(s)\n", mode, remaining);
			k_msleep(1000);
		}

		gpio_pin_set_dt(&led, 1);
	} else {
		for (int remaining = PREPARE_SECONDS; remaining > 0; remaining--) {
			printk("Entering %s in %d second(s)\n", mode, remaining);
			k_msleep(1000);
		}
	}
}

int main(void)
{
	prepare_for_low_power(LOW_POWER_STOP_NAME);

	printk("Entering %s now. Reset the board to exit this test.\n", LOW_POWER_STOP_NAME);
	k_msleep(20);

	SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
	__disable_irq();

	LL_PWR_ClearFlag_STOP();
	LL_PWR_EnableFlashLowPWRMode();
	LL_PWR_SetPowerMode(LOW_POWER_STOP_MODE);
	SCB_EnableDeepSleep();

	while (true) {
		__DSB();
		__WFI();
	}

	return 0;
}
