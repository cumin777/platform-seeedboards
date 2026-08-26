# Edge AI gesture data collection

This sample samples the onboard LSM6DS3TR-C at 100 Hz and exposes the board's
USB CDC ACM console for labelled CSV capture.

```text
label swipe_left
start
```

Save the seven-column lines while recording. Stop with `stop`; use `status` or
`help` for the available commands. The first six columns are, in order,
accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z. Values are SI units scaled
by 1000, matching the gesture model sample. Labels use only letters, digits,
`_`, and `-`.

Build and upload:

```powershell
pio run -d examples/seeed-xiao-nrf54lm20b -t upload
pio device monitor -b 115200
```
