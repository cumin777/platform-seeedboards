# XIAO STM32C5 Damiao Speed Aging Sample

This is the long-run aging sample for a Damiao DM-J4310-2EC style integrated
motor/driver. It keeps sending Damiao speed-mode CAN commands at 20 ms
intervals and prints a 1 Hz health log for a 7-day aging run.

The short 5-second gear-switching sample is still kept separately for waveform
observation.

## CAN Transceiver Note

Do not connect MCU `CAN_TX` / `CAN_RX` logic pins directly to the motor
`CANH` / `CANL` bus. CANH/CANL are differential bus signals and require a CAN
transceiver.

Use the XIAO board CAN bus connector/pins that are already behind the board CAN
transceiver:

```text
XIAO CAN_H  -> motor CAN_H
XIAO CAN_L  -> motor CAN_L
XIAO GND    -> motor GND / 24V-
24V+        -> motor VCC
24V-        -> motor GND
```

The motor has its own internal driver and CAN transceiver. The MCU only sends
CAN commands; it does not drive the motor phases directly.

## Aging Profile

The motor data lists 120 rpm rated speed, about 12.57 rad/s. This sample keeps
the aging speeds below rated speed:

| Step | Duration | Speed |
| --- | ---: | ---: |
| rest | 5 min | 0.0 rad/s |
| low | 20 min | 3.0 rad/s |
| medium | 20 min | 6.0 rad/s |
| low_down | 10 min | 3.0 rad/s |
| rest_end | 5 min | 0.0 rad/s |

The profile is a 1-hour loop. For a 7-day aging test, leave the firmware
running continuously.

## Protection Behavior

- Prints status once per second.
- Warns in the log at 70 C.
- Enters cooldown at 80 C and commands 0 rad/s.
- Resumes the current step when temperature drops to 65 C.
- Disables the motor at 90 C, on Damiao feedback error code, or on CAN feedback
  timeout.
- Enables Zephyr CAN statistics so ACK/bit/stuff/CRC/form/RX-overrun counters
  appear in the status log.

## Build

```powershell
cd D:\workspace\platform-seeedboards\examples\seeed-xiao-stm32c5\zephyr-damiao-speed-aging
pio run -t clean
pio run
```

The UF2 firmware is generated at:

```text
.pio\build\seeed-xiao-stm32c5\firmware.uf2
```
