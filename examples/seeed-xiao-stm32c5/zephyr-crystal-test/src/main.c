/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 HSE/LSE crystal oscillator test.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stm32_backup_domain.h>
#include <stm32_ll_rcc.h>

#define HSE_TARGET_HZ 48000000U
#define HSE_MIN_HZ 47999520U
#define HSE_MAX_HZ 48000480U

#define LSE_TARGET_MILLIHZ 32768000U
#define LSE_MIN_MILLIHZ 32767672U
#define LSE_MAX_MILLIHZ 32768328U

#define HSE_READY_TIMEOUT_MS 100U
#define LSE_READY_TIMEOUT_MS 10000U
#define STATUS_INTERVAL_MS 1000U

static bool wait_for_hse_ready(uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	while (k_uptime_get() <= deadline) {
		if (LL_RCC_HSE_IsReady()) {
			return true;
		}
		k_msleep(1);
	}

	return LL_RCC_HSE_IsReady();
}

static bool wait_for_lse_ready(uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	while (k_uptime_get() <= deadline) {
		if (LL_RCC_LSE_IsReady()) {
			return true;
		}
		k_msleep(10);
	}

	return LL_RCC_LSE_IsReady();
}

static bool start_hse(void)
{
	if (!LL_RCC_HSE_IsReady()) {
		LL_RCC_HSE_DisableBypass();
		LL_RCC_HSE_Enable();
	}

	return wait_for_hse_ready(HSE_READY_TIMEOUT_MS);
}

static bool start_lse(void)
{
	bool ready;

	stm32_backup_domain_enable_access();

	if (!LL_RCC_LSE_IsReady()) {
		LL_RCC_LSE_DisableBypass();
		LL_RCC_LSE_SetDriveCapability(LL_RCC_LSEDRIVE_MEDIUMHIGH);
		LL_RCC_LSE_Enable();
	}

	ready = wait_for_lse_ready(LSE_READY_TIMEOUT_MS);
	stm32_backup_domain_disable_access();

	return ready;
}

int main(void)
{
	bool hse_ready;
	bool lse_ready;
	uint32_t loop = 0U;

	printk("XIAO STM32C5 crystal oscillator test\n");
	printk("HSE target: %u Hz, +/-10 ppm window: %u - %u Hz\n",
	       HSE_TARGET_HZ, HSE_MIN_HZ, HSE_MAX_HZ);
	printk("LSE target: %u mHz, +/-10 ppm window: %u - %u mHz\n",
	       LSE_TARGET_MILLIHZ, LSE_MIN_MILLIHZ, LSE_MAX_MILLIHZ);
	printk("LSE drive: medium-high, STM32 drive capability code 2\n");
	printk("Measure X2/HSE and X1/LSE directly with active probe and 50 ohm input as required.\n");

	hse_ready = start_hse();
	lse_ready = start_lse();

	printk("Initial ready state: HSE=%s, LSE=%s\n",
	       hse_ready ? "ready" : "not ready",
	       lse_ready ? "ready" : "not ready");

	while (true) {
		hse_ready = LL_RCC_HSE_IsReady();
		lse_ready = LL_RCC_LSE_IsReady();

		printk("[%u] HSE %u Hz: %s; LSE %u mHz: %s\n",
		       loop++, HSE_TARGET_HZ,
		       hse_ready ? "ready" : "not ready",
		       LSE_TARGET_MILLIHZ,
		       lse_ready ? "ready" : "not ready");

		k_msleep(STATUS_INTERVAL_MS);
	}
}
