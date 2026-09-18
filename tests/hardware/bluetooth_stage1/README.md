# Bluetooth Stage 1 hardware test

`tests/hardware/bluetooth_stage1` is an internal probe for the pre-API
CYW43/BTstack integration. Its build matrix covers STM32G474 Nucleo + PIM730,
Raspberry Pi Pico W, and RP2350 ARM Pico 2 W. It deliberately enables no public
Bluetooth feature macro and must not be used as an application API example.

Hardware runs of this probe cover STM32G474 + PIM730 and Pico W. Pico 2 W is
checked with the public
[Bluetooth Observer](../bluetooth_observer/README.md) and
[BLE Stream](../bluetooth_stream/README.md) fixtures instead. RP2350 RISC-V
is unsupported because its CYW43 Bluetooth transport is not enabled.

The build owns BTstack sources directly and does not link `pico_cyw43_arch`,
`pico_btstack_cyw43`, or Pico SDK Bluetooth storage integration. It brings up the
shared JH CYW43 radio owner through its BLE reference, downloads the Bluetooth
firmware through the same CYW43 instance, starts connectable advertising as
`JH BLE Stage 1`, and exposes a bounded static read/write GATT characteristic.

Successful compilation is only the software gate. Hardware results must record
the `JHBT1` output, connection/write behaviour, ELF/map memory use, and the
exact board/wiring under test. The STM32 run additionally verifies that the
PIM730 `BT_ON` trace still follows `WL_ON` in the assembled setup.

The `bluetooth` variant is the probe; `wifi-only` is the otherwise equivalent
memory baseline. Both variants must be measured from their ELF/map files with
the same target, board, compiler, and build type.

The `wifi-only` image excludes BTstack, the Bluetooth firmware, and the
Bluetooth pools on the shared bus.

Build the probe for each board, and repeat with `--variant wifi-only` for the
baseline:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stage1 \
  --target stm32g474 --board nucleo-g474re-pim730 --variant bluetooth
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stage1 \
  --target rp2040 --board picow --variant bluetooth
```

## Hardware substage 1.a wiring and procedure

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
