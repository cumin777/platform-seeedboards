/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 CAN babbling sample.
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

#define CANBUS_NODE DT_CHOSEN(zephyr_canbus)
#define CAN_ID 0x010
#define CAN_BITRATE 500000U

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

static void print_can_status(const struct device *dev, const char *prefix)
{
	struct can_bus_err_cnt err_cnt = {0};
	enum can_state state = CAN_STATE_STOPPED;
	int ret;

	ret = can_get_state(dev, &state, &err_cnt);
	if (ret != 0) {
		printk("%s CAN state read failed: %d\n", prefix, ret);
		return;
	}

	printk("%s CAN state=%s rxerr=%u txerr=%u\n",
	       prefix, can_state_name(state), err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
}

static void print_can_timing(const struct device *dev)
{
	struct can_timing timing = {0};
	uint32_t core_clock = 0U;
	uint32_t total_tq;
	int ret;

	ret = can_get_core_clock(dev, &core_clock);
	if (ret != 0) {
		printk("CAN core clock read failed: %d\n", ret);
		return;
	}

	ret = can_calc_timing(dev, &timing, CAN_BITRATE, 0);
	if (ret < 0) {
		printk("CAN timing calc failed for %u bps: %d\n", CAN_BITRATE, ret);
		return;
	}

	total_tq = 1U + timing.prop_seg + timing.phase_seg1 + timing.phase_seg2;
	printk("CAN timing %u bps: core=%u Hz prescaler=%u sjw=%u prop=%u phase1=%u phase2=%u sample=%u.%u%% err=%d\n",
	       CAN_BITRATE, core_clock, timing.prescaler, timing.sjw,
	       timing.prop_seg, timing.phase_seg1, timing.phase_seg2,
	       (100U * (1U + timing.prop_seg + timing.phase_seg1)) / total_tq,
	       ((1000U * (1U + timing.prop_seg + timing.phase_seg1)) / total_tq) % 10U,
	       ret);
}

static void state_change_callback(const struct device *dev, enum can_state state,
				  struct can_bus_err_cnt err_cnt, void *user_data)
{
	ARG_UNUSED(user_data);

	printk("CAN state change: %s rxerr=%u txerr=%u\n",
	       can_state_name(state), err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
}

static void tx_callback(const struct device *dev, int error, void *user_data)
{
	struct k_sem *tx_sem = user_data;
	static uint32_t tx_error_count;

	if (error != 0) {
		tx_error_count++;

		if (tx_error_count <= 4U || (tx_error_count % 16U) == 0U) {
			printk("CAN TX error: %d%s count=%u\n",
			       error, error == -ENETUNREACH ? " (bus-off)" : "",
			       tx_error_count);
			print_can_status(dev, "tx");
		}
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

	print_can_timing(can_dev);

	ret = can_set_mode(can_dev, 0);
	if (ret != 0) {
		printk("Failed to set CAN mode: %d\n", ret);
		return 0;
	}

	ret = can_set_bitrate(can_dev, CAN_BITRATE);
	if (ret != 0) {
		printk("Failed to set CAN bitrate %u: %d\n", CAN_BITRATE, ret);
		return 0;
	}

	can_set_state_change_callback(can_dev, state_change_callback, NULL);

	ret = can_start(can_dev);
	if (ret != 0) {
		printk("Failed to start CAN controller: %d\n", ret);
		return 0;
	}

	k_sem_init(&tx_sem, 1, 1);
	printk("Babbling on %s at %u bps with standard CAN ID 0x%03x\n",
	       can_dev->name, CAN_BITRATE, CAN_ID);
	print_can_status(can_dev, "start");

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
