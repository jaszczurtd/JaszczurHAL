# BSD sockets TCP/UDP examples

This folder contains small C examples for the `HAL_ENABLE_BSD_SOCKETS`
compatibility layer. They intentionally keep the socket part close to common
Linux/Wikipedia-style examples: `socket()`, `bind()`, `listen()`, `accept()`,
`getaddrinfo()`, `connect()`, `send()`, `read()`/`recv()`, `sendto()`,
`recvfrom()` and `close()`.

The only embedded-specific pieces are WiFi startup and the JaszczurHAL
`app_start()` / `app_task0()` entry points.

## Targets

The folder is a dispatcher-backed RP2040/Pico W example project. The default
`Project: Build` task builds the TCP server. The other socket programs are
available as example variants:

```bash
../../vscode/entry/jh-vscode build --project . --target rp2040
../../vscode/entry/jh-vscode build --project . --target rp2040 --variant tcp_client
../../vscode/entry/jh-vscode build --project . --target rp2040 --variant udp_server
../../vscode/entry/jh-vscode build --project . --target rp2040 --variant udp_client
```

The manifest defaults to `rp2040:picow`, so the registry supplies a WiFi-capable
native board profile for all four builds.

## Configuration

Edit `hal_project_config.h` before building:

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
