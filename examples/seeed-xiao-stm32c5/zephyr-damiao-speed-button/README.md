# XIAO STM32C5 Damiao Speed Button Sample

This sample controls a Damiao DM-J4310-2EC-style motor through Classic CAN at
1 Mbps. It uses Damiao speed mode and automatically cycles through speed gears.

## Wiring

```text
XIAO CAN_H  -> motor CAN_H
XIAO CAN_L  -> motor CAN_L
XIAO GND    -> motor GND / 24V-
24V+        -> motor VCC
24V-        -> motor GND

```

## Speed Gears

The official motor data lists rated speed as 120 rpm and no-load maximum speed
as 200 rpm. This sample uses conservative test speeds:

| Gear | Speed | Approx rpm |
| --- | --- | --- |
| 0 | 0.0 rad/s | 0 rpm |
| 1 | 3.0 rad/s | 28.6 rpm |
| 2 | 6.0 rad/s | 57.3 rpm |
| 3 | 10.0 rad/s | 95.5 rpm |

The firmware advances one gear every 5 seconds:

```text
0 -> 1 -> 2 -> 3 -> 2 -> 1 -> 0
```

## Build

```powershell
cd D:\workspace\platform-seeedboards\examples\seeed-xiao-stm32c5\zephyr-damiao-speed-button
pio run
```

The build output includes:

```text
.pio\build\seeed-xiao-stm32c5\firmware.uf2
```

## Notes

The MCU sends target speed commands only. Damiao's internal driver handles the
motor commutation, current loop, velocity loop, encoder feedback, and protection.
If a controlled command ramp is needed later, add it by interpolating the speed
command in this sample.
