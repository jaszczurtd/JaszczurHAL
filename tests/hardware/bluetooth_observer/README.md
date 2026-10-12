# Bluetooth Observer hardware test

`tests/hardware/bluetooth_observer` validates the passive BLE Observer API on
Raspberry Pi Pico W, Pico 2 W, and STM32G474 Nucleo with PIM730/RM2. It starts
passive legacy scanning, drains the bounded report queue, parses AD structures,
and records Teltonika company data, iBeacon, and Eddystone signatures without
initiating a BLE connection.

`device_tree.json` describes the hardware of every assembly: `picow`
(Pico W), `pico2w` (Pico 2 W) and `nucleo-pim730` (the default).
`nucleo-pim730` is a plain NUCLEO-G474RE with an external PIM730/RM2 module;
the table below repeats the module wiring.

| PIM730/RM2 signal | NUCLEO-G474RE |
|---|---|
| CS | PB12, CN10 pin 16 |
| DAT | PB15, CN10 pin 26 |
| WL_ON | PB14, CN10 pin 28 |
| CLK | PB13, CN10 pin 30 |

Build and upload each assembly separately:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer --assembly picow

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer --assembly pico2w

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer --assembly nucleo-pim730
```

Successful output uses the `JHBL4A` prefix. Record at least one Teltonika EYE
Beacon report on each board, the total and dropped report counters, and the
ELF/map memory summary. The test remains passive: scan responses, GATT client,
connections, pairing, and bonding are outside this probe.

Use `STOP`, `START`, `REOPEN`, and `INFO` to verify scan restart, full BLE
profile reacquisition without a controller reset, and bounded diagnostics. A
valid report must arrive after both `START` and `REOPEN`.

RP2350 RISC-V is unsupported because its CYW43 Bluetooth transport is not
enabled.
