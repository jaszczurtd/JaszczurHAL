# Projekt testowy kompilacji i linkowania ESP32-S3 - etap 3

Ten projekt, który nie wykonuje testów sprzętowych, wybiera w jednym projekcie
ESP-IDF wszystkie implementacje ESP32-S3 ukończone do etapu 3, a także bazową
implementację NimBLE Peripheral/Observer. CI i lokalna pełna bramka używają
go do sprawdzania wyboru funkcji, źródeł i zależności, kompilacji, linkowania,
generowania partycji oraz artefaktów.

Nie jest to sprzętowy test akceptacyjny. Działanie WiFi, gniazd, TLS, usług,
OTA i radia BLE w czasie pracy oraz nowe peryferia ukończone w etapie 2
sprawdzają osobne testy sprzętowe. Nie należy wnioskować o ich działaniu na
podstawie samej kompilacji.

Polecenie kompilacji:

```sh
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --clean
```
