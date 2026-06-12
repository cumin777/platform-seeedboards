/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Axon NPU smoke test for the XIAO nRF54LM20B test board
 * (XIAO nRF54LM20A carrier + nRF54LM20B chip).
 *
 * This test verifies that the Axon NPU hardware is present and addressable
 * by reading identification registers and toggling the enable register.
 * It does NOT require the sdk-edge-ai module — only direct register access.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(axon_smoke, LOG_LEVEL_INF);

#define AXON_NODE  DT_NODELABEL(axon)

#if DT_NODE_HAS_STATUS(AXON_NODE, okay)

#define AXON_BASE          ((mem_addr_t)DT_REG_ADDR(AXON_NODE))
#define AXON_SIZE          DT_REG_SIZE(AXON_NODE)
#define AXON_ENABLE_OFFSET 0x400u

/* Read a 32-bit register at the given byte offset from AXON_BASE */
static inline uint32_t axon_read(uint32_t offset)
{
	return *(volatile uint32_t *)(AXON_BASE + offset);
}

/* Write a 32-bit register at the given byte offset from AXON_BASE */
static inline void axon_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(AXON_BASE + offset) = value;
}

int main(void)
{
	LOG_INF("=== Axon NPU Smoke Test ===");
	LOG_INF("DT base address : 0x%08x", (unsigned int)AXON_BASE);
	LOG_INF("DT register size: %u bytes", (unsigned int)AXON_SIZE);
	LOG_INF("DT IRQ number   : %d", (int)DT_IRQN(AXON_NODE));

	/* Read first 4 registers (offset 0x00 – 0x0c) */
	for (int i = 0; i < 4; i++) {
		uint32_t val = axon_read(i * 4);
		LOG_INF("  reg[0x%02x] = 0x%08x", i * 4, val);
	}

	/* Toggle the ENABLE register (offset 0x400, bit 0) */
	uint32_t enable_before = axon_read(AXON_ENABLE_OFFSET);
	LOG_INF("ENABLE before  : 0x%08x", enable_before);

	axon_write(AXON_ENABLE_OFFSET, enable_before | 1u);
	uint32_t enable_after_on = axon_read(AXON_ENABLE_OFFSET);
	LOG_INF("ENABLE after ON : 0x%08x", enable_after_on);

	axon_write(AXON_ENABLE_OFFSET, enable_before & ~1u);
	uint32_t enable_after_off = axon_read(AXON_ENABLE_OFFSET);
	LOG_INF("ENABLE after OFF: 0x%08x", enable_after_off);

	if (enable_after_on != (enable_before | 1u)) {
		LOG_WRN("ENABLE register did not accept the write — NPU may not be powered");
	} else {
		LOG_INF("Axon NPU register access: OK");
	}

	LOG_INF("=== Smoke Test Complete ===");
	return 0;
}

#else

int main(void)
{
	LOG_ERR("Axon NPU node not found or disabled in device tree!");
	LOG_ERR("Ensure the board variant includes the axon-npu.dtsi overlay.");
	return -1;
}

#endif /* DT_NODE_HAS_STATUS(AXON_NODE, okay) */
