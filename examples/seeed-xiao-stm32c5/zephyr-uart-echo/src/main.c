/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART echo: receives a line from USART1 and echoes the whole string back.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <string.h>

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_console)
#define RX_QUEUE_SIZE 128
#define LINE_BUF_SIZE 128
#define HEARTBEAT_INTERVAL_MS 5000
#define IDLE_FLUSH_MS 1000

K_MSGQ_DEFINE(uart_rx_msgq, sizeof(uint8_t), RX_QUEUE_SIZE, 4);

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static atomic_t irq_cb_count;
static atomic_t rx_bytes;
static atomic_t rx_lines;
static atomic_t rx_dropped;
static atomic_t main_msg_get;

static void uart_send_buf(const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

static void uart_send_str(const char *str)
{
	uart_send_buf((const uint8_t *)str, strlen(str));
}

void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	ARG_UNUSED(user_data);
	atomic_inc(&irq_cb_count);

	if (!uart_irq_update(dev) || !uart_irq_rx_ready(dev)) {
		return;
	}

	while (uart_fifo_read(dev, &c, 1) == 1) {
		atomic_inc(&rx_bytes);
		if (k_msgq_put(&uart_rx_msgq, &c, K_NO_WAIT) != 0) {
			atomic_inc(&rx_dropped);
		}
	}
}

static void print_echo_line(char *line_buf, size_t *line_pos)
{
	if (*line_pos == 0) {
		return;
	}

	line_buf[*line_pos] = '\0';
	atomic_inc(&rx_lines);
	uart_send_str("\r\n[echo] ");
	uart_send_str(line_buf);
	uart_send_str("\r\n");
	*line_pos = 0;
}

static void process_rx_byte(uint8_t c, char *line_buf, size_t *line_pos)
{
	if (c == '\r' || c == '\n') {
		print_echo_line(line_buf, line_pos);
		return;
	}

	if (*line_pos < (LINE_BUF_SIZE - 1)) {
		line_buf[(*line_pos)++] = (char)c;
	} else {
		uart_send_str("\r\n[warn] line buffer full, clearing partial line\r\n");
		*line_pos = 0;
	}
}

int main(void)
{
	char line_buf[LINE_BUF_SIZE];
	size_t line_pos = 0;
	int64_t next_heartbeat = k_uptime_get() + HEARTBEAT_INTERVAL_MS;
	int64_t last_rx_time = 0;

	if (!device_is_ready(uart_dev)) {
		printk("UART device not ready\n");
		return 0;
	}

	uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
	uart_irq_rx_enable(uart_dev);

	uart_send_str("\r\n============================================\r\n");
	uart_send_str("  UART Echo Test for XIAO STM32C5\r\n");
	uart_send_str("============================================\r\n");
	uart_send_str("Mode: interrupt-driven RX + polling TX\r\n");
	uart_send_str("Port: USART1, XIAO D6/TX(PA9) and D7/RX(PA10), 115200 8N1\r\n");
	uart_send_str("Behavior: received line is echoed after CR/LF.\r\n");
	uart_send_str("If no CR/LF is sent, buffered text is echoed after 1 s idle.\r\n");
	uart_send_str("Type characters now:\r\n");

	while (true) {
		uint8_t c;
		int64_t now = k_uptime_get();
		int64_t wait_ms = next_heartbeat - now;

		if (wait_ms < 0) {
			wait_ms = 0;
		}
		if (line_pos > 0 && wait_ms > IDLE_FLUSH_MS) {
			wait_ms = IDLE_FLUSH_MS;
		}

		if (k_msgq_get(&uart_rx_msgq, &c, K_MSEC(wait_ms)) == 0) {
			atomic_inc(&main_msg_get);
			last_rx_time = k_uptime_get();
			process_rx_byte(c, line_buf, &line_pos);
		} else if (line_pos > 0 &&
			   (k_uptime_get() - last_rx_time) >= IDLE_FLUSH_MS) {
			print_echo_line(line_buf, &line_pos);
		}

		now = k_uptime_get();
		if (now >= next_heartbeat) {
			char heartbeat[160];

			snprintk(heartbeat, sizeof(heartbeat),
				 "\r\n[heartbeat] uptime=%u ms irq_cb=%ld rx_bytes=%ld "
				 "rx_lines=%ld q_drop=%ld main_get=%ld\r\n",
				 k_uptime_get_32(),
				 atomic_get(&irq_cb_count),
				 atomic_get(&rx_bytes),
				 atomic_get(&rx_lines),
				 atomic_get(&rx_dropped),
				 atomic_get(&main_msg_get));
			uart_send_str(heartbeat);
			next_heartbeat = now + HEARTBEAT_INTERVAL_MS;
		}
	}

	return 0;
}
