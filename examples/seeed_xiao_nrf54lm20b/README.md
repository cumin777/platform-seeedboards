# XIAO nRF54LM20B samples

These projects are kept under the board-specific directory so that the nRF54LM20B adaptations remain independent from the shared sample implementations.

Available samples:

- `zephyr-epaper/2inch13`
- `zephyr-epaper/5inch83`
- `zephyr-epaper/7inch5`
- `zephyr-expansion-base-for-xiao/buzzer`
- `zephyr-expansion-base-for-xiao/i2c-sht31`
- `zephyr-expansion-base-for-xiao/oled`
- `zephyr-expansion-base-for-xiao/oled-lvgl`
- `zephyr-expansion-base-for-xiao/rtc`
- `zephyr-expansion-base-for-xiao/sd_card`
- `zephyr-gps`
- `zephyr-npm1300-register-read`

Each project contains a single `seeed-xiao-nrf54lm20b` PlatformIO environment. Build from a sample directory with:

```shell
pio run -e seeed-xiao-nrf54lm20b
```

The default console is USB CDC ACM, as provided by the XIAO nRF54LM20B board definition.
