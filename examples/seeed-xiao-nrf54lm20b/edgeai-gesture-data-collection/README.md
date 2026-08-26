# Edge AI Gesture Data Collection

This sample collects training data from the onboard LSM6DS3TR-C IMU on the
Seeed Studio XIAO nRF54LM20B. It samples the accelerometer and gyroscope at
100 Hz and sends labelled CSV records through the board's USB CDC ACM serial
port.

## What the sample does

The firmware is idle after startup and does not stream sensor data until a
recording is started. A label is selected over USB CDC before recording. While
recording, one line is printed every 10 ms:

```text
accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,label
```

The six sensor values are integers in the same format as the gesture
recognition sample:

- acceleration: `m/s^2 * 1000`
- angular velocity: `rad/s * 1000`
- channel order: `accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z`

The label is appended as the seventh column. Startup messages and command
responses are also printed, so keep only valid seven-column CSV lines when
creating a dataset.

## Requirements

- XIAO nRF54LM20B
- USB cable with data support
- PlatformIO Core
- A serial terminal such as `pio device monitor`, PuTTY, or a Python serial
  tool

The USB CDC port is the board's default console port. The baud rate is not
used by USB CDC, but `115200` is the conventional monitor setting.

## Build and upload

Run these commands from the repository root:

```powershell
pio run -d examples/seeed-xiao-nrf54lm20b/edgeai-gesture-data-collection `
  -e seeed-xiao-nrf54lm20b -t clean

pio run -d examples/seeed-xiao-nrf54lm20b/edgeai-gesture-data-collection `
  -e seeed-xiao-nrf54lm20b -t upload
```

To open the serial monitor:

```powershell
pio device list
pio device monitor -b 115200
```

If more than one serial port is connected, select the board explicitly with
`--port`, for example:

```powershell
pio device monitor --port COM12 -b 115200
```

After flashing, wait for the USB CDC port to reconnect before opening the
monitor.

## USB CDC commands

Send one command followed by Enter. Labels may contain 1–31 ASCII letters,
digits, `_`, or `-`.

```text
help
label idle
label swipe_left
start
stop
status
```

Command behavior:

- `label <name>` selects the label for future records.
- `start` begins streaming. A label must be selected first.
- `stop` stops streaming immediately.
- `status` reports the selected label and recording state.
- `help` prints the command list.

Changing the label while recording is not recommended. Stop the current
recording first, select the next label, and then start again.

## Recommended capture workflow

1. Open the USB CDC serial monitor.
2. Select one class, for example `label swipe_left`.
3. Send `start`.
4. Keep the board still for 1–2 seconds.
5. Perform one gesture.
6. Keep the board still for another 1–2 seconds.
7. Send `stop`.
8. Save the captured lines under the selected class, for example
   `gesture_dataset/swipe_left/user01_001.csv`.
9. Repeat with independent recordings and the other classes.

Recommended initial classes are `idle`, `swipe_left`, `swipe_right`, `shake`,
`rotate_left`, and `rotate_right`. Collect at least 100 independent samples
per class and vary the operator, hand, orientation, speed, and force.

## Example session

```text
XIAO nRF54LM20B Edge AI gesture data collector
format: accel/gyro values are SI units multiplied by 1000
commands: label <name>, start, stop, status, help
select a label, then start recording
label swipe_left
ok: label=swipe_left
start
ok: recording label=swipe_left
9812,-35,104,2,-1,0,swipe_left
9807,-42,98,3,0,-2,swipe_left
stop
ok: stopped
```

For model training, split recordings by capture batch or operator. Do not
randomly split overlapping windows from one continuous recording into the
training, validation, and test sets.
