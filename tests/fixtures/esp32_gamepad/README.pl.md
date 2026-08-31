# Fixture buildu/linkowania gamepada Classic na ESP32

To stanowisko bez testu sprzętowego wybiera profil gamepada Bluetooth Classic
HID na oryginalnym targetcie ESP32. Sprawdza rozwiązywanie funkcji, zależności
Bluedroid i HID Host, build, linkowanie oraz generowanie artefaktów.

Nie weryfikuje zachowania radia ani parowania z fizycznym kontrolerem.

```sh
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32_gamepad \
  --target esp32 \
  --board esp32-devkitc-v4 \
  --clean
```
