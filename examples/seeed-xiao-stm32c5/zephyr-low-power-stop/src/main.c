/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 low-power STOP test.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <cmsis_core.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_cortex.h>
#include <stm32_ll_exti.h>
#include <stm32_ll_gpio.h>
#include <stm32_ll_pwr.h>
#include <stm32_ll_usart.h>

#define LED0_NODE DT_ALIAS(led0)
#define CAN_PHY_NODE DT_NODELABEL(can_phy0)
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

#if DT_NODE_HAS_PROP(CAN_PHY_NODE, standby_gpios)
static const struct gpio_dt_spec can_stb =
	GPIO_DT_SPEC_GET(CAN_PHY_NODE, standby_gpios);
#endif

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

		gpio_pin_set_dt(&led, 0);
	} else {
		for (int remaining = PREPARE_SECONDS; remaining > 0; remaining--) {
			printk("Entering %s in %d second(s)\n", mode, remaining);
			k_msleep(1000);
		}
	}
}

static void clear_pending_irqs(void)
{
	LL_EXTI_ClearRisingFlag_0_31(LL_EXTI_LINE_ALL_0_31);
	LL_EXTI_ClearFallingFlag_0_31(LL_EXTI_LINE_ALL_0_31);
	LL_EXTI_ClearRisingFlag_32_63(LL_EXTI_LINE_ALL_32_63);
	LL_EXTI_ClearFallingFlag_32_63(LL_EXTI_LINE_ALL_32_63);

	for (size_t i = 0; i < ARRAY_SIZE(NVIC->ICPR); i++) {
		NVIC->ICPR[i] = UINT32_MAX;
	}

	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
}

static void release_console_and_usb_pins(void)
{
	LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);

	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9 | LL_GPIO_PIN_10 |
				  LL_GPIO_PIN_11 | LL_GPIO_PIN_12,
			   LL_GPIO_MODE_ANALOG);
	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_9 | LL_GPIO_PIN_10 |
				  LL_GPIO_PIN_11 | LL_GPIO_PIN_12,
			   LL_GPIO_PULL_NO);
}

static void disable_console_uart(void)
{
#ifdef USART1
	NVIC_DisableIRQ(USART1_IRQn);
	NVIC_ClearPendingIRQ(USART1_IRQn);

	LL_USART_Disable(USART1);
	LL_APB2_GRP1_DisableClock(LL_APB2_GRP1_PERIPH_USART1);
	LL_APB2_GRP1_DisableClockLowPower(LL_APB2_GRP1_PERIPH_USART1);
#endif
}

static void disable_usb_peripheral(void)
{
#if IS_ENABLED(CONFIG_UDC_DRIVER) && DT_NODE_EXISTS(DT_NODELABEL(zephyr_udc0))
	const struct device *const udc = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));

	if (device_is_ready(udc)) {
		(void)udc_disable(udc);
	}
#endif

#ifdef USB_DRD_FS_IRQn
	NVIC_DisableIRQ(USB_DRD_FS_IRQn);
	NVIC_ClearPendingIRQ(USB_DRD_FS_IRQn);
#endif

#ifdef LL_APB2_GRP1_PERIPH_USB
	LL_APB2_GRP1_DisableClock(LL_APB2_GRP1_PERIPH_USB);
	LL_APB2_GRP1_DisableClockLowPower(LL_APB2_GRP1_PERIPH_USB);
#endif
}

static void disable_debug_low_power_emulation(void)
{
#ifdef DBGMCU
	DBGMCU->CR &= ~(DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP |
		       DBGMCU_CR_DBG_STANDBY | DBGMCU_CR_TRACE_IOEN |
		       DBGMCU_CR_TRACE_EN);
#endif
}

static void prepare_external_flash_for_low_power(void)
{
	LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
	LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);

	/* Keep the NOR flash deselected, with WP#/HOLD# at valid CMOS levels.
	 * Leave SCLK/IO0/IO1 analog to avoid fighting the flash or board pulls.
	 */
	LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_10, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_7 | LL_GPIO_PIN_6, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_10, LL_GPIO_PULL_NO);
	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_7 | LL_GPIO_PIN_6, LL_GPIO_PULL_NO);

	LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_10);
	LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_7 | LL_GPIO_PIN_6);

	LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_2 | LL_GPIO_PIN_1, LL_GPIO_MODE_ANALOG);
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_5, LL_GPIO_MODE_ANALOG);
	LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_2 | LL_GPIO_PIN_1, LL_GPIO_PULL_NO);
	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_5, LL_GPIO_PULL_NO);
}

static void prepare_board_for_stop(void)
{
#if DT_NODE_HAS_PROP(CAN_PHY_NODE, standby_gpios)
	if (gpio_is_ready_dt(&can_stb)) {
		(void)gpio_pin_configure_dt(&can_stb, GPIO_OUTPUT_ACTIVE);
	}
#endif

	if (gpio_is_ready_dt(&led)) {
		(void)gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}

	prepare_external_flash_for_low_power();
	release_console_and_usb_pins();
	disable_console_uart();
	disable_usb_peripheral();
	disable_debug_low_power_emulation();
	clear_pending_irqs();
}

int main(void)
{
	prepare_for_low_power(LOW_POWER_STOP_NAME);

	printk("Entering %s now. Reset the board to exit this test.\n", LOW_POWER_STOP_NAME);
	k_msleep(20);

	SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
	prepare_board_for_stop();
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
