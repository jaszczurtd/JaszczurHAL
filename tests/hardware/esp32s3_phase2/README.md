# ESP32-S3 Phase 2 hardware probe

This project validates the Phase 2 peripheral HAL on the
`waveshare-esp32-s3-zero` profile. It needs only the board's native USB cable;
no external sensor, jumper, or SPI/I2C device is required.

The firmware checks:

- system time, architecture, UID, heap, die temperature, watchdog, reset/fault
  boundary, and explicit unsupported operations;
- FreeRTOS mutexes, critical sections, and `app_task0`/`app_task1` affinity on
  cores 0/1;
- GPIO input with pull-up, output/readback, and a same-owner reconfigured GPIO
  interrupt;
- 12-bit ADC readings driven apart by the GPIO's internal pull-down/pull-up;
- hardware UART1 TX/RX through one GPIO-matrix loopback pin;
- I2C master bus clear, initialization, and a complete address scan (zero
  discovered devices is valid for an unwired board);
- SPI2 master transactions, blocking DMA, and the synchronous async-DMA
  fallback without assuming received data from an absent slave;
- managed default-pool GPTimer pause/resume, repeated ISR callbacks, and
  teardown;
- bidirectional debug traffic over the startup console's native USB
  Serial/JTAG VFS.

Build and materialize the relocatable artifact manifest:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware \
  --clean

python3 scripts/build_esp_idf.py artifacts \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware
```

Use the stable `/dev/serial/by-id/...` alias of the board on Linux (or its COM
port on Windows) for both flash and verification:

```bash
python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_phase2/verify_phase2.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

The verifier sends `PING` and accepts only a complete `status=PASS` report. A
missing callback, wrong core, stalled RX path, out-of-range ADC result, or
failed peripheral status therefore cannot be reported as a successful smoke
test.
