# XIAO STM32C5 Damiao DM-J4340P-2EC V1.1 24V Speed Sample FDCAN2 External

This sample controls a Damiao DM-J4340P-2EC V1.1 24V integrated motor/driver
through Classic CAN at 1 Mbps. It uses Damiao speed mode and automatically
cycles through speed gears for CAN waveform observation.

This variant simulates using XIAO STM32C5 as a module on a user PCB: the
firmware uses the XIAO FDCAN2 controller logic pins with the user's external
CAN transceiver, while forcing the XIAO board CAN transceiver standby pin
`CAN_STB/PB14` high.

## Wiring

```text
XIAO D14 / PB13 / FDCAN2_TX -> external CAN transceiver TXD
XIAO D13 / PB5 / FDCAN2_RX  <- external CAN transceiver RXD
XIAO D15 / PB14 / CAN_STB   -> driven HIGH by firmware to disable the XIAO board transceiver
XIAO GND                    -> external CAN transceiver GND -> motor GND / 24V-
External transceiver CAN_H  -> motor CAN_H
External transceiver CAN_L  -> motor CAN_L
External transceiver VCC    -> 5V supply, if required by the transceiver board
24V+                        -> motor VCC
24V-                        -> motor GND
```

This sample selects FDCAN2 through `zephyr/app.overlay`. Zephyr still initializes
the board CAN PHY binding, then the application drives PB14 high after
`can_start()` so the XIAO board transceiver stays in standby while the FDCAN2
logic pins are used by the external transceiver. Do not connect MCU CAN_TX/CAN_RX
logic pins directly to the motor CANH/CANL differential bus.

## Speed Gears

The DM-J4340P-2EC V1.1 24V manual lists rated speed as 120 rpm and no-load
maximum speed as 200 rpm. This sample uses conservative test speeds below the
rated speed:

| Gear | Speed | Approx rpm |
| --- | ---: | ---: |
| 0 | 0.0 rad/s | 0 rpm |
| 1 | 3.0 rad/s | 28.6 rpm |
| 2 | 6.0 rad/s | 57.3 rpm |

The firmware advances one gear every 5 seconds:

```text
0 -> 3 -> 6 -> 3 -> 0 rad/s
```

## CAN Protocol

The sample assumes the motor is already configured to speed mode by the Damiao
PC tool. In speed mode the command frame is:

```text
CAN ID: 0x200 + motor_id
D[0..3]: v_des float, little-endian, rad/s
DLC: 4
```

The default `DAMIAO_MOTOR_ID` is `1`.

This sample uses normal Classic CAN frames at 1 Mbps. It does not enable CAN FD
data phase or send CAN FD frames.

At startup the firmware writes only the runtime control mode over CAN:

```text
CTRL_MODE = 3       speed mode
```

The sample reads `ACC`, `DEC`, `MAX_SPD`, `VMAX`, `KP_ASR`, `KI_ASR`, `Deta`,
and `VBus` for diagnostics, but does not write speed-loop or speed-limit
parameters and does not send Damiao's "store parameters" command.

## Build

```powershell
cd D:\workspace\platform-seeedboards\examples\seeed-xiao-stm32c5\zephyr-damiao-j4340P-2EC-v11-speed-fdcan2-external
pio run
```

The build output includes:

```text
.pio\build\seeed-xiao-stm32c5\firmware.uf2
```

## Notes

The MCU sends target speed commands only. Damiao's internal driver handles the
motor commutation, current loop, velocity loop, encoder feedback, and protection.

The firmware prints `RX rate: N frame/s` once per second. The highest practical
read rate depends on the external CAN transceiver, bus wiring, termination,
motor response rate, and selected CAN bitrate.

Reference manual used during setup:

```text
https://wiki.aifitlab.com/damiao-docs/dm-j4340p-2ec-v11-motor-instruction-manual
```
