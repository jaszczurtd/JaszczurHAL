# Fixture build/link ESP32-S3 Phase 3

Ten fixture bez testu sprzętowego wybiera w jednym projekcie ESP-IDF wszystkie
backendy ESP32-S3 zaimplementowane do Phase 3. CI i lokalny pełny gate używają
go do sprawdzania rozwiązywania funkcji, wyboru źródeł i zależności, builda,
linkowania, generowania partycji oraz artefaktów.

Nie jest to sprzętowy test akceptacyjny. Weryfikacja runtime WiFi, socketów,
TLS, usług, OTA oraz peryferiów ukończonych w Phase 2 należy do Phase 3.5.
Udany build tego fixture nie potwierdza ich działania na sprzęcie.

Polecenie builda:

```sh
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --clean
```
