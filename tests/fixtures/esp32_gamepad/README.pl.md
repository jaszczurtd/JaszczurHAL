# Projekt testowy kompilacji i linkowania gamepada Classic na ESP32

Ten projekt, który nie wykonuje testów sprzętowych, wybiera profil gamepada
Bluetooth Classic HID na oryginalnym targecie ESP32. Sprawdza wybór funkcji,
zależności Bluedroid i HID Host, kompilację, linkowanie oraz generowanie
artefaktów.

Nie weryfikuje zachowania radia ani parowania z fizycznym kontrolerem.

```sh
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32_gamepad \
  --target esp32 \
  --board esp32-devkitc-v4 \
  --clean
```
