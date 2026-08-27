/*
 * XIAO nRF54LM20B Edge AI gesture data collector.
 *
 * USB CDC commands:
 *   label <name>  Select the label written to each sample.
 *   start         Begin CSV output.
 *   stop          Stop CSV output.
 *   status        Print the current label and recording state.
 *
 * While recording, each line is:
 * accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,label
 * Acceleration is m/s2*1000 and angular velocity is rad/s*1000.
 */
#include "imu.h"

#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define SAMPLE_RATE_HZ 100
#define LABEL_MAX_LEN 31
#define COMMAND_MAX_LEN 64
#define COMMAND_RX_QUEUE_SIZE 128

static const struct device *const console_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static struct k_sem sample_ready;
static struct k_mutex output_lock;
static volatile bool recording;
static char selected_label[LABEL_MAX_LEN + 1];
K_MSGQ_DEFINE(command_rx_msgq, sizeof(uint8_t), COMMAND_RX_QUEUE_SIZE, 1);

static void imu_ready(void)
{
	k_sem_give(&sample_ready);
}

static void print_status(void)
{
	k_mutex_lock(&output_lock, K_FOREVER);
	printk("status: label=%s recording=%s\r\n", selected_label[0] ? selected_label : "(unset)",
	       recording ? "yes" : "no");
	k_mutex_unlock(&output_lock);
}

static bool valid_label(const char *label)
{
	size_t len = strlen(label);
	if (len == 0 || len > LABEL_MAX_LEN) return false;
	for (size_t i = 0; i < len; ++i) {
		char c = label[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
	}
	return true;
}

static void handle_command(char *command)
{
	char *arg;
	while (*command == ' ' || *command == '\t') ++command;
	if (strncmp(command, "label ", 6) == 0) {
		arg = command + 6;
		if (valid_label(arg)) {
			strncpy(selected_label, arg, sizeof(selected_label) - 1);
			selected_label[sizeof(selected_label) - 1] = '\0';
			printk("ok: label=%s\r\n", selected_label);
		} else {
			printk("error: label must be 1-%d chars [A-Za-z0-9_-]\r\n", LABEL_MAX_LEN);
		}
	} else if (strcmp(command, "start") == 0) {
		if (!selected_label[0]) {
			printk("error: select a label first with: label <name>\r\n");
		} else {
			recording = true;
			printk("ok: recording label=%s\r\n", selected_label);
		}
	} else if (strcmp(command, "stop") == 0) {
		recording = false;
		printk("ok: stopped\r\n");
	} else if (strcmp(command, "status") == 0) {
		print_status();
	} else if (strcmp(command, "help") == 0) {
		printk("commands: label <name>, start, stop, status, help\r\n");
	} else if (command[0]) {
		printk("error: unknown command; use help\r\n");
	}
}

/* CDC ACM RX is interrupt driven.  Polling uart_poll_in() is unreliable on
 * some USB host/firmware combinations because data arrives asynchronously
 * from the USB device controller.  Keep the ISR short and parse commands in
 * the thread below. */
static void console_uart_callback(const struct device *dev, void *user_data)
{
	uint8_t bytes[16];
	int count;

	ARG_UNUSED(user_data);
	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (!uart_irq_rx_ready(dev)) {
			continue;
		}
		count = uart_fifo_read(dev, bytes, sizeof(bytes));
		for (int i = 0; i < count; ++i) {
			(void)k_msgq_put(&command_rx_msgq, &bytes[i], K_NO_WAIT);
		}
	}
}

static void command_thread(void *a, void *b, void *c)
{
	char line[COMMAND_MAX_LEN];
	size_t length = 0;
	uint8_t byte;
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	for (;;) {
		if (k_msgq_get(&command_rx_msgq, &byte, K_MSEC(100)) != 0) {
			continue;
		}
		if (byte == '\r' || byte == '\n') {
			if (length > 0) {
				line[length] = '\0';
				handle_command(line);
				length = 0;
			}
		} else if (length < sizeof(line) - 1 && byte >= 0x20 && byte <= 0x7e) {
			line[length++] = (char)byte;
		}
	}
}
K_THREAD_DEFINE(command_tid, 1024, command_thread, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	imu_config_t config = {
		.accel_fs_g = IMU_ACCEL_SCALE_4G,
		.gyro_fs_dps = IMU_GYRO_SCALE_1000DPS,
		.data_rate_hz = SAMPLE_RATE_HZ,
	};
	imu_data_t sample;

	k_sem_init(&sample_ready, 0, 1);
	k_mutex_init(&output_lock);
	selected_label[0] = '\0';
	k_sleep(K_MSEC(500));
	if (!device_is_ready(console_uart)) {
		return -ENODEV;
	}
	if (uart_irq_callback_user_data_set(console_uart, console_uart_callback, NULL) != 0) {
		printk("error: USB CDC RX callback setup failed\r\n");
		return -EIO;
	}
	uart_irq_rx_enable(console_uart);
	if (imu_init(&config, imu_ready) != STATUS_SUCCESS) {
		printk("error: IMU initialization failed\r\n");
		return -EIO;
	}
	printk("XIAO nRF54LM20B Edge AI gesture data collector\r\n");
	printk("format: accel/gyro values are SI units multiplied by 1000\r\n");
	printk("commands: label <name>, start, stop, status, help\r\n");
	printk("select a label, then start recording\r\n");

	for (;;) {
		k_sem_take(&sample_ready, K_FOREVER);
		if (!recording || imu_read(&sample) != STATUS_SUCCESS) continue;
		k_mutex_lock(&output_lock, K_FOREVER);
		printk("%d,%d,%d,%d,%d,%d,%s\r\n", sample.accel[0].raw, sample.accel[1].raw,
		       sample.accel[2].raw, sample.gyro[0].raw, sample.gyro[1].raw,
		       sample.gyro[2].raw, selected_label);
		k_mutex_unlock(&output_lock);
	}
}
