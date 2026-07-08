/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB CDC ACM 1,000,000-bps line-coding echo test for XIAO STM32C5.
 * PA11/PA12 are USB FS DM/DP pins, not hardware UART TX/RX pins.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#define TARGET_BAUDRATE 1000000U
#define ECHO_RING_BUF_SIZE 1024
#define HEARTBEAT_INTERVAL_MS 5000

static const struct device *const cdc_dev =
	DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static uint8_t echo_ring_buffer[ECHO_RING_BUF_SIZE];
static struct ring_buf echo_ringbuf;
static uint32_t rx_bytes;
static uint32_t tx_bytes;
static uint32_t drop_bytes;

static void cdc_puts(const char *s)
{
	while (*s != '\0') {
		uart_poll_out(cdc_dev, (unsigned char)*s);
		s++;
	}
}

static void cdc_print_uint(uint32_t value)
{
	char buf[11];
	int pos = ARRAY_SIZE(buf) - 1;

	buf[pos] = '\0';

	if (value == 0U) {
		uart_poll_out(cdc_dev, '0');
		return;
	}

	while (value != 0U && pos > 0) {
		pos--;
		buf[pos] = '0' + (value % 10U);
		value /= 10U;
	}

	cdc_puts(&buf[pos]);
}

static void print_banner(uint32_t baudrate)
{
	cdc_puts("\r\n");
	cdc_puts("============================================\r\n");
	cdc_puts("  USB CDC Echo Test for XIAO STM32C5\r\n");
	cdc_puts("============================================\r\n");
	cdc_puts("USB-C pins:\r\n");
	cdc_puts("  PA11 -> USB DM / D-\r\n");
	cdc_puts("  PA12 -> USB DP / D+\r\n");
	cdc_puts("Mode:\r\n");
	cdc_puts("  USB CDC ACM virtual serial port\r\n");
	cdc_puts("  This is not a hardware UART waveform on PA11/PA12.\r\n");
	cdc_puts("Host setting:\r\n");
	cdc_puts("  Open the USB CDC COM port at 1000000 baud, 8N1.\r\n");
	cdc_puts("  CDC baud is USB line coding; USB FS physical signaling is 12 Mbps.\r\n");
	cdc_puts("Current host line coding baud: ");
	cdc_print_uint(baudrate);
	cdc_puts("\r\n");

	if (baudrate != TARGET_BAUDRATE) {
		cdc_puts("WARNING: host did not set 1000000 baud.\r\n");
	}

	cdc_puts("Behavior: every received byte is echoed immediately.\r\n");
	cdc_puts("Heartbeat: printed every 5 seconds after the USB CDC port is open.\r\n");
	cdc_puts("Type characters now:\r\n\r\n");
}

static void print_heartbeat(uint32_t dtr, uint32_t baudrate)
{
	cdc_puts("[heartbeat] dtr=");
	cdc_print_uint(dtr);
	cdc_puts(" baud=");
	cdc_print_uint(baudrate);
	cdc_puts(" rx=");
	cdc_print_uint(rx_bytes);
	cdc_puts(" tx=");
	cdc_print_uint(tx_bytes);
	cdc_puts(" drop=");
	cdc_print_uint(drop_bytes);
	cdc_puts("\r\n");
}

static void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t buf[64];
	int len;

	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			len = uart_fifo_read(dev, buf, sizeof(buf));
			if (len > 0) {
				uint32_t stored = ring_buf_put(&echo_ringbuf, buf, len);

				rx_bytes += len;
				if (stored < (uint32_t)len) {
					drop_bytes += (uint32_t)len - stored;
				}
				uart_irq_tx_enable(dev);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			len = ring_buf_get(&echo_ringbuf, buf, sizeof(buf));
			if (len == 0) {
				uart_irq_tx_disable(dev);
				continue;
			}

			len = uart_fifo_fill(dev, buf, len);
			if (len > 0) {
				tx_bytes += len;
			}
		}
	}
}

int main(void)
{
	uint32_t dtr = 0U;
	uint32_t baudrate = 0U;
	bool banner_printed = false;

	if (!device_is_ready(cdc_dev)) {
		return 0;
	}

	ring_buf_init(&echo_ringbuf, sizeof(echo_ring_buffer), echo_ring_buffer);
	(void)uart_line_ctrl_set(cdc_dev, UART_LINE_CTRL_DCD, 1);
	(void)uart_line_ctrl_set(cdc_dev, UART_LINE_CTRL_DSR, 1);

	uart_irq_callback_user_data_set(cdc_dev, serial_cb, NULL);
	uart_irq_rx_enable(cdc_dev);

	while (true) {
		(void)uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_DTR, &dtr);
		(void)uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);

		if (dtr != 0U && !banner_printed) {
			print_banner(baudrate);
			banner_printed = true;
		}

		if (dtr != 0U) {
			print_heartbeat(dtr, baudrate);
		}

		k_msleep(HEARTBEAT_INTERVAL_MS);
	}

	return 0;
}
