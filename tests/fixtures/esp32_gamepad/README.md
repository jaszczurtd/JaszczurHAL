# ESP32 Classic gamepad compile/link fixture

This non-hardware fixture selects the Bluetooth Classic HID gamepad profile on
the original ESP32 target. Its link probe covers the public Classic manager,
generic raw HID Host, and gamepad adapter. It verifies feature resolution,
Bluedroid/HID dependencies, compilation, linking, and artifact generation.

It does not verify radio behavior or pairing with a physical controller.

```sh
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32_gamepad \
  --target esp32 \
  --board esp32-devkitc-v4 \
  --clean
```
