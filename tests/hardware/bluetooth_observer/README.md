# Bluetooth Observer hardware test

`tests/hardware/bluetooth_observer` validates the passive BLE Observer API on
Raspberry Pi Pico W, Pico 2 W, and STM32G474 Nucleo with PIM730/RM2. It starts
passive legacy scanning, drains the bounded report queue, parses AD structures,
and records Teltonika company data, iBeacon, and Eddystone signatures without
initiating a BLE connection.

Build and upload each board separately:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target rp2040 --board picow

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target rp2350-arm --board pico2w

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target stm32g474 --board nucleo-g474re-pim730
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
