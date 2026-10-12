# RP native storage hardware test

`tests/hardware/rp_storage` validates native EEPROM and LittleFS on physical
RP2040/RP2350 hardware. It commits an EEPROM boot counter, formats and remounts
the LittleFS partition, resets through the watchdog, then verifies EEPROM
persistence and a LittleFS mount without another format.

Build and upload through the regular native workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_storage \
  --assembly pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_storage \
  --assembly pico \
  --port /dev/serial/by-id/<device>
```

Run the verifier:

```sh
python3 tests/hardware/rp_storage/verify_storage.py \
  --port /dev/serial/by-id/<device>
```

`device_tree.json` describes the hardware of every assembly: `pico` for Pico,
`pico2-arm` and `pico2-riscv` for Pico 2. The test needs only the board. For
Pico 2 use `--assembly pico2-arm` or `--assembly pico2-riscv`.
