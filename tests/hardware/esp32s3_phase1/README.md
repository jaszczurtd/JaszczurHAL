# ESP32-S3 Phase 1 hardware probe

This probe closes the target, board, build, flash, and monitor plumbing for the
Waveshare ESP32-S3-Zero SKU 25081. It intentionally does not exercise the GPIO,
serial, bus, networking, or storage HAL APIs planned for Phase 2.

The firmware reports the exact generated target and board identity, then checks
the detected chip model, core count, physical flash size, PSRAM initialization,
and physical PSRAM size against the board registry. `verify_phase1.py` obtains
its expectations from the same target and board descriptors and waits for the
repeated report on the native USB Serial/JTAG port.

Use the stable `/dev/serial/by-id/` path when available. Build and validate the
artifacts through the production ESP-IDF runner:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_phase1 \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --output .build/hardware/esp32s3_phase1 --clean
python3 scripts/build_esp_idf.py artifacts \
  --project tests/hardware/esp32s3_phase1 \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --output .build/hardware/esp32s3_phase1
```

The same project is tracked as a `jh-vscode` ESP-IDF project. Set `PORT` to the
stable alias of the connected board, then build, refresh IntelliSense, upload,
and monitor through the public workflow:

```bash
PORT="/dev/serial/by-id/<Espressif-USB-Serial-JTAG-device>"
vscode/entry/jh-vscode config-dump \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode build \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode refresh-intellisense \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode upload \
  --project tests/hardware/esp32s3_phase1 --port "$PORT"
vscode/entry/jh-vscode monitor \
  --project tests/hardware/esp32s3_phase1 --port "$PORT" \
  --lock-policy replace-own
```

The selected device must match the board profile's USB Serial/JTAG VID/PID
`303a:1001`. To test upload handoff, leave the monitor running and invoke the
same `upload` command from a second terminal. The upload must release only this
project's monitor, flash the complete three-image manifest, reset the board,
and allow the monitor to reconnect.

Stop the monitor before running the standalone verifier because both commands
take exclusive ownership of the serial port:

```bash
python3 tests/hardware/esp32s3_phase1/verify_phase1.py \
  --port "$PORT"
```

A successful run prints one JSON object with `"phase": "task0"`, a sequence
of at least one, and `"status": "PASS"`.

## Verified Phase 1 baseline

The physical closure run completed with a clean 555-step ESP-IDF build. The
application image was 150544 bytes with 86% of its partition free. Three complete
uploads each flashed the bootloader, partition table, and application image.
The runtime report matched an ESP32-S3 with two cores, 4194304 bytes of physical
flash, and initialized 2097152-byte Quad PSRAM. The persistent ESP monitor also
released the port for upload, reconnected after reset, and resumed the repeated
`app_task0()` heartbeat.

This result validates the Phase 1 target/board/build/flash/monitor contract for
the Waveshare ESP32-S3-Zero SKU 25081. It does not extend support to the GPIO,
serial, bus, networking, storage, or optional second-task surfaces assigned to
Phase 2.
