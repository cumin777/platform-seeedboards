# XIAO STM32C5 Full-Feature Firmware

This firmware combines the XIAO STM32C5 feature samples into one Zephyr app:

- USB CDC ACM console at 1,000,000 baud host line coding
- A0-A3 ADC voltage printing
- A4/D4/PB7 hardware PWM at 1 kHz, 50% duty, using TIM4_CH2
- User LED blink
- Battery voltage readout through BAT_EN/PE2 and BAT_Reading/PA4
- LSM6DS3TR-C IMU temperature PI control, six-axis data printing, and PID printing
- Continuous external flash erase/write/read/verify

Notes:

- A4/D4 is PB7 on this board. This firmware disables I2C1 so PB7 can be used as TIM4_CH2 PWM.
- The onboard IMU uses I2C2 on PB3/PB4 and is not affected by disabling I2C1.
- The battery sense path is internal BAT_Reading/PA4, not the XIAO D4/PB7 header pin.

Build:

```sh
pio run
```

Monitor:

```sh
pio device monitor -b 1000000
```
