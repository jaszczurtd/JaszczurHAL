# ESP32-S3 OTA hardware test

`tests/hardware/esp32s3_ota` validates raw ESP-IDF application OTA on the
`waveshare-esp32-s3-zero` profile: discovery, AUTH2, the rejections of faulty
uploads, trial boot, a refused upload during the trial, rollback of an
unconfirmed image, confirmation, and the return to the first image. The board
needs its native USB cable and a WiFi network; the host needs Python with
`pyserial` and must accept the OTA callback on TCP/8266 and discovery replies
on UDP/8266 (see the firewall section of [OTAWorkflow.md](../../../doc/en/OTAWorkflow.md)).

The firmware joins the network as `jh-ota-esp32s3`, starts the OTA service on
UDP/8266 and answers single-byte commands on the debug serial port: `S`
prints a `JH_ESP32_OTA` status line (image, address, boot mode, versions,
OTA errors), `C` confirms the running image and `R` restarts the board. The
`IMAGE_B` variant differs only in the image name it reports.

The verifier checks:

- image A runs stable and answers unicast discovery with its slot size;
- a wrong password, an AUTH2 answer replayed against a new nonce, an answer
  from another UDP source and a pre-AUTH2 answer are rejected or ignored;
- a filesystem image (command 100) is refused;
- an upload that the host ends after authentication is given up at once, not
  after the receive timeout;
- an image whose MD5 differs from the announcement, data that is not an ESP
  application, an announcement larger than the slot and a transfer stopped
  halfway are all refused, and image A keeps running;
- image B is accepted and boots as a trial; an upload during the trial is
  refused; a restart without confirmation rolls back to A;
- image B again, confirmed, stays after a restart;
- image A uploaded and confirmed: the board ends where it started.

## Settings

Create `ota_test_config.h` next to `app.cpp`. Git ignores it:

```c
#pragma once
#define JH_OTA_TEST_SSID "network-name"
#define JH_OTA_TEST_WIFI_PASSWORD "network-password"
#define JH_OTA_TEST_PASSWORD "ota-password"
```

The verifier reads the OTA password from this file.

## Build, flash and verify

Each image builds into its own directory, so both stay available:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_ota --assembly esp32s3-zero \
  --name jh_esp32_ota --output .build/esp-idf/image-a
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_ota --assembly esp32s3-zero \
  --name jh_esp32_ota --variant IMAGE_B --output .build/esp-idf/image-b

python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_ota --assembly esp32s3-zero \
  --name jh_esp32_ota --output .build/esp-idf/image-a \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_ota/verify_ota.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

`--output` is relative to the project. The serial flash writes the partition
table, the OTA data and image A, so the board starts from slot 0. The verifier
prints one JSON line per check and a summary; it fails when any check fails.

## Hardware coverage

The test passes on the Waveshare ESP32-S3-Zero. A power cut during the
transfer or right after the boot partition switch is not part of it; that
needs a controlled power switch.
