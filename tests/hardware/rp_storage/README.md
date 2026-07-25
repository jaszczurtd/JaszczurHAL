# RP native storage hardware probe

This fixture validates native EEPROM and LittleFS on physical RP2040/RP2350
hardware. It commits an EEPROM boot counter, formats and remounts the LittleFS
partition, resets through the watchdog, then verifies EEPROM persistence and a
LittleFS mount without another format.

Build and upload through the regular native workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_storage \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_storage \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Run the verifier:

```sh
python3 tests/hardware/rp_storage/verify_storage.py \
  --port /dev/serial/by-id/<device>
```

Use `rp2350-arm` or `rp2350-riscv` with board `pico2` for Pico 2.
