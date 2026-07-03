/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 CAN counter sample.
 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#define CANBUS_NODE DT_CHOSEN(zephyr_canbus)
#define LED_MSG_ID 0x10
#define COUNTER_MSG_ID 0x12345
#define SLEEP_TIME K_MSEC(500)

static const struct device *const can_dev = DEVICE_DT_GET(CANBUS_NODE);
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

CAN_MSGQ_DEFINE(counter_msgq, 4);

static void tx_callback(const struct device *dev, int error, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (error != 0) {
		printf("CAN TX callback error: %d\n", error);
	}
}

static void handle_received_counter(void)
{
	struct can_frame frame;

	while (k_msgq_get(&counter_msgq, &frame, K_NO_WAIT) == 0) {
		if ((frame.flags & CAN_FRAME_RTR) != 0U || frame.dlc != 2U) {
			continue;
		}

		printf("Counter received: %u\n",
		       sys_be16_to_cpu(UNALIGNED_GET((uint16_t *)&frame.data[0])));
	}
}

int main(void)
{
	const struct can_filter counter_filter = {
		.flags = CAN_FILTER_IDE,
		.id = COUNTER_MSG_ID,
		.mask = CAN_EXT_ID_MASK,
	};
	struct can_frame led_frame = {
		.id = LED_MSG_ID,
		.dlc = 1,
	};
	struct can_frame counter_frame = {
		.flags = CAN_FRAME_IDE,
		.id = COUNTER_MSG_ID,
		.dlc = 2,
	};
	uint16_t counter = 0;
	bool led_on = false;
	int ret;

	printf("XIAO STM32C5 CAN counter sample\n");

	if (!device_is_ready(can_dev)) {
		printf("CAN device %s is not ready\n", can_dev->name);
		return 0;
	}

	if (led.port != NULL && gpio_is_ready_dt(&led)) {
		ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			printf("LED configure failed: %d\n", ret);
			led.port = NULL;
		}
	}

	ret = can_add_rx_filter_msgq(can_dev, &counter_msgq, &counter_filter);
	if (ret < 0) {
		printf("Failed to add CAN RX filter: %d\n", ret);
		return 0;
	}
	printf("Counter filter ID: %d\n", ret);

	ret = can_start(can_dev);
	if (ret != 0) {
		printf("Failed to start CAN controller: %d\n", ret);
		return 0;
	}

	while (true) {
		led_on = !led_on;
		led_frame.data[0] = led_on ? 1U : 0U;
		(void)can_send(can_dev, &led_frame, K_MSEC(100), tx_callback, NULL);

		UNALIGNED_PUT(sys_cpu_to_be16(counter), (uint16_t *)&counter_frame.data[0]);
		printf("Counter sent: %u\n", counter);
		counter++;
		(void)can_send(can_dev, &counter_frame, K_MSEC(100), tx_callback, NULL);

		if (led.port != NULL) {
			(void)gpio_pin_set_dt(&led, led_on ? 1 : 0);
		}

		handle_received_counter();
		k_sleep(SLEEP_TIME);
	}
}
