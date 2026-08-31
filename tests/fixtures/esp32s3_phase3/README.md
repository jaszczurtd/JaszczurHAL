# ESP32-S3 Phase 3 compile/link fixture

This non-hardware fixture selects every ESP32-S3 backend implemented through
Phase 3 plus the base NimBLE Peripheral/Observer backend in one ESP-IDF
project. It is used by CI and the local full gate to prove feature resolution,
source/dependency selection, compilation, linking, partition generation, and
artifact production.

It is not a hardware acceptance test. Runtime WiFi, socket, TLS, service, OTA,
BLE radio behavior, and newly completed Phase 2 peripheral verification belong
to separate hardware gates and must not be inferred from this build.

Build it with:

```sh
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --clean
```
