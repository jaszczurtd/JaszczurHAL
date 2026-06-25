# BSD sockets TCP/UDP examples

This folder contains small C examples for the `HAL_ENABLE_BSD_SOCKETS`
compatibility layer. They intentionally keep the socket part close to common
Linux/Wikipedia-style examples: `socket()`, `bind()`, `listen()`, `accept()`,
`getaddrinfo()`, `connect()`, `send()`, `read()`/`recv()`, `sendto()`,
`recvfrom()` and `close()`.

The only embedded-specific pieces are WiFi startup and the JaszczurHAL
`app_start()` / `app_task0()` entry points.

## Targets

The examples are built as four separate RP2040/Pico W targets:

```bash
cmake -S examples -B build_examples_rp2040 -DJH_EXAMPLE_TARGET=rp2040
cmake --build build_examples_rp2040 --target 42_bsd_sockets_tcp_server_rp2040
cmake --build build_examples_rp2040 --target 42_bsd_sockets_tcp_client_rp2040
cmake --build build_examples_rp2040 --target 42_bsd_sockets_udp_server_rp2040
cmake --build build_examples_rp2040 --target 42_bsd_sockets_udp_client_rp2040
```

`examples/CMakeLists.txt` selects the WiFi-capable RP2040 FQBN
`JH_RP2040_WIFI_FQBN` for all four targets.

## Configuration

Edit `hal_project_config.h` before building or pass equivalent `-D` defines
through `EXTRA_HAL_DEFINES`:

```c
#define BSD_EXAMPLE_WIFI_SSID "your-ssid"
#define BSD_EXAMPLE_WIFI_PASSWORD "your-password"
#define BSD_EXAMPLE_SERVER_IP "192.168.1.50"
#define BSD_EXAMPLE_SERVER_HOST BSD_EXAMPLE_SERVER_IP
#define BSD_EXAMPLE_TCP_PORT 8080u
#define BSD_EXAMPLE_UDP_PORT 9000u
```

For client/server tests, flash the matching server example to one Pico W and
the matching client example to another Pico W. Set `BSD_EXAMPLE_SERVER_HOST`
in the client build to the server board's DNS name or serial-log IP address.

## MVP limits

- IPv4 only.
- DNS is available through minimal `getaddrinfo()`; clients still resolve to
  IPv4 endpoints.
- These examples intentionally use blocking `accept()`, `read()`/`recv()` and
  `recvfrom()`. The UDP client waits for a server reply after `sendto()`.
- The adapter also supports minimal `fcntl(F_SETFL, O_NONBLOCK)`,
  `MSG_DONTWAIT` and `select()` readiness; `poll()` is still outside this
  example stage.
- TLS is intentionally outside this example stage.
