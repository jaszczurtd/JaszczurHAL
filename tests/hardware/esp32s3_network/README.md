# ESP32-S3 network hardware test

`tests/hardware/esp32s3_network` runs the native ESP-IDF network backend on
the `waveshare-esp32-s3-zero` profile against a fixture on a Linux host in the
same LAN. The board needs only its native USB cable and a WiFi network; the
host needs Python with `pyserial` (and `cryptography` for the SERVICES
suite) and must accept inbound connections on the fixture ports.

The default build runs the basic suite; the `SERVICES` variant runs the
services suite.

JaszczurHAL supports IPv4 networking. The fixture also runs IPv6 link-local
probes of the native ESP-IDF stack as additional backend diagnostics. These
probes do not establish IPv6 as a supported JaszczurHAL feature. The same
support boundary applies to IPv6 available through host stacks such as Windows.

## Basic suite

The firmware checks:

- station mode, a scan that finds the configured network, a join with a wrong
  password (`HAL_EAUTH`) and a join to an absent network (`HAL_ENOENT`);
- the join with DHCP, DNS resolution of `example.com` and a ping to the host;
- UDP echo with the sender's port, and a receive that waits for its timeout;
- TCP echo, a refused connection, end of stream after the peer closes, and
  backpressure: sends to a host that never reads return at once with a partial
  count until the socket reports `HAL_EAGAIN`;
- 100 TCP echo round trips in `app_task0()` while `app_task1()` runs 100 UDP
  echo round trips on its own socket;
- TCP and UDP echo through the native ESP-IDF BSD API;
- WiFi disconnected while `app_task1()` blocks in a UDP and then a TCP
  receive: the receive ends within milliseconds with `HAL_ECANCELED`;
- invalidation of an open TCP socket by a disconnect, a new join and TCP echo
  after it;
- WiFi switched off with an open UDP socket, switched on again, a new join,
  DNS and UDP after it;
- the servers on the board's address, then a restart of all of them and a
  stop with the host's clients still connected.

The host probes the servers: HTTP GET, HEAD, POST, 404, a handler failure
(500) and a malformed request (400); files with an ETag and `304`, an upload
refused without the token header (403), an accepted upload read back, and a
`..` path; network commands over HTTP (valid, unknown, bad JSON) and over
WebSocket; WebSocket echo, an unmasked and an oversized frame (both end the
session) and a third session refused with 503; the net console with a wrong
password, a command, and a third client refused as busy; UDP and TCP echo
over the board's IPv6 link-local address; a client that never finishes its
request (408 while another client is served); idle connections that fill the
client pool until their timeout frees it. After the restart the host fetches
the page and exchanges a WebSocket message again; after the stop every held
connection ends and every port refuses new ones.

## Services suite (`SERVICES` variant)

The firmware checks, in this order:

- a TLS connection without a wall clock (`HAL_ECONFIG`) and a WireGuard
  handshake without one (no peer);
- NTP to a server that never answers (request fails, clock stays unset) and
  NTP whose primary name does not resolve, served by the secondary; libc time
  then follows the HAL clock;
- the TLS client: a valid session with an HTTP exchange, nine more sessions
  that return their heap, a wrong host name, an unrelated CA, an expired and a
  not-yet-valid certificate (`HAL_EAUTH`), an entropy failure (`HAL_EHW`),
  cancellation (`HAL_ECANCELED`), a peer that drops the handshake (`HAL_EIO`)
  and a peer that never answers (`HAL_ETIMEOUT`);
- the HTTPS and plain HTTP clients;
- MQTT, MQTTS twice, MQTTS to a wrong host name (rejected) and MQTT again after
  TLS is disabled;
- WireGuard split tunnel: handshake, UDP, TCP and MQTT through the tunnel, the
  LAN outside it, teardown with a TCP connection still open through the
  tunnel; three begin/end cycles; full tunnel with UDP and TCP; the endpoint
  given as a host name; WiFi lost under an active tunnel, a new join and a new
  tunnel;
- the unused stack of lwIP's thread after the tunnel tests.

The host serves NTP on UDP/10123, TLS and plain HTTP peers, a TLS peer that
drops the handshake and one that never answers, MQTT and MQTTS brokers, and on
the WireGuard address UDP echo, TCP echo and MQTT.

## Network settings

Create `net_test_config.h` next to `app.cpp`. Git ignores it, so the network
credentials stay on the machine:

```c
#pragma once
#define JH_NET_TEST_SSID "network-name"
#define JH_NET_TEST_PASSWORD "network-password"
#define JH_NET_TEST_HOST "192.168.1.10"  /* host running verify_network.py */
/* SERVICES suite only: the board's WireGuard key and the host's public key. */
#define JH_NET_TEST_WG_PRIVATE_KEY "board-private-key"
#define JH_NET_TEST_WG_PEER_PUBLIC_KEY "host-public-key"
```

The tunnel defaults are the board at `10.77.0.2`, the host at `10.77.0.1` and
the host endpoint on UDP/51821; `JH_NET_TEST_WG_LOCAL_IP`,
`JH_NET_TEST_WG_PEER_IP` and `JH_NET_TEST_WG_PORT` override them.

For the SERVICES suite, bring up a WireGuard interface on the host with
address `10.77.0.1/24`, listening on UDP/51821, with the board's public key
as its peer and `AllowedIPs = 10.77.0.2/32`. Creating the interface needs
root; the verifier then runs as an ordinary user. TLS uses a test CA:

```bash
python3 tests/hardware/esp32s3_network/verify_network.py prepare-tls \
  --host 192.168.1.10
```

It writes the CA, the server certificate for the public wildcard-DNS name
`192-168-1-10.sslip.io` (the board resolves it through its DNS) and an
unrelated CA to the ignored `.tls/` directory, and the firmware header
`net_test_tls.h`. Build after it; the certificates are valid for 30 days.

The firmware announces the host address and its ports; the verifier serves
exactly those. Port numbers live only in the suite sources.

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

python3 tests/hardware/esp32s3_network/verify_network.py run \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

For the services suite add `--variant SERVICES` to the build and flash
commands. The verifier starts its services after the firmware's `ready` line
and sends `GO`. In the basic suite it probes the servers when the firmware
reports `check=serve`, then sends `RESTART`, `STOP` and `DONE`. It succeeds
only when every check passed, none is missing, the board did not reset and
every host probe succeeded; it prints one JSON summary. `--raw-log FILE` also
writes every serial line, the panic output included.

`--soak-seconds N` adds a soak run after the suite: the basic suite runs TCP
echo and DNS in `app_task0()` and UDP echo in `app_task1()`, the services
suite runs UDP and TCP echo through a split tunnel (WireGuard renews its keys
every two minutes). The board prints progress every 30 seconds and passes
with at most 0.5 % failed exchanges and a decrease in free heap of no more
than 8 KiB from the start of the soak run.

## Hardware coverage

Both suites and their soak modes are hardware-validated on the Waveshare
ESP32-S3-Zero. Loss of the DHCP address without a disconnect and WireGuard
entropy failure still need hardware tests.
