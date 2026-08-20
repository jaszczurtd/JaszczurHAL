# 18 - FreeRTOS suite

This consolidated example has two dispatcher profiles:

- the base `app.c` profile is the small cross-target FreeRTOS smoke test. It
  exercises both HAL and native FreeRTOS mutexes, two application tasks, two
  additional workers, delays, idle processing, GPIO, and shared state;
- the `network` variant builds `network_app.cpp` on WiFi-capable RP targets and
  STM32G474 with PIM730. It runs one coordinated HTTP/WebSocket/file/command
  service, a network console, a BSD TCP/UDP worker, an HTTP/HTTPS client
  worker while retaining the two application-task smoke paths. The profile also
  compiles the Telegram notification backend as an integration check, without
  embedding notification credentials or sending a runtime probe.

The network variant deliberately owns each singleton service only once. Its
HTTP server uses seven of the default eight routes: `/`, `/api/status`, the
HTTP command endpoint, and four RAM-file routes. WebSocket messages and console
lines share the `status` and `echo` command handlers.

## Network configuration

Override these compile definitions for a real network:

- `NETWORK_SUITE_WIFI_SSID` and `NETWORK_SUITE_WIFI_PASSWORD`;
- `NETWORK_SUITE_REMOTE_HOST`, `NETWORK_SUITE_BSD_TCP_PORT`, and
  `NETWORK_SUITE_BSD_UDP_PORT` for the periodic BSD client probes;
- `NETWORK_SUITE_HTTP_HOST` for the HTTP/HTTPS client worker;
- `NETWORK_SUITE_CONSOLE_PASSWORD` for the console on port 2323.

The firmware exposes HTTP on port 80, WebSocket at `ws://<ip>:81/ws`, a BSD
TCP echo server on port 8080, and a BSD UDP echo server on port 9000 by default.
The dispatcher sets bounded service pools (`4` TCP listeners, `6` TCP sockets,
listener backlog `2`, one TLS handle, and one client per
HTTP/WebSocket/console service). The network profile reserves 1536 FreeRTOS
stack words for the server/application task, 384 for the second application
task, 768 for the BSD worker, and 1536 for the HTTP/HTTPS worker. The base smoke
profile remains available on RP2350 RISC-V; the network profile is intentionally
limited to WiFi-capable RP2040/RP2350 Arm boards and STM32G474 with PIM730.

Verified HTTPS cannot be demonstrated safely without an application-selected
trust anchor. By default the worker performs HTTP and prints an explicit HTTPS
configuration diagnostic. To execute HTTPS too, define
`HTTP_EXAMPLE_CA_AVAILABLE` and add `ca_certificate.h` beside the source with:

```c
const unsigned char http_example_ca_der[] = { /* DER CA certificate */ };
const unsigned int http_example_ca_der_len = sizeof(http_example_ca_der);
```

The worker waits for NTP synchronization, converts that DER certificate to a
HAL trust anchor, and then performs the verified TLS request. TLS and the
Telegram backend are compiled into the normal network variant even when the
certificate is not supplied, so the gate still checks the full integration
surface. Runtime RAM on STM32G474 is
intentionally tight; validate the HTTPS-enabled configuration on the intended
board and inspect its link map before deployment.
