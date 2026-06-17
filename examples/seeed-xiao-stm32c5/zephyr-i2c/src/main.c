/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C bus scanner: probes all 7-bit addresses on I2C1 and
 * reports which ones ACK.  Demonstrates the I2C controller API.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define I2C_DEV_NODE DT_NODELABEL(i2c1)

int main(void)
{
	const struct device *const i2c_dev = DEVICE_DT_GET(I2C_DEV_NODE);

	if (!device_is_ready(i2c_dev)) {
		printk("I2C device not ready\n");
		return 0;
	}

	printk("I2C bus scan on %s\n", i2c_dev->name);
	printk("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

	for (uint8_t row = 0; row < 8; row++) {
		printk("%02x: ", row * 16);
		for (uint8_t col = 0; col < 16; col++) {
			uint8_t addr = row * 16 + col;

			/* Skip reserved addresses */
			if ((addr & 0x78) == 0x00 || (addr & 0x78) == 0x78) {
				printk("-- ");
				continue;
			}

			uint8_t dummy;
			int ret = i2c_read(i2c_dev, &dummy, 0, addr);
			printk(ret == 0 ? "%02x " : "-- ", addr);
		}
		printk("\n");
	}

	printk("Scan complete.\n");
	return 0;
}
