# Bluetooth Stage 1 hardware probe

This private probe validates the pre-API CYW43/BTstack integration on the two
boards selected by the design: STM32G474 Nucleo + PIM730 and Raspberry Pi Pico
W. It deliberately enables no public Bluetooth feature macro and must not be
used as an application API example.

The build owns BTstack sources directly and does not link `pico_cyw43_arch`,
`pico_btstack_cyw43`, or Pico SDK Bluetooth storage glue. It brings up the
existing JH Wi-Fi/lwIP owner, downloads the Bluetooth firmware through the same
CYW43 instance, starts connectable advertising as `JH BLE Stage 1`, and exposes
a bounded static read/write GATT characteristic.

Successful compilation is only the software gate. Hardware results must record
the `JHBT1` output, connection/write behaviour, ELF/map memory use, and the
exact board/wiring under test. The STM32 run additionally verifies that the
PIM730 `BT_ON` trace still follows `WL_ON` in the assembled setup.

The `bluetooth` variant is the probe; `wifi-only` is the otherwise equivalent
memory baseline. Both variants must be measured from their ELF/map files with
the same target, board, compiler, and build type.

The Stage 1 software builds measured on 2026-08-04 are:

| Target and variant | FLASH load | SRAM static | Reserved heap/stack |
|---|---:|---:|---:|
| STM32G474 + PIM730, `bluetooth` | 332.3 KiB | 50.0 KiB | 3.0 KiB |
| STM32G474 + PIM730, `wifi-only` | 276.9 KiB | 43.2 KiB | 3.0 KiB |
| RP2040 Pico W, `bluetooth` | 403.2 KiB | 60.4 KiB | 6.0 KiB |
| RP2040 Pico W, `wifi-only` | 326.0 KiB | 53.6 KiB | 6.0 KiB |

These measurements do not require a reduced ATT MTU or smaller Stage 1 queues.

Hardware substage 1.a completed on both profiles on 2026-08-04. The
STM32G474 + PIM730 probe used the wiring below. The Pico W probe used its
on-board CYW43439 and enumerated as `JaszczurHAL RP` over USB. On both boards
the probe reached controller-ready and connectable advertising states, BlueZ
resolved the static GATT service, characteristic read and write passed, and the
peripheral accepted a disconnect followed by a fresh connection and GATT read.
The matched `wifi-only` images also reported `HAL_OK`. Initial STM32 ATT
discovery exposed a missing Security Manager initialization; the probe now
initializes `sm_init()` before `att_server_init()`. Connection lifecycle is
observed through one HCI event registration so each physical link is counted
once. The final image restored to each board is the `bluetooth` variant.
The Pico W connection run recorded no drain-budget hits. The STM32 probe
recorded two bounded drain hits during controller initialization and then
remained stable with `HAL_OK` transport status.

## Hardware substage 1.a

Begin with the Nucleo disconnected from USB and all other power. Connect the
PIM730 directly with short leads:

| PIM730 | STM32G474 | Nucleo connector |
|---|---|---|
| `CS` | `PB12` | CN10 pin 16 |
| `DAT` | `PB15` | CN10 pin 26 |
| `WL_ON` | `PB14` | CN10 pin 28 |
| `CLK` | `PB13` | CN10 pin 30 |
| `GND` | GND | CN10 pin 20 |
| `3V3` | 3.3 V | CN7 pin 16 |

Do not use 5 V. Confirm visually that the PIM730 cuttable
`BT_ON`-to-`WL_ON` trace is intact; leave `BT_ON`/`BL_ON` otherwise
unconnected. Only after the wiring and trace state are confirmed should the
STM32 Bluetooth image be programmed through the Nucleo ST-Link. Record the
periodic `JHBT1` status before testing discovery, connection, characteristic
read/write, disconnect/reconnect, and the WiFi-only regression. The Pico W
on-board-radio run follows as the second hardware profile.
