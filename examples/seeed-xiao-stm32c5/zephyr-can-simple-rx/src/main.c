/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO STM32C5 minimal Classic CAN RX sample.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/sys_io.h>

#define CANBUS_NODE DT_CHOSEN(zephyr_canbus)
#define CAN_PHY_NODE DT_NODELABEL(can_phy0)
#define CAN_BITRATE 500000U
#define RX_QUEUE_DEPTH 16

#define FDCAN2_BASE_ADDR 0x4000a800UL
#define GPIOB_BASE_ADDR 0x42020400UL
#define GPIO_IDR_OFFSET 0x10UL
#define FDCAN2_RX_PIN 5U
#define FDCAN2_TX_PIN 13U
#define RCC_BASE_ADDR 0x44020c00UL
#define RCC_APB1HENR_ADDR (RCC_BASE_ADDR + 0xa0UL)
#define RCC_CCIPR1_ADDR (RCC_BASE_ADDR + 0xd8UL)
#define RCC_FDCANSEL_SHIFT 26U
#define RCC_FDCANSEL_MASK (0x3UL << RCC_FDCANSEL_SHIFT)

#if DT_NODE_HAS_PROP(CAN_PHY_NODE, standby_gpios)
static const struct gpio_dt_spec can_stb_gpio = GPIO_DT_SPEC_GET(CAN_PHY_NODE, standby_gpios);
#endif

struct rx_item {
	struct can_frame frame;
	uint32_t uptime_ms;
};

K_MSGQ_DEFINE(rx_msgq, sizeof(struct rx_item), RX_QUEUE_DEPTH, 4);

static atomic_t rx_count;
static atomic_t rx_dropped;

static const char *lec_name(uint32_t lec)
{
	switch (lec) {
	case 0:
		return "none";
	case 1:
		return "stuff";
	case 2:
		return "form";
	case 3:
		return "ack";
	case 4:
		return "bit1";
	case 5:
		return "bit0";
	case 6:
		return "crc";
	case 7:
		return "no-change";
	default:
		return "?";
	}
}

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

	printk("%s state=%s rxerr=%u txerr=%u rx=%ld dropped=%ld\n",
	       prefix, can_state_name(state), err_cnt.rx_err_cnt, err_cnt.tx_err_cnt,
	       atomic_get(&rx_count), atomic_get(&rx_dropped));
}

static void print_can_stb(const char *prefix)
{
#if DT_NODE_HAS_PROP(CAN_PHY_NODE, standby_gpios)
	int value;

	if (!gpio_is_ready_dt(&can_stb_gpio)) {
		printk("%s CAN_STB GPIO not ready\n", prefix);
		return;
	}

	value = gpio_pin_get_dt(&can_stb_gpio);
	printk("%s CAN_STB=%d (%s)\n", prefix, value,
	       value == 0 ? "normal/active" : "standby");
#else
	ARG_UNUSED(prefix);
#endif
}

static uint32_t sample_gpio_edges(uint32_t pin, uint32_t sample_count)
{
	uint32_t mask = BIT(pin);
	uint32_t last = sys_read32(GPIOB_BASE_ADDR + GPIO_IDR_OFFSET) & mask;
	uint32_t edges = 0U;

	for (uint32_t i = 0; i < sample_count; i++) {
		uint32_t now = sys_read32(GPIOB_BASE_ADDR + GPIO_IDR_OFFSET) & mask;

		if (now != last) {
			edges++;
			last = now;
		}
	}

	return edges;
}

static void print_fdcan_pin_activity(const char *prefix)
{
	uint32_t idr = sys_read32(GPIOB_BASE_ADDR + GPIO_IDR_OFFSET);
	uint32_t rx_edges = sample_gpio_edges(FDCAN2_RX_PIN, 200000U);
	uint32_t tx_edges = sample_gpio_edges(FDCAN2_TX_PIN, 200000U);

	printk("%s GPIOB_IDR=%08x PB5(FDCAN2_RX)=%u edges=%u PB13(FDCAN2_TX)=%u edges=%u\n",
	       prefix, idr,
	       (idr & BIT(FDCAN2_RX_PIN)) ? 1U : 0U, rx_edges,
	       (idr & BIT(FDCAN2_TX_PIN)) ? 1U : 0U, tx_edges);
}

static void print_fdcan_diag(const char *prefix)
{
	uint32_t cccr = sys_read32(FDCAN2_BASE_ADDR + 0x018UL);
	uint32_t ecr = sys_read32(FDCAN2_BASE_ADDR + 0x040UL);
	uint32_t psr = sys_read32(FDCAN2_BASE_ADDR + 0x044UL);
	uint32_t ir = sys_read32(FDCAN2_BASE_ADDR + 0x050UL);
	uint32_t ie = sys_read32(FDCAN2_BASE_ADDR + 0x054UL);
	uint32_t rxf0s = sys_read32(FDCAN2_BASE_ADDR + 0x0a4UL);
	uint32_t rxf1s = sys_read32(FDCAN2_BASE_ADDR + 0x0b8UL);
	uint32_t txbrp = sys_read32(FDCAN2_BASE_ADDR + 0x0c8UL);
	uint32_t txbar = sys_read32(FDCAN2_BASE_ADDR + 0x0ccUL);
	uint32_t ccipr1 = sys_read32(RCC_CCIPR1_ADDR);
	uint32_t fdcan_sel = (ccipr1 & RCC_FDCANSEL_MASK) >> RCC_FDCANSEL_SHIFT;

	printk("%s FDCAN CCCR=%08x init=%u cce=%u asm=%u mon=%u dar=%u "
	       "ECR=%08x rec=%u tec=%u cel=%u PSR=%08x lec=%s dlec=%s bo=%u ew=%u ep=%u act=%u\n",
	       prefix, cccr,
	       (cccr & BIT(0)) ? 1U : 0U,
	       (cccr & BIT(1)) ? 1U : 0U,
	       (cccr & BIT(2)) ? 1U : 0U,
	       (cccr & BIT(5)) ? 1U : 0U,
	       (cccr & BIT(6)) ? 1U : 0U,
	       ecr, (ecr >> 8) & 0x7fU, ecr & 0xffU, (ecr >> 16) & 0xffU,
	       psr, lec_name(psr & 0x7U), lec_name((psr >> 8) & 0x7U),
	       (psr & BIT(7)) ? 1U : 0U,
	       (psr & BIT(6)) ? 1U : 0U,
	       (psr & BIT(5)) ? 1U : 0U,
	       (psr >> 3) & 0x3U);
	printk("%s FDCAN IR=%08x IE=%08x RXF0S=%08x RXF1S=%08x TXBRP=%08x TXBAR=%08x "
	       "RCC_APB1HENR=%08x CCIPR1=%08x FDCANSEL=%u\n",
	       prefix, ir, ie, rxf0s, rxf1s, txbrp, txbar,
	       sys_read32(RCC_APB1HENR_ADDR), ccipr1, fdcan_sel);
	print_can_stb(prefix);
	print_fdcan_pin_activity(prefix);
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
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	printk("CAN state change: %s rxerr=%u txerr=%u\n",
	       can_state_name(state), err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
}

static void rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
{
	struct rx_item item = {
		.frame = *frame,
		.uptime_ms = k_uptime_get_32(),
	};

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (k_msgq_put(&rx_msgq, &item, K_NO_WAIT) == 0) {
		atomic_inc(&rx_count);
	} else {
		atomic_inc(&rx_dropped);
	}
}

static void print_frame(const struct rx_item *item)
{
	const struct can_frame *frame = &item->frame;
	uint8_t len = can_dlc_to_bytes(frame->dlc);

	printk("RX t=%u id=%s 0x%0*x dlc=%u len=%u data=",
	       item->uptime_ms,
	       (frame->flags & CAN_FRAME_IDE) != 0U ? "ext" : "std",
	       (frame->flags & CAN_FRAME_IDE) != 0U ? 8 : 3,
	       frame->id, frame->dlc, len);

	for (uint8_t i = 0; i < len; i++) {
		printk("%02x%s", frame->data[i], i + 1U == len ? "" : " ");
	}

	printk("\n");
}

int main(void)
{
	const struct device *const can_dev = DEVICE_DT_GET(CANBUS_NODE);
	const struct can_filter std_filter = {
		.flags = 0,
		.id = 0,
		.mask = 0,
	};
	const struct can_filter ext_filter = {
		.flags = CAN_FILTER_IDE,
		.id = 0,
		.mask = 0,
	};
	int ret;

	printk("XIAO STM32C5 simple Classic CAN RX sample\n");

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

	ret = can_add_rx_filter(can_dev, rx_callback, NULL, &std_filter);
	if (ret < 0) {
		printk("Failed to add standard RX filter: %d\n", ret);
		return 0;
	}
	printk("Standard RX filter id: %d\n", ret);

	ret = can_add_rx_filter(can_dev, rx_callback, NULL, &ext_filter);
	if (ret < 0) {
		printk("Failed to add extended RX filter: %d\n", ret);
		return 0;
	}
	printk("Extended RX filter id: %d\n", ret);

	can_set_state_change_callback(can_dev, state_change_callback, NULL);

	ret = can_start(can_dev);
	if (ret != 0) {
		printk("Failed to start CAN controller: %d\n", ret);
		return 0;
	}

	printk("Listening on %s at %u bps, all standard/extended Classic CAN frames\n",
	       can_dev->name, CAN_BITRATE);
	print_can_status(can_dev, "start");
	print_fdcan_diag("start");

	while (true) {
		struct rx_item item;

		ret = k_msgq_get(&rx_msgq, &item, K_SECONDS(2));
		if (ret == 0) {
			print_frame(&item);
		} else {
			print_can_status(can_dev, "idle");
			print_fdcan_diag("idle");
		}
	}
}
