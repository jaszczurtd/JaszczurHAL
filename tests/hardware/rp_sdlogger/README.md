# RP SDLogger hardware test

`tests/hardware/rp_sdlogger` validates the shared SDLogger with a physical SPI
SD card. It mounts the card, opens the EEPROM-numbered log, appends
deterministic content, flushes and closes the file, resets through the
watchdog, remounts the card, checks the exact appended file tail, and verifies
that the EEPROM log counter persisted.

`device_tree.json` describes the wiring of every assembly: `pico` for Pico,
`pico2-arm` and `pico2-riscv` for Pico 2. The card is the node `sd` on SPI0,
clocked at the 12 MHz the SD driver uses for data. The program takes the bus,
its pins and the card select from the generated header, and the table below
repeats that wiring.

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
  --assembly pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_sdlogger \
  --assembly pico \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_sdlogger/verify_sdlogger.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico --runtime baremetal
```

For Pico 2 build with `--assembly pico2-arm` or `--assembly pico2-riscv` and
pass `--target rp2350-arm` or `--target rp2350-riscv` with `--board pico2` to
the verifier. Add `--variant FREERTOS` to build and upload commands and use
`--runtime freertos` for the FreeRTOS run.

The verifier is repeatable without formatting the card. If an old log file
with the same name exists, it validates the newly appended deterministic tail.
