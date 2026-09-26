# ESP32-S3 ADC scan hardware test

`tests/hardware/esp32s3_adc_scan` validates the continuous ADC scan on a
Waveshare ESP32-S3-Zero. The scan runs on GPIO3 and GPIO4 at 12 us per
conversion (400 frames, 9.6 ms per block). The probe checks that blocks arrive
in order with rising completion times, that the newest sample follows GPIO3
between an internal pull-down and pull-up both through `hal_adc_scan_latest()`
and through `hal_adc_read()` without any block being taken in between, that a
pin outside the scan reads 0 while the scan owns ADC1, that four flash sector
writes (the last sector of the NVS partition, unused here) succeed while the
scan goes on (lost blocks are counted from the sequence),
and that stop hands the converter back to one-shot reads and a second start
works. The WS2812 on GPIO21 is blue during the checks, green on PASS and red
on FAIL. Nothing needs to be wired; only the ring's geometry is judged.

Build, materialize the artifacts and flash through the native USB Serial/JTAG
port, as for the Phase 2 probe:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_adc_scan \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_adc_scan_hardware \
  --clean

python3 scripts/build_esp_idf.py artifacts \
  --project tests/hardware/esp32s3_adc_scan \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_adc_scan_hardware

python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_adc_scan \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_adc_scan_hardware \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_adc_scan/verify_esp32s3_adc_scan.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

The verifier accepts only a `status=PASS` report whose every check flag is
set, whose frame period is 24000 ns, with at least 20 blocks in the 300 ms
window, pull-down below 600 and pull-up above 3400 on both read paths, an
unscanned pin at 0 and four flash writes.
