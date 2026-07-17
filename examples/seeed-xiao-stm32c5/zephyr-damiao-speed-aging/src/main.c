/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 Damiao motor 7-day speed aging sample.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define CANBUS_NODE DT_CHOSEN(zephyr_canbus)

#ifndef DAMIAO_MOTOR_ID
#define DAMIAO_MOTOR_ID 1U
#endif

#define DAMIAO_SPEED_MODE_OFFSET 0x200U
#define DAMIAO_CMD_ENABLE 0xFCU
#define DAMIAO_CMD_DISABLE 0xFDU
#define DAMIAO_CMD_CLEAR_ERROR 0xFBU

#define CONTROL_PERIOD_MS 20
#define STATUS_PERIOD_MS 1000
#define STARTUP_FEEDBACK_GRACE_MS 10000
#define FEEDBACK_TIMEOUT_MS 5000

#define TEMP_WARN_C 70U
#define TEMP_COOLDOWN_ENTER_C 80U
#define TEMP_COOLDOWN_EXIT_C 65U
#define TEMP_FAULT_C 90U

static const struct device *const can_dev = DEVICE_DT_GET(CANBUS_NODE);
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

CAN_MSGQ_DEFINE(rx_msgq, 32);

struct aging_step {
	const char *name;
	float speed_rad_s;
	uint32_t duration_ms;
};

struct motor_feedback {
	uint32_t count;
	uint32_t can_id;
	uint8_t motor_id;
	uint8_t err;
	uint16_t pos_raw;
	uint16_t vel_raw;
	uint16_t torque_raw;
	uint8_t mos_temp;
	uint8_t rotor_temp;
	int64_t last_ms;
	bool valid;
};

enum aging_mode {
	AGING_MODE_PROFILE,
	AGING_MODE_COOLDOWN,
	AGING_MODE_FAULT,
};

static const struct aging_step aging_profile[] = {
	{"rest", 0.0f, 5U * 60U * 1000U},
	{"low", 3.0f, 20U * 60U * 1000U},
	{"medium", 6.0f, 20U * 60U * 1000U},
	{"low_down", 3.0f, 10U * 60U * 1000U},
	{"rest_end", 0.0f, 5U * 60U * 1000U},
};

static atomic_t tx_ok_count;
static atomic_t tx_error_count;
static atomic_t tx_submit_error_count;
static struct motor_feedback feedback;
static enum aging_mode mode = AGING_MODE_PROFILE;
static size_t profile_index;
static int64_t profile_step_start_ms;
static bool fault_reported;

static const char *can_state_name(enum can_state state)
{
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		return "error-active";
	case CAN_STATE_ERROR_WARNING:
		return "error-warning";
	case CAN_STATE_ERROR_PASSIVE:
		return "error-passive";
	case CAN_STATE_BUS_OFF:
		return "bus-off";
	case CAN_STATE_STOPPED:
		return "stopped";
	default:
		return "unknown";
	}
}

static void tx_callback(const struct device *dev, int error, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (error == 0) {
		atomic_inc(&tx_ok_count);
	} else {
		atomic_inc(&tx_error_count);
	}
}

static int damiao_send_cmd(uint8_t cmd)
{
	struct can_frame frame = {
		.id = DAMIAO_MOTOR_ID,
		.dlc = 8,
		.data = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, cmd},
	};
	int ret = can_send(can_dev, &frame, K_NO_WAIT, tx_callback, NULL);

	if (ret != 0) {
		atomic_inc(&tx_submit_error_count);
	}

	return ret;
}

static int damiao_send_speed(float speed_rad_s)
{
	struct can_frame frame = {
		.id = DAMIAO_SPEED_MODE_OFFSET + DAMIAO_MOTOR_ID,
		.dlc = 8,
	};
	int ret;

	memcpy(&frame.data[0], &speed_rad_s, sizeof(speed_rad_s));
	ret = can_send(can_dev, &frame, K_NO_WAIT, tx_callback, NULL);
	if (ret != 0) {
		atomic_inc(&tx_submit_error_count);
	}

	return ret;
}

static void parse_feedback_frame(const struct can_frame *frame)
{
	if ((frame->flags & CAN_FRAME_IDE) != 0U || frame->dlc < 8U) {
		return;
	}

	feedback.can_id = frame->id;
	feedback.motor_id = frame->data[0] & 0x0fU;
	feedback.err = frame->data[0] >> 4;
	feedback.pos_raw = ((uint16_t)frame->data[1] << 8) | frame->data[2];
	feedback.vel_raw = ((uint16_t)frame->data[3] << 4) | (frame->data[4] >> 4);
	feedback.torque_raw = ((uint16_t)(frame->data[4] & 0x0fU) << 8) | frame->data[5];
	feedback.mos_temp = frame->data[6];
	feedback.rotor_temp = frame->data[7];
	feedback.last_ms = k_uptime_get();
	feedback.count++;
	feedback.valid = true;
}

static void handle_feedback(void)
{
	struct can_frame frame;

	while (k_msgq_get(&rx_msgq, &frame, K_NO_WAIT) == 0) {
		parse_feedback_frame(&frame);
	}
}

static uint8_t feedback_max_temp(void)
{
	return feedback.mos_temp > feedback.rotor_temp ? feedback.mos_temp : feedback.rotor_temp;
}

static bool feedback_timed_out(int64_t now)
{
	if (feedback.valid) {
		return now - feedback.last_ms > FEEDBACK_TIMEOUT_MS;
	}

	return now > STARTUP_FEEDBACK_GRACE_MS;
}

static void enter_fault(const char *reason)
{
	mode = AGING_MODE_FAULT;
	(void)damiao_send_speed(0.0f);
	(void)damiao_send_cmd(DAMIAO_CMD_DISABLE);

	if (!fault_reported) {
		printf("FAULT: %s, motor disabled\n", reason);
		fault_reported = true;
	}
}

static float update_profile_and_get_speed(int64_t now)
{
	const struct aging_step *step;

	if (mode == AGING_MODE_FAULT) {
		return 0.0f;
	}

	if (feedback.valid) {
		uint8_t temp = feedback_max_temp();

		if (feedback.err != 0U) {
			enter_fault("motor feedback error code");
			return 0.0f;
		}

		if (temp >= TEMP_FAULT_C) {
			enter_fault("motor temperature fault");
			return 0.0f;
		}

		if (mode == AGING_MODE_PROFILE && temp >= TEMP_COOLDOWN_ENTER_C) {
			mode = AGING_MODE_COOLDOWN;
			printf("COOLDOWN: temp=%u C, command speed forced to 0\n", temp);
		} else if (mode == AGING_MODE_COOLDOWN && temp <= TEMP_COOLDOWN_EXIT_C) {
			mode = AGING_MODE_PROFILE;
			profile_step_start_ms = now;
			printf("RESUME: temp=%u C, restart current aging step\n", temp);
		}
	}

	if (feedback_timed_out(now)) {
		enter_fault("CAN feedback timeout");
		return 0.0f;
	}

	if (mode == AGING_MODE_COOLDOWN) {
		return 0.0f;
	}

	step = &aging_profile[profile_index];
	if (now - profile_step_start_ms >= step->duration_ms) {
		profile_index = (profile_index + 1U) % ARRAY_SIZE(aging_profile);
		profile_step_start_ms = now;
		step = &aging_profile[profile_index];
		printf("STEP: %u/%u %s %.2f rad/s\n", (uint32_t)profile_index + 1U,
		       (uint32_t)ARRAY_SIZE(aging_profile), step->name, (double)step->speed_rad_s);
	}

	return step->speed_rad_s;
}

static void print_status(float target_speed)
{
	enum can_state state = CAN_STATE_STOPPED;
	struct can_bus_err_cnt err_cnt = {0};
	int ret = can_get_state(can_dev, &state, &err_cnt);
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000);
	const struct aging_step *step = &aging_profile[profile_index];
	const char *state_text = ret == 0 ? can_state_name(state) : "unavailable";

	printf("STATUS t=%us step=%u/%u:%s mode=%d target=%.2f tx_ok=%ld tx_err=%ld tx_submit_err=%ld rx=%u can=%s tec=%u rec=%u",
	       uptime_s, (uint32_t)profile_index + 1U, (uint32_t)ARRAY_SIZE(aging_profile),
	       step->name, mode, (double)target_speed, (long)atomic_get(&tx_ok_count),
	       (long)atomic_get(&tx_error_count), (long)atomic_get(&tx_submit_error_count),
	       feedback.count, state_text, err_cnt.tx_err_cnt, err_cnt.rx_err_cnt);

#ifdef CONFIG_CAN_STATS
	printf(" ack=%u bit=%u stuff=%u crc=%u form=%u rxovr=%u",
	       can_stats_get_ack_errors(can_dev), can_stats_get_bit_errors(can_dev),
	       can_stats_get_stuff_errors(can_dev), can_stats_get_crc_errors(can_dev),
	       can_stats_get_form_errors(can_dev), can_stats_get_rx_overruns(can_dev));
#endif

	if (feedback.valid) {
		printf(" fb_id=0x%03x motor=%u err=0x%x pos=0x%04x vel=0x%03x tq=0x%03x mos=%u rotor=%u",
		       feedback.can_id, feedback.motor_id, feedback.err, feedback.pos_raw,
		       feedback.vel_raw, feedback.torque_raw, feedback.mos_temp, feedback.rotor_temp);

		if (feedback_max_temp() >= TEMP_WARN_C) {
			printf(" temp_warn");
		}
	} else {
		printf(" fb=none");
	}

	printf("\n");
}

int main(void)
{
	const struct can_filter feedback_filter = {
		.id = 0,
		.mask = 0,
	};
	int ret;
	int64_t next_status_ms;

	printf("XIAO STM32C5 Damiao speed aging sample\n");
	printf("Motor ID: %u, CAN bitrate: 1 Mbps, command period: %u ms\n",
	       DAMIAO_MOTOR_ID, CONTROL_PERIOD_MS);
	printf("Aging profile is a 1-hour loop; run continuously for the 7-day test.\n");

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

	profile_step_start_ms = k_uptime_get();
	next_status_ms = profile_step_start_ms;
	printf("STEP: 1/%u %s %.2f rad/s\n", (uint32_t)ARRAY_SIZE(aging_profile),
	       aging_profile[0].name, (double)aging_profile[0].speed_rad_s);

	while (true) {
		int64_t now = k_uptime_get();
		float target_speed;

		handle_feedback();
		target_speed = update_profile_and_get_speed(now);
		(void)damiao_send_speed(target_speed);

		if (now >= next_status_ms) {
			print_status(target_speed);
			next_status_ms += STATUS_PERIOD_MS;
		}

		if (led.port != NULL) {
			(void)gpio_pin_set_dt(&led, mode == AGING_MODE_FAULT ? 0 : ((now / 500) & 1));
		}

		k_sleep(K_MSEC(CONTROL_PERIOD_MS));
	}
}
