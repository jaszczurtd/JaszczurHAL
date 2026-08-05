# Bluetooth Observer hardware probe

This probe validates the experimental passive BLE Observer API on Raspberry Pi
Pico W and STM32G474 Nucleo with PIM730/RM2. It starts passive legacy scanning,
drains the bounded report queue, parses AD structures, and records Teltonika
company data, iBeacon, and Eddystone signatures without initiating a BLE
connection.

Build and upload each board separately:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target rp2040 --board picow

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target stm32g474 --board nucleo-g474re-pim730
```

Successful output uses the `JHBL4A` prefix. Record at least one Teltonika EYE
Beacon report on each board, the total and dropped report counters, and the
ELF/map memory summary. The test remains passive: scan responses, GATT client,
connections, pairing, and bonding are outside this probe.

## Recorded hardware result - 2026-08-05

Both boards completed the passive scan with `HAL_OK`, entered
`HAL_BLE_STATE_SCANNING`, recognized Teltonika company ID `0x089A`, and
reported no queue loss:

| Board | Reports observed | Signatures | Dropped | FLASH load | Static SRAM | Reserved SRAM |
|---|---:|---|---:|---:|---:|---:|
| RP2040 Pico W | 4 | Teltonika: 1, Eddystone: 3 | 0 | 407.2 KiB | 58.0 KiB | 6.0 KiB |
| STM32G474 Nucleo + PIM730 | 15 | Teltonika: 1, Eddystone: 3 | 0 | 334.5 KiB | 49.1 KiB | 3.0 KiB |

The Nucleo trace included the complete Teltonika report from
`7C:D9:F4:14:38:8C`, local name `MP1_FEE349`, RSSI -89 dBm, and manufacturer
data beginning with little-endian company ID `9A 08`. The Pico W summary
independently recorded one Teltonika report. Duplicate filtering was enabled,
so each distinct advertisement was retained only once.
