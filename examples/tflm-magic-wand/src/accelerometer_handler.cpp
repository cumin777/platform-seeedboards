/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from Zephyr samples/modules/tflite-micro/magic_wand/accelerometer_handler.cpp.
 * Original used adi_adxl345; this targets the board's LSM6DS3TR-C IMU (alias imu0). */
#include "accelerometer_handler.hpp"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <tensorflow/lite/micro/micro_log.h>

#define BUFLEN 300
int begin_index = 0;
const struct device *const sensor = DEVICE_DT_GET(DT_ALIAS(imu0));
int current_index = 0;

float bufx[BUFLEN] = { 0.0f };
float bufy[BUFLEN] = { 0.0f };
float bufz[BUFLEN] = { 0.0f };

bool initial = true;

TfLiteStatus SetupAccelerometer()
{
	if (!device_is_ready(sensor)) {
		printk("LSM6DSL IMU not ready\n");
		return kTfLiteApplicationError;
	}

	MicroPrintf("Got accelerometer (LSM6DS3TR-C): %s\n", sensor->name);
	return kTfLiteOk;
}

bool ReadAccelerometer(float *input, int length)
{
	int rc;
	struct sensor_value accel[3];
	int samples_count;

	rc = sensor_sample_fetch(sensor);
	if (rc < 0) {
		MicroPrintf("Fetch failed\n");
		return false;
	}
	if (!rc) {
		return false;
	}

	samples_count = rc;
	for (int i = 0; i < samples_count; i++) {
		rc = sensor_channel_get(sensor, SENSOR_CHAN_ACCEL_XYZ, accel);
		if (rc < 0) {
			MicroPrintf("ERROR: Update failed: %d\n", rc);
			return false;
		}
		bufx[begin_index] = (float)sensor_value_to_double(&accel[0]);
		bufy[begin_index] = (float)sensor_value_to_double(&accel[1]);
		bufz[begin_index] = (float)sensor_value_to_double(&accel[2]);
		begin_index++;
		if (begin_index >= BUFLEN) {
			begin_index = 0;
		}
	}

	if (initial && begin_index >= 100) {
		initial = false;
	}

	if (initial) {
		return false;
	}

	int sample = 0;
	for (int i = 0; i < (length - 3); i += 3) {
		int ring_index = begin_index + sample - length / 3;
		if (ring_index < 0) {
			ring_index += BUFLEN;
		}
		input[i] = bufx[ring_index];
		input[i + 1] = bufy[ring_index];
		input[i + 2] = bufz[ring_index];
		sample++;
	}
	return true;
}
