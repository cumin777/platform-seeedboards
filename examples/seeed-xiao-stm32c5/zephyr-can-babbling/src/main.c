/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 CAN babbling sample.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

#define CANBUS_NODE DT_CHOSEN(zephyr_canbus)
#define CAN_ID 0x010

static void tx_callback(const struct device *dev, int error, void *user_data)
{
	struct k_sem *tx_sem = user_data;

	ARG_UNUSED(dev);

	if (error != 0) {
		printk("CAN TX error: %d\n", error);
	}

	k_sem_give(tx_sem);
}

int main(void)
{
	const struct device *const can_dev = DEVICE_DT_GET(CANBUS_NODE);
	struct can_frame frame = {
		.id = CAN_ID,
		.dlc = 8,
		.data = {0x58, 0x49, 0x41, 0x4f, 0x2d, 0x43, 0x35, 0x00},
	};
	struct k_sem tx_sem;
	int ret;

	printk("XIAO STM32C5 CAN babbling sample\n");

	if (!device_is_ready(can_dev)) {
		printk("CAN device %s is not ready\n", can_dev->name);
		return 0;
	}

	ret = can_start(can_dev);
	if (ret != 0) {
		printk("Failed to start CAN controller: %d\n", ret);
		return 0;
	}

	k_sem_init(&tx_sem, 1, 1);
	printk("Babbling on %s with standard CAN ID 0x%03x\n", can_dev->name, CAN_ID);

	while (true) {
		k_sem_take(&tx_sem, K_FOREVER);

		ret = can_send(can_dev, &frame, K_MSEC(100), tx_callback, &tx_sem);
		if (ret != 0) {
			printk("Failed to enqueue CAN frame: %d\n", ret);
			k_sem_give(&tx_sem);
			k_sleep(K_MSEC(100));
		}
	}
}
