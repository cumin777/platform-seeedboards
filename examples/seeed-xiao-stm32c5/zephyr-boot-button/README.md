# XIAO STM32C5 BOOT button

This example configures the XIAO STM32C5 `PH2/BOOT0` pad as a normal user
button. The onboard LED is on while the button is held down.

The button is active-low: connect the button between `PH2/BOOT0` and GND. The
overlay enables the MCU internal pull-up.

`PH2` remains the STM32 BOOT0 strap. Do not hold the button during power-on or
reset, because a high/low level on BOOT0 can affect the selected boot mode.

Build and upload with:

```shell
pio run
pio run --target upload
```
