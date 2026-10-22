# ESP32-S3 network hardware test

`tests/hardware/esp32s3_network` runs the native ESP-IDF network backend on
the `waveshare-esp32-s3-zero` profile against a fixture on a host in the same
LAN. The board needs only its native USB cable and a WiFi network; the host
needs Python with `pyserial` and must accept inbound connections on the
fixture ports (18190-18193).

The firmware checks:

- station mode, a scan that finds the configured network, a join with a wrong
  password (`HAL_EAUTH`) and a join to an absent network (`HAL_ENOENT`);
- the join with DHCP, DNS resolution of `example.com` and a ping to the host;
- UDP echo with the sender's port, and a receive that waits for its timeout;
- TCP echo, a refused connection, end of stream after the peer closes, and
  backpressure: sends to a host that never reads return at once with a partial
  count until the socket reports `HAL_EAGAIN`;
- invalidation of an open TCP socket by a disconnect, a new join and TCP echo
  after it;
- WiFi switched off with an open UDP socket, switched on again, a new join,
  DNS and UDP after it;
- the HTTP and WebSocket servers started with `hal_http_server_start_ex()` and
  `hal_websocket_server_start_ex()` on the board's address; the host fetches a
  page and exchanges a WebSocket message.

The status LED blinks blue while the firmware waits for the host, stays blue
while the checks run, and turns green on PASS or red on FAIL.

## Network settings

Create `net_test_config.h` next to `app.cpp`. Git ignores it, so the network
credentials stay on the machine:

```c
#pragma once
#define JH_NET_TEST_SSID "network-name"
#define JH_NET_TEST_PASSWORD "network-password"
#define JH_NET_TEST_HOST "192.168.1.10"  /* host running verify_network.py */
```

The firmware announces the host address and its ports; the verifier serves
exactly those. Port numbers live only in `app.cpp`.

## Build, flash and verify

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_network \
  --assembly esp32s3-zero \
  --name jh_esp32_network_hardware

python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_network \
  --assembly esp32s3-zero \
  --name jh_esp32_network_hardware \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_network/verify_network.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

The verifier starts its services after the firmware's `ready` line, sends
`GO`, probes the servers when the firmware reports `check=serve`, and then
sends `DONE`. It succeeds only when every check passed, none is missing and
both host probes succeeded; it prints one JSON summary.

## Hardware coverage

The test passes on the Waveshare ESP32-S3-Zero. Outside it, these network
areas still need hardware tests: IPv6, loss of the DHCP address, concurrent
socket use from both tasks, teardown during blocking operations, and the
TLS, MQTT, OTA and WireGuard paths.
