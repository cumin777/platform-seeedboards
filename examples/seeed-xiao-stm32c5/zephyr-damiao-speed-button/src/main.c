/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 Damiao motor speed-mode sample.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define CANBUS_NODE DT_CHOSEN(zephyr_canbus)

#ifndef DAMIAO_MOTOR_ID
#define DAMIAO_MOTOR_ID 1U
#endif

#define DAMIAO_SPEED_MODE_OFFSET 0x200U
#define DAMIAO_CMD_ENABLE 0xFCU
#define DAMIAO_CMD_DISABLE 0xFDU
#define DAMIAO_CMD_CLEAR_ERROR 0xFBU

#define CONTROL_PERIOD_MS 20
#define GEAR_PERIOD_MS 5000
#define FEEDBACK_PRINT_PERIOD_MS 500

static const struct device *const can_dev = DEVICE_DT_GET(CANBUS_NODE);
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

CAN_MSGQ_DEFINE(rx_msgq, 16);

struct speed_gear {
	uint8_t gear;
	float speed_rad_s;
	const char *label;
};

static const struct speed_gear gears[] = {
	{0U, 0.0f, "stop"},
	{1U, 3.0f, "low"},
	{2U, 6.0f, "medium"},
	{3U, 10.0f, "high"},
};

static const uint8_t gear_sequence[] = {0U, 1U, 2U, 3U, 2U, 1U};
static uint8_t sequence_pos;

static void tx_callback(const struct device *dev, int error, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (error != 0) {
		printf("CAN TX error: %d\n", error);
	}
}

static int damiao_send_cmd(uint8_t cmd)
{
	struct can_frame frame = {
		.id = DAMIAO_MOTOR_ID,
		.dlc = 8,
		.data = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, cmd},
	};

	return can_send(can_dev, &frame, K_NO_WAIT, tx_callback, NULL);
}

static int damiao_send_speed(float speed_rad_s)
{
	struct can_frame frame = {
		.id = DAMIAO_SPEED_MODE_OFFSET + DAMIAO_MOTOR_ID,
		.dlc = 8,
	};

	memcpy(&frame.data[0], &speed_rad_s, sizeof(speed_rad_s));
	return can_send(can_dev, &frame, K_NO_WAIT, tx_callback, NULL);
}

static void print_current_gear(void)
{
	const struct speed_gear *gear = &gears[gear_sequence[sequence_pos]];

	printf("Gear %u (%s): %.2f rad/s\n", gear->gear, gear->label, (double)gear->speed_rad_s);
}

static void advance_gear(void)
{
	sequence_pos = (sequence_pos + 1U) % ARRAY_SIZE(gear_sequence);
	print_current_gear();
}

static void handle_feedback(void)
{
	static int64_t last_print_time;
	struct can_frame frame;
	int64_t now = k_uptime_get();

	while (k_msgq_get(&rx_msgq, &frame, K_NO_WAIT) == 0) {
		if ((frame.flags & CAN_FRAME_IDE) != 0U || frame.dlc < 8U) {
			continue;
		}

		if (now - last_print_time < FEEDBACK_PRINT_PERIOD_MS) {
			continue;
		}

		uint8_t motor_id = frame.data[0] & 0x0fU;
		uint8_t err = frame.data[0] >> 4;
		uint16_t pos_raw = ((uint16_t)frame.data[1] << 8) | frame.data[2];
		uint16_t vel_raw = ((uint16_t)frame.data[3] << 4) | (frame.data[4] >> 4);
		uint16_t torque_raw = ((uint16_t)(frame.data[4] & 0x0fU) << 8) | frame.data[5];

		printf("FB can_id=0x%03x motor=%u err=0x%x pos=0x%04x vel=0x%03x tq=0x%03x mos=%u rotor=%u\n",
		       frame.id, motor_id, err, pos_raw, vel_raw, torque_raw, frame.data[6],
		       frame.data[7]);
		last_print_time = now;
	}
}

int main(void)
{
	const struct can_filter feedback_filter = {
		.id = 0,
		.mask = 0,
	};
	int ret;

	printf("XIAO STM32C5 Damiao speed button sample\n");
	printf("Automatic gear sequence every 5 seconds: 0->1->2->3->2->1->0\n");
	printf("Motor ID: %u, CAN bitrate: 1 Mbps\n", DAMIAO_MOTOR_ID);

	if (!device_is_ready(can_dev)) {
		printf("CAN device %s is not ready\n", can_dev->name);
		return 0;
	}

	if (led.port != NULL && gpio_is_ready_dt(&led)) {
		(void)gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}

	ret = can_set_bitrate(can_dev, 1000000U);
	if (ret != 0) {
		printf("Failed to set CAN bitrate: %d\n", ret);
		return 0;
	}

	ret = can_add_rx_filter_msgq(can_dev, &rx_msgq, &feedback_filter);
	if (ret < 0) {
		printf("Failed to add CAN RX filter: %d\n", ret);
		return 0;
	}

	ret = can_start(can_dev);
	if (ret != 0) {
		printf("Failed to start CAN controller: %d\n", ret);
		return 0;
	}

	(void)damiao_send_cmd(DAMIAO_CMD_CLEAR_ERROR);
	k_sleep(K_MSEC(100));
	(void)damiao_send_cmd(DAMIAO_CMD_ENABLE);
	k_sleep(K_MSEC(100));
	print_current_gear();

	int64_t next_gear_time = k_uptime_get() + GEAR_PERIOD_MS;

	while (true) {
		const struct speed_gear *gear = &gears[gear_sequence[sequence_pos]];

		if (k_uptime_get() >= next_gear_time) {
			advance_gear();
			next_gear_time += GEAR_PERIOD_MS;
		}

		(void)damiao_send_speed(gear->speed_rad_s);
		handle_feedback();

		if (led.port != NULL) {
			(void)gpio_pin_set_dt(&led, gear->gear != 0U);
		}

		k_sleep(K_MSEC(CONTROL_PERIOD_MS));
	}
}
