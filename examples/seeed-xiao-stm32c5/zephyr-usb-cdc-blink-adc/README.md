# XIAO STM32C5 USB CDC Blink ADC

Small bring-up sample with only:

- USB CDC ACM UART output, not Zephyr console
- User LED blink
- A0-A3 ADC voltage printing every 5 seconds
- A4/D4/PB7 PWM output at 1 kHz, 50% duty
- IMU heater PA8/TIM1_CH1 PWM test alternating fixed 0%/10% duty
- IMU temperature readout using direct polled I2C register reads
- Battery voltage readout through BAT_EN/PE2 and BAT_Reading/PA4
- External flash erase/write/read/verify every 5 seconds

Build:

```sh
pio run
```

Monitor:

```sh
pio device monitor -b 1000000
```

Behavior:

- No banner is printed.
- The user LED toggles in its own thread every 500 ms and does not depend on the USB CDC port being open.
- USB CDC output is attempted every 5 seconds; DTR is printed only as a diagnostic state.
- Each 5-second report is grouped as `[USB]`, `[LED]`, `[ADC A0-A3]`, `[PWM]`, `[HEATER PWM]`, `[IMU TEMP]`, `[BAT]`, and `[FLASH]`.
- A4/D4 is the XIAO header PB7 pin. Battery sense uses internal PA4/ADC1_IN4, not PB7.
- Heater PWM uses the board `imu-heater` alias on PA8/TIM1_CH1.
- IMU temperature uses direct I2C register polling on the board `imu0` alias. This test does not enable IMU interrupts, sensor drivers, sensor triggers, or PID control.
- Flash test uses external flash offset `0x00100000`, writes 256 bytes, reads them back, and prints verify status.
