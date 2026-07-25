# RP SDLogger hardware probe

This fixture validates the shared SDLogger with a physical SPI SD card. It
mounts the card, opens the EEPROM-numbered log, appends deterministic content,
flushes and closes the file, resets through the watchdog, remounts the card,
checks the exact appended file tail, and verifies that the EEPROM log counter
persisted.

Connect a 3.3 V SPI SD module to a Pico or Pico 2:

| SD signal | RP GPIO | Pico physical pin |
|---|---:|---:|
| MISO | GP16 | 21 |
| CS | GP17 | 22 |
| SCK | GP18 | 24 |
| MOSI | GP19 | 25 |
| 3V3 | 3V3(OUT) | 36 |
| GND | GND | 23 |

Build, upload, and verify the bare-metal RP2040 variant:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_sdlogger \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_sdlogger \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_sdlogger/verify_sdlogger.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico --runtime baremetal
```

For Pico 2, select `rp2350-arm` or `rp2350-riscv`, use the `pico2` build board,
and pass `--board pico-2` to the verifier. Add `--variant freertos` to build
and upload commands and use `--runtime freertos` for the FreeRTOS run.

The verifier is repeatable without formatting the card. If an old log file
with the same name exists, it validates the newly appended deterministic tail.
