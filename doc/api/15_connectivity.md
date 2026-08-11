# Network connectivity

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: `hal_wifi`, `hal_udp`, `hal_tcp`, `hal_http_server`,
`hal_http_files`, `hal_websocket`, `hal_net_console`, `hal_net_commands`,
`hal_wireguard`, `hal_mqtt`, `hal_ota`, `hal_time`, and the optional
`HAL_ENABLE_BSD_SOCKETS` compatibility adapter.
Shared network types live in `hal_net.h`.

## Status-returning network API

New code can use additive `_ex` operations returning `hal_status_t` across
WiFi, resolver, TCP, UDP, MQTT and WireGuard. Existing APIs remain available
unchanged. Operations which historically returned a count, accepted socket or
peer state use an explicit output parameter, so converting to a status does not
discard the original result.

WiFi, resolver, TCP and UDP implement status handling directly in the mock and
RP-family backends. Their historical `bool`, count and handle APIs are adjacent
thin compatibility wrappers; they do not contain the real I/O path.

Representative entries include `hal_wifi_begin_station_ex()`,
`hal_wifi_ping_status_ex()`, `hal_net_resolve_ipv4_ex()`,
`hal_tcp_socket_{connect,send,recv}_ex()`,
`hal_tcp_listener_{bind,listen,accept}_ex()`,
`hal_udp_socket_{bind,sendto,recvfrom}_ex()`, legacy-packet UDP `_ex` helpers,
`hal_mqtt_{connect,publish,subscribe}_ex()` and
`hal_wireguard_{begin,peer_up,kick_handshake}_ex()`.

The complete additive status surface is:

```c
// WiFi and resolver
hal_status_t hal_wifi_set_mode_ex(hal_wifi_mode_t mode);
hal_status_t hal_wifi_disconnect_ex(bool erase_credentials);
hal_status_t hal_wifi_set_hostname_ex(const char *hostname);
hal_status_t hal_wifi_begin_station_ex(const char *ssid, const char *password,
                                       bool non_blocking);
hal_status_t hal_wifi_set_timeout_ms_ex(uint32_t timeout_ms);
hal_status_t hal_wifi_get_local_ip_ex(char *out, size_t out_size);
hal_status_t hal_wifi_get_dns_ip_ex(char *out, size_t out_size);
hal_status_t hal_wifi_get_mac_ex(char *out, size_t out_size);
hal_status_t hal_wifi_ping_status_ex(const char *host_or_ip,
                                     uint32_t timeout_ms, int *out_result);
hal_status_t hal_wifi_scan_networks_ex(int *out_count);
hal_status_t hal_wifi_get_scan_result_ex(size_t index,
                                         hal_wifi_scan_result_t *out);
hal_status_t hal_net_resolve_ipv4_ex(
    const char *host_or_ip, uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]);

// Handle-based TCP
hal_status_t hal_tcp_socket_open_ex(hal_tcp_socket_t *out_socket);
hal_status_t hal_tcp_socket_connect_ex(hal_tcp_socket_t socket,
                                       const hal_net_endpoint_t *remote,
                                       uint32_t timeout_ms);
hal_status_t hal_tcp_socket_send_ex(hal_tcp_socket_t socket, const void *data,
                                    size_t len, size_t *out_sent);
hal_status_t hal_tcp_socket_recv_ex(hal_tcp_socket_t socket, void *buffer,
                                    size_t max_len, uint32_t timeout_ms,
                                    size_t *out_received);
hal_status_t hal_tcp_listener_bind_ex(hal_tcp_listener_t listener,
                                      const hal_net_endpoint_t *local);
hal_status_t hal_tcp_listener_listen_ex(hal_tcp_listener_t listener,
                                        uint8_t backlog);
hal_status_t hal_tcp_listener_accept_ex(hal_tcp_listener_t listener,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        hal_tcp_socket_t *out_socket);
hal_status_t hal_tcp_listener_open_ex(hal_tcp_listener_t *out_listener);

// Handle-based and compatibility UDP
hal_status_t hal_udp_socket_open_ex(hal_udp_socket_t *out_socket);
hal_status_t hal_udp_socket_bind_ex(hal_udp_socket_t socket,
                                    const hal_net_endpoint_t *local);
hal_status_t hal_udp_socket_sendto_ex(hal_udp_socket_t socket, const void *data,
                                      size_t len,
                                      const hal_net_endpoint_t *remote,
                                      size_t *out_sent);
hal_status_t hal_udp_socket_recvfrom_ex(hal_udp_socket_t socket, void *buffer,
                                        size_t max_len,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        size_t *out_received);
hal_status_t hal_udp_begin_ex(uint16_t local_port);
hal_status_t hal_udp_parse_packet_ex(int *out_size);
hal_status_t hal_udp_read_ex(uint8_t *buffer, uint16_t max_len,
                             uint16_t *out_read);
hal_status_t hal_udp_remote_ip_ex(char *out, size_t out_size);
hal_status_t hal_udp_remote_port_ex(uint16_t *out_port);
hal_status_t hal_udp_begin_packet_ex(const char *host_or_ip,
                                     uint16_t remote_port);
hal_status_t hal_udp_begin_packet_remote_ex(void);
hal_status_t hal_udp_write_ex(const uint8_t *data, uint16_t len,
                              uint16_t *out_written);
hal_status_t hal_udp_write_str_ex(const char *text, uint16_t *out_written);
hal_status_t hal_udp_end_packet_ex(void);

// MQTT
hal_status_t hal_mqtt_set_server_ex(const char *host, uint16_t port);
hal_status_t hal_mqtt_set_callback_ex(hal_mqtt_message_callback_t callback,
                                      void *user);
hal_status_t hal_mqtt_set_keepalive_ex(uint16_t keepalive_s);
hal_status_t hal_mqtt_set_socket_timeout_ex(uint16_t timeout_s);
hal_status_t hal_mqtt_set_buffer_size_ex(uint16_t size);
hal_status_t hal_mqtt_connect_ex(const char *client_id);
hal_status_t hal_mqtt_connect_auth_ex(const char *client_id, const char *user,
                                      const char *pass);
hal_status_t hal_mqtt_loop_ex(void);
hal_status_t hal_mqtt_publish_ex(const char *topic, const uint8_t *payload,
                                 uint16_t payload_len, bool retained);
hal_status_t hal_mqtt_publish_str_ex(const char *topic, const char *payload,
                                     bool retained);
hal_status_t hal_mqtt_subscribe_ex(const char *topic, uint8_t qos);
hal_status_t hal_mqtt_unsubscribe_ex(const char *topic);

// WireGuard
hal_status_t hal_wireguard_begin_ex(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
    const char *private_key, const char *remote_peer_address,
    const char *remote_peer_public_key, uint16_t remote_peer_port);
hal_status_t hal_wireguard_begin_advanced_ex(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS], const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port,
    const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
    const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]);
hal_status_t hal_wireguard_peer_up_ex(char *endpoint_ip_out,
                                      size_t endpoint_ip_out_size,
                                      uint16_t *endpoint_port_out,
                                      bool *out_peer_up);
hal_status_t hal_wireguard_kick_handshake_ex(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms);
```

Common validation reports `HAL_EINVAL`. Resolver/lookup absence uses
`HAL_ENOENT`, an unavailable accept uses `HAL_EAGAIN`, pool exhaustion uses
`HAL_ENOMEM`, and an operation attempted in the wrong socket state uses
`HAL_ESTATE`. On RP CYW43 backends, public status operations consistently
report:

- `HAL_EUNSUPPORTED` when the selected board profile does not declare all
  required radio hardware;
- `HAL_EUNINIT` when that hardware is declared but has not initialized
  successfully;
- `HAL_EHW` after a probe or initialization has marked the hardware failed.

The initialization call itself returns its original driver status; later
network access reports the sticky `HAL_EHW` state. Pico+PIM730 requires both
the CYW43 and external-radio-frontend capabilities. Failed preflight does not
touch the backend or radio pins. Numeric IPv4 parsing, WireGuard address
parsing, and configuration-only MQTT setters remain usable offline. Native
transport failures that do not change the board hardware state use `HAL_EIO`.
See the public module headers for complete signatures.

## Shared network types

`hal_net.h` contains plain C value types shared by handle-based UDP, TCP, and
BSD/POSIX compatibility layers. Endpoints have family-tagged storage for IPv4
and IPv6. The current CYW43 backends advertise IPv4; unsupported families
return `HAL_EUNSUPPORTED`.

```c
#include <hal/network/hal_net.h>

#define HAL_NET_IPV4_ADDR_LEN 4u
#define HAL_NET_IPV6_ADDR_LEN 16u
#define HAL_NET_MAX_ADDR_LEN HAL_NET_IPV6_ADDR_LEN
#define HAL_NET_TIMEOUT_FOREVER UINT32_MAX

typedef enum {
  HAL_NET_AF_UNSPEC = 0,
  HAL_NET_AF_INET = 2,
  HAL_NET_AF_INET6 = 10
} hal_net_family_t;

typedef struct {
  hal_net_family_t family;
  uint8_t addr[HAL_NET_MAX_ADDR_LEN];
  uint8_t addr_len;
  uint16_t port;
  uint32_t scope_id;
} hal_net_endpoint_t;

#define HAL_NET_CAP_IPV4      (1u << 0u)
#define HAL_NET_CAP_IPV6      (1u << 1u)
#define HAL_NET_CAP_DUAL_STACK (1u << 2u)

#ifdef HAL_ENABLE_WIFI
hal_status_t hal_net_get_capabilities_ex(
    hal_net_capabilities_t *out_capabilities);
hal_status_t hal_net_service(void);
hal_status_t hal_net_resolve_ex(const char *host_or_ip,
                                hal_net_family_t family_hint,
                                hal_net_endpoint_t *results,
                                size_t capacity,
                                size_t *out_count);
bool hal_net_resolve_ipv4(const char *host_or_ip,
                          uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]);
hal_status_t hal_net_resolve_ipv4_ex(
    const char *host_or_ip, uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]);
#endif
```

Address bytes are stored in network byte order. `addr_len` is four for IPv4
and sixteen for IPv6; `scope_id` carries an IPv6 interface scope. The `port`
field is in host byte order; POSIX adapters perform their own `htons()` /
`ntohs()` translation at the API boundary.

**Resolver notes:**
- `hal_net_resolve_ex(...)` accepts numeric addresses or hostnames, a family
  hint, and a bounded caller-owned result array. `HAL_EOVERFLOW` reports the
  required result count without writing a partial result.
- `hal_net_resolve_ipv4(...)` accepts a dotted IPv4 literal or hostname and
  writes four IPv4 octets. The caller keeps the transport port separately.
- The mock backend resolves IPv4 literals, `localhost`, and test entries added
  with `hal_mock_net_set_dns_entry(...)`.
- CYW43 backends resolve numeric literals locally and use their owned lwIP
  resolver for hostnames. Hostname resolution requires initialized hardware;
  literal parsing does not.

**Mock resolver helpers:**
```c
void hal_mock_net_reset(void);
bool hal_mock_net_set_dns_entry(const char *host, const char *ip);
```

## `hal_wifi` - WiFi  *(optional - `HAL_ENABLE_WIFI`)*

RP builds that need WiFi select a radio-capable profile: `picow`, `pico2w`, or
`pico-rm2`. `HAL_ENABLE_WIFI` selects the facade; dependent modules such as
MQTT and WireGuard propagate it.
Network modules may also be compiled for a plain Pico profile; public calls
then return `HAL_EUNSUPPORTED` without accessing CYW43 pins. On a capable
profile, `hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA)` and
`hal_wifi_begin_station_ex(...)` are the explicit initialization entry points.
State queries, scans and transport opens do not initialize the radio
implicitly.

CYW43 profiles keep the radio's factory MAC address when it is present in the
module OTP. This includes Raspberry Pi-assigned addresses such as
`28:CD:C1:xx:xx:xx`. If the radio reports that its OTP MAC is unset, JaszczurHAL
uses the Pico SDK fallback convention: a locally administered unicast address
derived from the least-significant six bytes of the board UID. The fallback is
stable for a board and does not reuse the common UID prefix shared by multiple
RP boards. `hal_wifi_get_mac_ex()` and the lwIP interface both use the address
stored in the CYW43 controller state after initialization; applications must
not substitute a separately generated board-UID address when OTP is present.

For RP CYW43 profiles, `hal_wifi_begin_station_ex(..., true)` starts the join
request and returns once CYW43 accepts it. The caller may release or overwrite
the SSID and password buffers after the call returns. Service the connection by
polling `hal_wifi_get_state_ex()` or `hal_wifi_is_connected()`; those calls
advance the polling backend and expose connecting, no-network, authentication,
DHCP and connected states. Passing `false` retains the bounded blocking join
that waits for a DHCP lease.

STM32G474 supports an external CYW43/PIM730 through the experimental
`nucleo-g474re-pim730` profile, its one-wire gSPI transport, and the same pinned
lwIP stack. The plain `nucleo-g474re` profile deliberately has no radio
capability and rejects a CYW43 network build. Station join on STM32G474 is
bounded and blocking; requesting the non-blocking form returns
`HAL_EUNSUPPORTED`.

```c
#include <hal/network/hal_wifi.h>

typedef enum {
    HAL_WIFI_MODE_OFF    = 0,
    HAL_WIFI_MODE_STA    = 1,
    HAL_WIFI_MODE_AP     = 2,
    HAL_WIFI_MODE_AP_STA = 3,
} hal_wifi_mode_t;

typedef enum {
    HAL_WIFI_ENC_UNKNOWN = 0,
    HAL_WIFI_ENC_NONE,
    HAL_WIFI_ENC_WPA,
    HAL_WIFI_ENC_WPA2,
    HAL_WIFI_ENC_AUTO,
} hal_wifi_encryption_t;

typedef struct {
    char                  ssid[HAL_WIFI_SSID_MAX_LEN];
    uint8_t               bssid[HAL_WIFI_BSSID_LEN];
    hal_wifi_encryption_t encryption;
    int32_t               rssi;
    int32_t               channel;
} hal_wifi_scan_result_t;

bool    hal_wifi_set_mode(hal_wifi_mode_t mode);
bool    hal_wifi_disconnect(bool erase_credentials);
bool    hal_wifi_set_hostname(const char *hostname);
bool    hal_wifi_begin_station(const char *ssid, const char *password, bool non_blocking);
bool    hal_wifi_set_timeout_ms(uint32_t timeout_ms);
bool    hal_wifi_is_connected(void);
int     hal_wifi_status(void);
bool    hal_wifi_has_local_ip(void);
int32_t hal_wifi_rssi(void);                        // dBm
int     hal_wifi_get_strength(void);                // 0..5 bars
bool    hal_wifi_get_local_ip(char *out, size_t out_size);
bool    hal_wifi_get_dns_ip(char *out, size_t out_size);
bool    hal_wifi_get_mac(char *out, size_t out_size);
hal_status_t hal_wifi_set_mode_ex(hal_wifi_mode_t mode);
hal_status_t hal_wifi_disconnect_ex(bool erase_credentials);
hal_status_t hal_wifi_set_hostname_ex(const char *hostname);
hal_status_t hal_wifi_begin_station_ex(const char *ssid, const char *password,
                                       bool non_blocking);
hal_status_t hal_wifi_set_timeout_ms_ex(uint32_t timeout_ms);
hal_status_t hal_wifi_get_local_ip_ex(char *out, size_t out_size);
hal_status_t hal_wifi_get_dns_ip_ex(char *out, size_t out_size);
hal_status_t hal_wifi_get_mac_ex(char *out, size_t out_size);
int     hal_wifi_ping(const char *host_or_ip);      // >=0 ok, <0 error (uses timeout set by hal_wifi_set_timeout_ms)
int     hal_wifi_ping_ex(const char *host_or_ip, uint32_t timeout_ms); // >=0 ok, <0 error (per-call timeout)
hal_status_t hal_wifi_ping_status_ex(const char *host_or_ip,
                                     uint32_t timeout_ms, int *out_result);
int     hal_wifi_scan_networks(void);               // >=0 result count, <0 error
bool    hal_wifi_get_scan_result(size_t index, hal_wifi_scan_result_t *out);
hal_status_t hal_wifi_scan_networks_ex(int *out_count);
hal_status_t hal_wifi_get_scan_result_ex(size_t index,
                                         hal_wifi_scan_result_t *out);
const char *hal_wifi_encryption_to_string(hal_wifi_encryption_t encryption);
```

**impl/rp2040:** JaszczurHAL-owned CYW43 driver and lwIP stack over PIO/gSPI.
**impl/stm32g474:** the same CYW43/lwIP owner over the STM32G474 one-wire gSPI
transport.
**impl/.mock:** state injection via mock helpers.
**Thread safety:** The RP and STM32G474 hardware backends serialize public HAL
wrapper calls. Internal singleton mutexes protect provider state, network
service progress, and CYW43/lwIP stack access in bare-metal and FreeRTOS
builds. The mock backend is a deterministic state-injection test double for
single-threaded tests.

**Mock helpers:**
```c
void        hal_mock_wifi_reset(void);
void        hal_mock_wifi_set_connected(bool connected);
void        hal_mock_wifi_set_status(int status);
void        hal_mock_wifi_set_rssi(int32_t rssi);
void        hal_mock_wifi_set_local_ip(const char *ip);
void        hal_mock_wifi_set_dns_ip(const char *ip);
void        hal_mock_wifi_set_mac(const char *mac);
void        hal_mock_wifi_set_ping_result(int result);
const char *hal_mock_wifi_get_hostname(void);
uint32_t    hal_mock_wifi_get_timeout_ms(void);
bool        hal_mock_wifi_set_scan_result(size_t index,
                                          const char *ssid,
                                          hal_wifi_encryption_t encryption,
                                          const uint8_t bssid[HAL_WIFI_BSSID_LEN],
                                          int32_t channel,
                                          int32_t rssi);
```

### CYW43 backend configuration and lifecycle

All hardware CYW43 builds select one facade backend, one bus, and the pinned
lwIP stack:

```c
#define HAL_NETWORK_BACKEND_CYW43
#define HAL_CYW43_STACK_LWIP
```

RP board profiles emit `HAL_CYW43_BUS_PICO_PIO` and the matching pins. Pico W,
Pico 2 W, and Pico+PIM730 use the same provider lifecycle. The PIO transport
derives its 16.8 clock divider from the live `clk_sys` and
`HAL_CYW43_GSPI_TARGET_HZ` (31.25 MHz by default), selecting the matching
high/low-speed sampling program without exceeding the target. Changing
`clk_sys` while the provider is active is rejected; deinitialize the network,
change the clock, and initialize it again.

STM32G474 projects with an external PIM730 select board profile
`nucleo-g474re-pim730`. The generated profile supplies the backend, bus, stack,
and encoded pins; applications must not reproduce these definitions manually.
The fixed wiring is:

| PIM730 | STM32G474 | Nucleo connector |
|---|---|---|
| `CS` | `PB12` | CN10 pin 16 |
| `DAT` | `PB15` | CN10 pin 26 |
| `WL_ON` | `PB14` | CN10 pin 28 |
| `CLK` | `PB13` | CN10 pin 30 |
| `GND` | GND | CN10 pin 20 |
| `3V3` | 3.3 V | CN7 pin 16 |

PIM730 is a 3.0-3.3 V device; never connect it to 5 V. `DAT` is the combined
data input/output and host-wake line. Use short, direct wiring. The supported
profile assumes that the PIM730 cuttable `BT_ON`-to-`WL_ON` trace is intact, so
`BT_ON`/`BL_ON` remains otherwise unconnected. Inspect that trace before using
the profile; a cut trace is a different hardware topology and is not currently
described by a board profile.

The equivalent generated configuration is:

```c
#define HAL_NETWORK_BACKEND_CYW43
#define HAL_CYW43_BUS_STM32_GSPI
#define HAL_CYW43_STACK_LWIP
#define HAL_CYW43_PIN_WL_ON       /* encoded STM32 GPIO */
#define HAL_CYW43_PIN_CHIP_SELECT /* encoded STM32 GPIO */
#define HAL_CYW43_PIN_DATA        /* shared DAT/host-wake GPIO */
#define HAL_CYW43_PIN_CLOCK       /* encoded STM32 GPIO */
#define HAL_CYW43_MAX_TRANSACTION_BYTES 2048u
```

The four pins must be distinct, valid STM32G474 GPIOs. Transaction capacity is
at least eight bytes and a multiple of four. The polling gSPI data path is
optimized for the HSI16 baseline. DAT changes direction between transmit and
receive. The host-wake edge is handled by a high-priority one-shot EXTI: the
ISR masks the line and schedules work, and the service path rearms it after
draining CYW43/lwIP work.

Both target backends own CYW43 power-up, firmware download, lwIP netif, DHCP,
DNS, ICMP echo, raw UDP/TCP, scans, and teardown. `hal_net_service()` performs
one bounded service pass. RP bare-metal and STM32G474 use the poll execution
model; FreeRTOS callers still use the same serialized stack context. Init,
socket/listener pools, and deinit are protected separately so teardown closes
all facade handles before stopping lwIP, the radio, and the bus.

Network memory is bounded by compile-time pools. The main controls are
`HAL_TCP_SOCKET_MAX_INSTANCES` (default 4),
`HAL_TCP_LISTENER_MAX_INSTANCES` (default 2),
`HAL_UDP_SOCKET_MAX_INSTANCES` (default 4),
`HAL_LWIP_TCP_RX_LIMIT` (default 16 KiB per TCP engine),
`HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH` (default 5), and
`HAL_LWIP_UDP_RX_QUEUE_DEPTH` (default 4). Size these together with
`HAL_CYW43_MAX_TRANSACTION_BYTES` and the selected lwIP configuration for the
target's SRAM budget.

---

## `hal_http_client` - HTTP/HTTPS client  *(opt-in - `HAL_ENABLE_HTTP_CLIENT`)*

`hal_http_client` performs one bounded HTTP/1.1 request over HAL TCP or the
verified BearSSL TLS client. The flag enables TCP and WiFi. Select
`HAL_ENABLE_TLS` as well for HTTPS.

```c
#include <hal/network/http/hal_http_client.h>

hal_http_client_request_t request;
hal_http_client_request_init(&request);
request.transport = HAL_HTTP_CLIENT_TRANSPORT_PLAINTEXT;
request.host = "example.com";
request.port = 80u;
request.method = "GET";
request.path = "/";
request.timeout_ms = 15000u;

uint8_t body[512];
hal_http_client_response_t response;
hal_status_t status =
    hal_http_client_perform_ex(&request, body, sizeof(body), &response);
```

The request can reference caller-owned headers and an optional body. Input
validation rejects empty hosts, invalid methods, non-absolute paths, CR/LF
injection, zero ports/timeouts, and inconsistent pointer/count pairs.
`hal_http_client_request_init()` selects plaintext GET `/`, port 80, and a
15-second deadline.

For HTTPS, set `transport` to `HAL_HTTP_CLIENT_TRANSPORT_TLS`, normally use
port 443, and point `tls_security` at a configured trust store, time callback,
and entropy callback. The one-shot call creates a bounded-worker TLS client,
uses the request timeout for connect/read/write/shutdown, verifies the request
hostname through BearSSL, and closes the transport before returning. The trust
anchor and callback storage must remain valid for the entire call.

The client sends `Connection: close`, parses HTTP/1.0 or HTTP/1.1 status lines,
recognizes `Content-Length`, and reads the body until the declared length or
connection close. Response bodies are copied without a terminator.
`HAL_EOVERFLOW` reports the required body length when the caller buffer is too
small. Chunked transfer encoding returns `HAL_EUNSUPPORTED`.

**Implementation:** `hal/network/http/hal_http_client.cpp`.
**Tests:** `test_hal_http_client` covers validation, fragmented response
headers, response metadata, and bounded body copies.
`test_hal_http_client_plaintext_compile` keeps the plaintext-only flag
combination buildable. The verified HTTP/HTTPS client path is part of
[`examples/18_freertos_suite`](../../examples/18_freertos_suite/README.md).

---

## `hal_http_server` - HTTP/1.1 server  *(opt-in - `HAL_ENABLE_HTTP_SERVER`)*

Small poll-driven HTTP server implemented over the handle-based `hal_tcp`
listener/socket API. Enabling `HAL_ENABLE_HTTP_SERVER` propagates
`HAL_ENABLE_TCP`, which in turn propagates `HAL_ENABLE_WIFI` on current
network-capable builds.

The first version is intentionally compact and deterministic:

- exact method/path route matching,
- one request per TCP connection,
- `GET`, `HEAD`, `POST`, `PUT`, `DELETE` and `OPTIONS` method parsing,
- query string, request headers and request body exposure to the handler,
- buffered response body with automatic `Content-Length`,
- exact and prefix route registration,
- explicit status, content-type and response-header helpers,
- cooperative `hal_http_server_poll()` service loop.

```c
#include <hal/network/http/hal_http_server.h>

typedef enum {
  HAL_HTTP_METHOD_UNKNOWN = 0,
  HAL_HTTP_METHOD_GET,
  HAL_HTTP_METHOD_HEAD,
  HAL_HTTP_METHOD_POST,
  HAL_HTTP_METHOD_PUT,
  HAL_HTTP_METHOD_DELETE,
  HAL_HTTP_METHOD_OPTIONS
} hal_http_method_t;

typedef struct {
  const char *name;
  const char *value;
} hal_http_header_t;

typedef struct {
  hal_http_method_t method;
  const char *path;
  const char *query;
  const char *body;
  size_t body_len;
  const hal_http_header_t *headers;
  size_t header_count;
  hal_net_endpoint_t remote;
} hal_http_request_t;

typedef hal_status_t (*hal_http_handler_t)(const hal_http_request_t *request,
                                           hal_http_response_t *response,
                                           void *user);

hal_status_t hal_http_server_route(hal_http_method_t method, const char *path,
                                   hal_http_handler_t handler, void *user);
hal_status_t hal_http_server_route_prefix(hal_http_method_t method,
                                          const char *path_prefix,
                                          hal_http_handler_t handler,
                                          void *user);
void hal_http_server_clear_routes(void);
hal_status_t hal_http_server_start(uint16_t port);
void hal_http_server_stop(void);
bool hal_http_server_is_running(void);
void hal_http_server_poll(void);

hal_status_t hal_http_response_set_status(hal_http_response_t *response,
                                          uint16_t status_code,
                                          const char *reason);
hal_status_t hal_http_response_set_content_type(
    hal_http_response_t *response,
    const char *content_type);
hal_status_t hal_http_response_set_header(hal_http_response_t *response,
                                          const char *name,
                                          const char *value);
hal_status_t hal_http_response_write(hal_http_response_t *response,
                                     const void *data,
                                     size_t len);
hal_status_t hal_http_response_write_str(hal_http_response_t *response,
                                         const char *text);
const char *hal_http_request_get_header(const hal_http_request_t *request,
                                        const char *name);
const char *hal_http_method_to_string(hal_http_method_t method);
```

Example handler:

```c
static hal_status_t status_route(const hal_http_request_t *request,
                                 hal_http_response_t *response,
                                 void *user) {
  (void)user;
  const char *content_type =
      hal_http_request_get_header(request, "Content-Type");
  (void)content_type;
  hal_status_t status =
      hal_http_response_set_content_type(response, "application/json");
  if (status != HAL_OK) {
    return status;
  }
  return hal_http_response_write_str(response, "{\"ok\":true}");
}
```

Start and service from the application loop:

```c
hal_http_server_route(HAL_HTTP_METHOD_GET, "/api/status", status_route, NULL);
hal_http_server_start(80);

for (;;) {
  hal_http_server_poll();
}
```

Default static limits can be overridden before including HAL headers:

```c
#define HAL_HTTP_SERVER_MAX_ROUTES 8u
#define HAL_HTTP_SERVER_MAX_CLIENTS 2u
#define HAL_HTTP_SERVER_REQUEST_BUFFER_SIZE 512u
#define HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE 1024u
#define HAL_HTTP_SERVER_MAX_REQUEST_HEADERS 12u
#define HAL_HTTP_SERVER_MAX_RESPONSE_HEADERS 8u
#define HAL_HTTP_SERVER_RESPONSE_HEADER_SIZE 512u
#define HAL_HTTP_SERVER_DEFAULT_BACKLOG 2u
```

**shared thematic implementation:** `hal/network/http/hal_http_server.cpp`.
**impl/.mock:** covered through the mock TCP listener/socket backend and
`test_hal_http_server`.

---

## `hal_http_files` - file serving and upload  *(opt-in - `HAL_ENABLE_HTTP_FILES`)*

Small file adapter built on top of `hal_http_server`. Enabling
`HAL_ENABLE_HTTP_FILES` also enables `HAL_ENABLE_HTTP_SERVER`, `HAL_ENABLE_TCP`
and `HAL_ENABLE_WIFI`.

The adapter is filesystem-neutral. It maps HTTP URLs to a mounted root and
calls application/backend callbacks for `stat`, `read` and optional `write`.
That keeps the HTTP layer reusable for RAM assets, LittleFS, FatFs/SD, flash
assets or tests.

Supported behavior:

- `GET` / `HEAD` file serving through prefix routes,
- MIME type selection from extension,
- generated weak ETags from path, size and mtime,
- `If-None-Match` -> `304 Not Modified`,
- raw `PUT` upload to a path under the mounted prefix,
- multipart/form-data `POST` upload with `path` and `file` fields,
- path traversal rejection for `..` and backslashes.

```c
#include <hal/network/http/hal_http_files.h>

typedef struct {
  bool exists;
  bool is_dir;
  size_t size;
  uint32_t mtime;
  const char *content_type;
  const char *etag;
} hal_http_file_info_t;

typedef hal_status_t (*hal_http_file_stat_cb_t)(
    const char *path,
    hal_http_file_info_t *out_info,
    void *user);

typedef hal_status_t (*hal_http_file_read_cb_t)(
    const char *path,
    size_t offset,
    void *buffer,
    size_t max_len,
    size_t *out_len,
    void *user);

typedef hal_status_t (*hal_http_file_write_cb_t)(
    const char *path,
    size_t offset,
    const void *data,
    size_t len,
    bool final,
    void *user);

typedef enum {
  HAL_HTTP_FILE_UPLOAD_RAW = 0,
  HAL_HTTP_FILE_UPLOAD_MULTIPART
} hal_http_file_upload_t;

typedef hal_status_t (*hal_http_file_authorize_cb_t)(
    const hal_http_request_t *request,
    hal_http_file_upload_t upload,
    void *user);

typedef struct {
  const char *url_prefix;
  const char *fs_root;
  const char *index_name;
  const char *upload_path;
  bool enable_upload;
  hal_http_file_stat_cb_t stat;
  hal_http_file_read_cb_t read;
  hal_http_file_write_cb_t write;
  hal_http_file_authorize_cb_t authorize_upload;
  void *user;
} hal_http_files_config_t;

hal_status_t hal_http_files_mount(const hal_http_files_config_t *config);
void hal_http_files_clear(void);
const char *hal_http_files_content_type_for_path(const char *path);
hal_status_t hal_http_files_make_etag(const char *path,
                                      const hal_http_file_info_t *info,
                                      char *out,
                                      size_t out_size);
```

Basic flow:

```c
hal_http_files_config_t cfg = {0};
cfg.url_prefix = "/files";
cfg.fs_root = "/www";
cfg.upload_path = "/upload";
cfg.enable_upload = true;
cfg.stat = my_stat;
cfg.read = my_read;
cfg.write = my_write;
cfg.authorize_upload = my_authorize_upload;

hal_http_files_mount(&cfg);
hal_http_server_start(80);

for (;;) {
  hal_http_server_poll();
}
```

Uploads are fail-closed: `enable_upload = true` requires both `write` and
`authorize_upload`. The authorization callback runs before multipart parsing or
filesystem writes and must return `HAL_OK`; every other status produces HTTP
403. Use TLS when credentials cross an untrusted network.

Multipart upload example:

```http
POST /upload HTTP/1.1
Content-Type: multipart/form-data; boundary=AaB03x

--AaB03x
Content-Disposition: form-data; name="path"

logs
--AaB03x
Content-Disposition: form-data; name="file"; filename="boot.txt"
Content-Type: text/plain

hello
--AaB03x--
```

With the config above, the file callback receives path
`/www/logs/boot.txt`.

The current `hal_http_server` buffers each request and response in fixed-size
static buffers, so this adapter is intended for small embedded files,
configuration uploads and diagnostics rather than large streaming transfers.

Default static limits can be overridden before including HAL headers:

```c
#define HAL_HTTP_FILES_MAX_MOUNTS 2u
#define HAL_HTTP_FILES_PATH_MAX 128u
#define HAL_HTTP_FILES_ETAG_MAX 48u
#define HAL_HTTP_FILES_IO_BUFFER_SIZE 128u
```

**shared thematic implementation:** `hal/network/http/hal_http_files.cpp`.
**impl/.mock:** covered through mock HTTP/TCP and `test_hal_http_files`.

---

## `hal_websocket` - WebSocket server  *(opt-in - `HAL_ENABLE_WEBSOCKET`)*

Small poll-driven WebSocket server implemented directly over `hal_tcp`.
Enabling `HAL_ENABLE_WEBSOCKET` propagates `HAL_ENABLE_TCP`, which in turn
propagates `HAL_ENABLE_WIFI` on current connected builds.

The server accepts TCP clients, performs the HTTP Upgrade handshake for one
configured path, then switches each accepted socket into WebSocket frame
parsing. The first implementation is intentionally compact:

- RFC 6455 `Sec-WebSocket-Accept` handshake,
- masked client frames and unmasked server frames,
- single-frame text/binary messages,
- automatic pong response to ping,
- close frame handling with disconnect callback,
- per-client send helpers and broadcast helpers,
- cooperative `hal_websocket_server_poll()` service loop.

It does not implement fragmented messages, permessage-deflate, TLS, cookies or
subprotocol negotiation. Put authentication or session policy in the
application protocol or in the HTTP page that opens the socket.

```c
#include <hal/network/websocket/hal_websocket.h>

typedef uint8_t hal_websocket_client_t;

typedef enum {
  HAL_WEBSOCKET_MESSAGE_TEXT = 1,
  HAL_WEBSOCKET_MESSAGE_BINARY = 2
} hal_websocket_message_type_t;

typedef struct {
  void (*on_connect)(hal_websocket_client_t client, void *user);
  void (*on_message)(hal_websocket_client_t client,
                     hal_websocket_message_type_t type,
                     const uint8_t *data,
                     size_t len,
                     void *user);
  void (*on_disconnect)(hal_websocket_client_t client,
                        uint16_t close_code,
                        void *user);
} hal_websocket_callbacks_t;

hal_status_t hal_websocket_server_set_callbacks(
    const hal_websocket_callbacks_t *callbacks,
    void *user);
hal_status_t hal_websocket_server_start(uint16_t port, const char *path);
void hal_websocket_server_stop(void);
bool hal_websocket_server_is_running(void);
void hal_websocket_server_poll(void);

size_t hal_websocket_client_count(void);
bool hal_websocket_client_is_connected(hal_websocket_client_t client);

hal_status_t hal_websocket_send(hal_websocket_client_t client,
                                hal_websocket_message_type_t type,
                                const void *data,
                                size_t len);
hal_status_t hal_websocket_send_text(hal_websocket_client_t client,
                                     const char *text);
hal_status_t hal_websocket_broadcast(hal_websocket_message_type_t type,
                                     const void *data,
                                     size_t len,
                                     size_t *sent_count);
hal_status_t hal_websocket_broadcast_text(const char *text,
                                          size_t *sent_count);
hal_status_t hal_websocket_close(hal_websocket_client_t client,
                                 uint16_t close_code);
```

Minimal callback setup:

```c
static void ws_message(hal_websocket_client_t client,
                       hal_websocket_message_type_t type,
                       const uint8_t *data,
                       size_t len,
                       void *user) {
  (void)type;
  (void)user;
  hal_websocket_send(client, HAL_WEBSOCKET_MESSAGE_TEXT, data, len);
}

hal_websocket_callbacks_t cb = {0};
cb.on_message = ws_message;
hal_websocket_server_set_callbacks(&cb, NULL);
hal_websocket_server_start(81, "/ws");

for (;;) {
  hal_websocket_server_poll();
}
```

Broadcasting telemetry:

```c
char msg[64];
size_t sent_count = 0u;
snprintf(msg, sizeof(msg), "uptime=%lu", (unsigned long)hal_millis());
hal_websocket_broadcast_text(msg, &sent_count);
```

Default static limits can be overridden before including HAL headers:

```c
#define HAL_WEBSOCKET_MAX_CLIENTS 2u
#define HAL_WEBSOCKET_REQUEST_BUFFER_SIZE 512u
#define HAL_WEBSOCKET_FRAME_BUFFER_SIZE 256u
#define HAL_WEBSOCKET_DEFAULT_BACKLOG 2u
```

**shared thematic implementation:** `hal/network/websocket/hal_websocket.cpp`.
**impl/.mock:** covered through the mock TCP listener/socket backend and
`test_hal_websocket`.

---

## `hal_net_console` - TCP debug console  *(opt-in - `HAL_ENABLE_NET_CONSOLE`)*

Password-protected TCP console implemented over the handle-based `hal_tcp`
listener/socket API. Enabling `HAL_ENABLE_NET_CONSOLE` propagates
`HAL_ENABLE_TCP`, which in turn propagates `HAL_ENABLE_WIFI` on connected
builds.

The console is a transport, not a replacement for the normal debug port:
`hal_serial`, `deb` and `derr` still write to UART/USB, and authenticated TCP
clients receive an additional copy. TCP input is available to firmware through
a line callback and a polling RX buffer, so applications can expose a small
command shell or diagnostics interface.

Security model: a non-empty password is required by the API, but the transport
is plain TCP. Use it only on trusted networks or behind a secure tunnel/VPN
when remote access matters.

```c
#include <hal/network/net_console/hal_net_console.h>

#define HAL_NET_CONSOLE_DEFAULT_PORT 2323u

typedef uint8_t hal_net_console_client_t;

typedef enum {
  HAL_NET_CONSOLE_EVENT_CONNECT = 0,
  HAL_NET_CONSOLE_EVENT_AUTHENTICATED,
  HAL_NET_CONSOLE_EVENT_DISCONNECT
} hal_net_console_event_t;

typedef void (*hal_net_console_event_cb_t)(hal_net_console_client_t client,
                                           hal_net_console_event_t event,
                                           void *user);
typedef hal_status_t (*hal_net_console_line_cb_t)(
    hal_net_console_client_t client,
    const char *line,
    void *user);

hal_status_t hal_net_console_set_callbacks(hal_net_console_event_cb_t event_cb,
                                           hal_net_console_line_cb_t line_cb,
                                           void *user);
hal_status_t hal_net_console_start(uint16_t port, const char *password);
void hal_net_console_stop(void);
bool hal_net_console_is_running(void);
void hal_net_console_poll(void);

size_t hal_net_console_client_count(void);
size_t hal_net_console_authenticated_count(void);
bool hal_net_console_client_is_authenticated(hal_net_console_client_t client);

hal_status_t hal_net_console_write(const void *data, size_t len);
hal_status_t hal_net_console_write_text(const char *text);
hal_status_t hal_net_console_write_to(hal_net_console_client_t client,
                                      const void *data,
                                      size_t len);
hal_status_t hal_net_console_write_text_to(hal_net_console_client_t client,
                                           const char *text);

int hal_net_console_available(void);
int hal_net_console_read(void *buffer, size_t max_len);
void hal_net_console_close(hal_net_console_client_t client);
```

Minimal command callback:

```c
static hal_status_t console_line(hal_net_console_client_t client,
                                 const char *line,
                                 void *user) {
  (void)user;
  if (strcmp(line, "status") == 0) {
    return hal_net_console_write_text_to(client, "ok\r\n");
  }
  return hal_net_console_write_text_to(client, "unknown\r\n");
}

hal_net_console_set_callbacks(NULL, console_line, NULL);
hal_net_console_start(HAL_NET_CONSOLE_DEFAULT_PORT, "change-me");

for (;;) {
  hal_net_console_poll();
}
```

Default static limits can be overridden before including HAL headers:

```c
#define HAL_NET_CONSOLE_MAX_CLIENTS 2u
#define HAL_NET_CONSOLE_RX_BUFFER_SIZE 256u
#define HAL_NET_CONSOLE_TX_BUFFER_SIZE 1024u
#define HAL_NET_CONSOLE_LINE_BUFFER_SIZE 128u
#define HAL_NET_CONSOLE_PASSWORD_MAX 64u
#define HAL_NET_CONSOLE_DEFAULT_BACKLOG 2u
```

**shared thematic implementation:** `hal/network/net_console/hal_net_console.cpp`.
**impl/.mock:** covered through the mock TCP listener/socket backend and
`test_hal_net_console`.

---

## `hal_net_commands` - HTTP/WebSocket command layer  *(opt-in - `HAL_ENABLE_NET_COMMANDS`)*

Small command registry inspired by embedded WebUI control channels. Enabling
`HAL_ENABLE_NET_COMMANDS` also enables `HAL_ENABLE_HTTP_SERVER`,
`HAL_ENABLE_WEBSOCKET`, `HAL_ENABLE_CJSON`, `HAL_ENABLE_TCP` and
`HAL_ENABLE_WIFI`.

Requests can be plain text:

```text
status
echo hello
```

or JSON parsed by cJSON:

```json
{"cmd":"status","args":{"verbose":true}}
```

`cmd` and `command` are accepted as command-name fields. `args` and `params`
are exposed to handlers as `json_args`; string args are also mirrored through
`args_text`.

```c
#include <hal/network/net_commands/hal_net_commands.h>

typedef enum {
  HAL_NET_COMMANDS_FORMAT_TEXT = 0,
  HAL_NET_COMMANDS_FORMAT_JSON,
  HAL_NET_COMMANDS_FORMAT_AUTO
} hal_net_commands_format_t;

typedef enum {
  HAL_NET_COMMANDS_SOURCE_DIRECT = 0,
  HAL_NET_COMMANDS_SOURCE_HTTP,
  HAL_NET_COMMANDS_SOURCE_WEBSOCKET
} hal_net_commands_source_t;

typedef struct {
  hal_net_commands_source_t source;
  const char *command;
  const char *args_text;
  const cJSON *json_root;
  const cJSON *json_args;
  const hal_http_request_t *http_request;
  hal_websocket_client_t websocket_client;
} hal_net_command_request_t;

typedef struct {
  hal_status_t status;
  const char *message;
  const char *content_type;
  char body[HAL_NET_COMMANDS_RESPONSE_BUFFER_SIZE];
  size_t body_len;
  bool overflow;
} hal_net_command_response_t;

typedef hal_status_t (*hal_net_command_handler_t)(
    const hal_net_command_request_t *request,
    hal_net_command_response_t *response,
    void *user);

hal_status_t hal_net_commands_register(const char *name,
                                       hal_net_command_handler_t handler,
                                       void *user);
hal_status_t hal_net_commands_unregister(const char *name);
void hal_net_commands_clear(void);
size_t hal_net_commands_count(void);

hal_status_t hal_net_commands_execute_text(
    const char *text,
    hal_net_command_response_t *response);
hal_status_t hal_net_commands_execute_json(
    const char *json,
    size_t len,
    hal_net_command_response_t *response);
hal_status_t hal_net_commands_execute(
    const void *data,
    size_t len,
    hal_net_commands_format_t format,
    hal_net_command_response_t *response);

hal_status_t hal_net_commands_register_http_route(
    const char *path,
    hal_net_commands_format_t format);
hal_status_t hal_net_commands_handle_http_request(
    const hal_http_request_t *request,
    hal_http_response_t *response,
    hal_net_commands_format_t format);
hal_status_t hal_net_commands_handle_websocket_message(
    hal_websocket_client_t client,
    hal_websocket_message_type_t type,
    const uint8_t *data,
    size_t len,
    hal_net_commands_format_t format);
```

Response helpers append to a fixed response buffer and use `hal_status_t`:

```c
void hal_net_command_response_reset(hal_net_command_response_t *response);
hal_status_t hal_net_command_response_set_status(
    hal_net_command_response_t *response,
    hal_status_t status,
    const char *message);
hal_status_t hal_net_command_response_set_content_type(
    hal_net_command_response_t *response,
    const char *content_type);
hal_status_t hal_net_command_response_write(
    hal_net_command_response_t *response,
    const void *data,
    size_t len);
hal_status_t hal_net_command_response_write_str(
    hal_net_command_response_t *response,
    const char *text);
hal_status_t hal_net_command_response_write_json(
    hal_net_command_response_t *response,
    const cJSON *json);
```

Basic flow:

```c
static hal_status_t status_command(const hal_net_command_request_t *request,
                                   hal_net_command_response_t *response,
                                   void *user) {
  (void)request;
  (void)user;
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    return HAL_ENOMEM;
  }
  cJSON_AddStringToObject(root, "status", "ok");
  hal_status_t status = hal_net_command_response_write_json(response, root);
  cJSON_Delete(root);
  return status;
}

hal_net_commands_register("status", status_command, NULL);
hal_net_commands_register_http_route(HAL_NET_COMMANDS_DEFAULT_HTTP_PATH,
                                     HAL_NET_COMMANDS_FORMAT_AUTO);
```

For WebSocket, call the helper from the normal message callback:

```c
static void ws_message(hal_websocket_client_t client,
                       hal_websocket_message_type_t type,
                       const uint8_t *data,
                       size_t len,
                       void *user) {
  (void)user;
  hal_net_commands_handle_websocket_message(
      client, type, data, len, HAL_NET_COMMANDS_FORMAT_AUTO);
}
```

If a handler does not write a body, the dispatcher emits a small default
response in the request format. Unknown commands return `HAL_ENOENT`; JSON
parse errors return `HAL_EPROTO`. HTTP integration maps common HAL failures to
HTTP status codes (`400`, `403`, `404`, `413`, `500`) and still returns
`HAL_OK` to the HTTP server once a response was written.

Default static limits can be overridden before including HAL headers:

```c
#define HAL_NET_COMMANDS_MAX_COMMANDS 8u
#define HAL_NET_COMMANDS_NAME_MAX 32u
#define HAL_NET_COMMANDS_TEXT_BUFFER_SIZE 256u
#define HAL_NET_COMMANDS_RESPONSE_BUFFER_SIZE 512u
```

**shared thematic implementation:** `hal/network/net_commands/hal_net_commands.cpp`.
**impl/.mock:** covered through mock HTTP/WebSocket TCP backends and
`test_hal_net_commands`.

---


## `hal_ota` - authenticated firmware update  *(opt-in - `HAL_ENABLE_OTA`)*

Thread-safe RP OTA service over HAL UDP/TCP. It uses a versioned JaszczurHAL
container, coordinated flash writes, and a fixed boot applier with automatic
trial boot, confirmation, and rollback.

```c
#include <hal/network/ota/hal_ota.h>

typedef enum {
  HAL_OTA_COMMAND_SKETCH = 0,
  HAL_OTA_COMMAND_FILESYSTEM = 1,
  HAL_OTA_COMMAND_UNKNOWN = 255
} hal_ota_command_t;

typedef enum {
  HAL_OTA_ERROR_AUTH = 1,
  HAL_OTA_ERROR_BEGIN = 2,
  HAL_OTA_ERROR_CONNECT = 3,
  HAL_OTA_ERROR_RECEIVE = 4,
  HAL_OTA_ERROR_END = 5,
  HAL_OTA_ERROR_UNKNOWN = 255
} hal_ota_error_t;

typedef void (*hal_ota_on_start_callback_t)(hal_ota_command_t command, void *user);
typedef void (*hal_ota_on_end_callback_t)(void *user);
typedef void (*hal_ota_on_progress_callback_t)(uint32_t progress, uint32_t total, void *user);
typedef void (*hal_ota_on_error_callback_t)(hal_ota_error_t error, void *user);

bool hal_ota_set_port(uint16_t port);
bool hal_ota_set_hostname(const char *hostname);
bool hal_ota_set_password(const char *password);

bool hal_ota_on_start(hal_ota_on_start_callback_t callback, void *user);
bool hal_ota_on_end(hal_ota_on_end_callback_t callback, void *user);
bool hal_ota_on_progress(hal_ota_on_progress_callback_t callback, void *user);
bool hal_ota_on_error(hal_ota_on_error_callback_t callback, void *user);

bool hal_ota_begin(void);
void hal_ota_handle(void);
bool hal_ota_is_started(void);

hal_status_t hal_ota_confirm_boot_ex(void);
hal_status_t hal_ota_get_boot_info_ex(hal_ota_boot_info_t *out_info);
```

**Behavior notes:**
- Module is available only when `HAL_ENABLE_OTA` is defined.
- It propagates `WIFI`, `UDP`, `TCP`, `CRYPTO` and `CRC`.
- `hal_ota_begin()` initializes OTA service and registers internal event hooks.
- `hal_ota_handle()` polls OTA transport and dispatches queued events to user callbacks.
- Callback handlers can be replaced or unregistered by passing `NULL`.
- Re-entering `hal_ota_begin()` clears queued mock/driver events before processing.
- Native images contain target id, program offset, generation, version,
  payload SHA-256, HMAC-SHA256 and header CRC. The HMAC key is derived from the
  same application password used by transport authentication.
- Native flash is split into a 16 KiB immutable boot region, equal
  `program`/`staging` slots, a phase journal, scratch sector, two redundant
  state sectors and the existing LittleFS/EEPROM tail.
- The boot applier swaps `program` and `staging` sector by sector. Its monotonic
  phase journal lets it resume after power loss. An unconfirmed trial is
  reverted after `HAL_RP_OTA_MAX_BOOT_ATTEMPTS` boots.
- Call `hal_ota_confirm_boot_ex()` only after application self-tests have
  passed. Calling it while stable is harmless.

**impl/rp2040:** staging/applier implementation for RP2040 and RP2350.
**impl/.mock:** deterministic event-injection test double.
**Thread safety:** The RP-family backend is thread-safe and multicore-safe for public
APIs. A singleton `hal_mutex_t` serializes all wrapper calls and callback
dispatch is performed outside that lock.

**Mock helpers:**
```c
void        hal_mock_ota_reset(void);
void        hal_mock_ota_set_begin_result(bool result);
void        hal_mock_ota_inject_start(hal_ota_command_t command);
void        hal_mock_ota_inject_end(void);
void        hal_mock_ota_inject_progress(uint32_t progress, uint32_t total);
void        hal_mock_ota_inject_error(hal_ota_error_t error);
uint16_t    hal_mock_ota_get_port(void);
const char *hal_mock_ota_get_hostname(void);
const char *hal_mock_ota_get_password(void);
uint32_t    hal_mock_ota_get_handle_count(void);
```

The complete native project, firmware, VS Code, firewall, confirmation,
rollback, and recovery contract is documented in
[Native RP OTA Workflow](../OTAWorkflow.md). A complete application is
available in [`examples/25_ota`](../../examples/25_ota/).

---

## `hal_udp` - UDP datagrams  *(opt-in - `HAL_ENABLE_UDP`)*

Handle-based UDP transport API for independent datagram sockets. The original
single-socket `hal_udp_*` API remains available as a compatibility wrapper on a
default UDP handle.

```c
#include <hal/network/hal_udp.h>

#define HAL_UDP_IP_STR_LEN 16u

typedef struct hal_udp_socket_impl_t *hal_udp_socket_t;

hal_udp_socket_t hal_udp_socket_open(void);
bool hal_udp_socket_bind(hal_udp_socket_t socket,
                         const hal_net_endpoint_t *local);
int  hal_udp_socket_sendto(hal_udp_socket_t socket,
                           const void *data,
                           size_t len,
                           const hal_net_endpoint_t *remote);
int  hal_udp_socket_recvfrom(hal_udp_socket_t socket,
                             void *buffer,
                             size_t max_len,
                             hal_net_endpoint_t *remote,
                             uint32_t timeout_ms);
bool hal_udp_socket_can_recv(hal_udp_socket_t socket);
bool hal_udp_socket_can_send(hal_udp_socket_t socket);
void hal_udp_socket_close(hal_udp_socket_t socket);

bool hal_udp_begin(uint16_t local_port);
void hal_udp_stop(void);

int  hal_udp_parse_packet(void);
int  hal_udp_read(uint8_t *buffer, uint16_t max_len);

bool     hal_udp_remote_ip(char *out, size_t out_size);
uint16_t hal_udp_remote_port(void);

bool     hal_udp_begin_packet(const char *host_or_ip, uint16_t remote_port);
bool     hal_udp_begin_packet_remote(void);
uint16_t hal_udp_write(const uint8_t *data, uint16_t len);
uint16_t hal_udp_write_str(const char *text);
bool     hal_udp_end_packet(void);
```

**Behavior notes:**
- Module is available only when `HAL_ENABLE_UDP` is defined.
- `hal_udp_socket_open()` allocates a socket from
  `HAL_UDP_SOCKET_MAX_INSTANCES`; close unused sockets with
  `hal_udp_socket_close()`.
- `hal_udp_socket_bind(...)` binds an IPv4 local endpoint. The address family
  must be `HAL_NET_AF_INET` and the port must be non-zero.
- `hal_udp_socket_sendto(...)` sends one datagram to an IPv4 endpoint and
  returns the accepted byte count or `<0` on error.
- `hal_udp_socket_recvfrom(...)` reads from one bound socket. `timeout_ms == 0`
  is an immediate poll; `HAL_NET_TIMEOUT_FOREVER` requests a blocking wait.
- `hal_udp_socket_can_recv(...)` and `hal_udp_socket_can_send(...)` are
  non-consuming readiness probes for compatibility layers such as BSD
  `select()`.
- `hal_udp_begin(...)` opens/binds the legacy default UDP socket.
- `hal_udp_parse_packet()` returns packet size, `0` when no packet is available.
- `hal_udp_remote_ip(...)` and `hal_udp_remote_port()` expose sender endpoint
  captured from the last successful `hal_udp_parse_packet()`.
- `hal_udp_begin_packet_remote()` sends response datagram to that captured sender.
- `hal_udp_write(...)` / `hal_udp_write_str(...)` append payload bytes to the
  datagram opened by `hal_udp_begin_packet*()`.
- `hal_udp_stop()` clears cached remote endpoint and active packet-send context.
- When `hal_wireguard` is active, datagrams to destinations covered by the
  WireGuard route/AllowedIPs are carried through the encrypted tunnel.

**impl/rp2040:** JaszczurHAL-owned lwIP raw UDP engine with a static socket
pool.
**impl/.mock:** deterministic multi-socket test double with injected inbound
packets, captured outbound packet metadata and payload.
**Thread safety:** The RP-family backend is thread-safe and multicore-safe for public
APIs. A singleton `hal_mutex_t` serializes access to the static UDP pool and
the owned lwIP engine.

**Mock helpers:**
```c
void        hal_mock_udp_reset(void);
void        hal_mock_udp_inject_packet(const char *remote_ip,
                                       uint16_t remote_port,
                                       const uint8_t *payload,
                                       uint16_t len);
void        hal_mock_udp_inject_packet_to(hal_udp_socket_t socket,
                                          const char *remote_ip,
                                          uint16_t remote_port,
                                          const uint8_t *payload,
                                          uint16_t len);
void        hal_mock_udp_set_end_packet_result(bool result);
void        hal_mock_udp_set_end_packet_result_for(hal_udp_socket_t socket,
                                                   bool result);
uint16_t    hal_mock_udp_get_local_port(void);
uint16_t    hal_mock_udp_get_local_port_for(hal_udp_socket_t socket);
const char *hal_mock_udp_get_last_begin_packet_host(void);
uint16_t    hal_mock_udp_get_last_begin_packet_port(void);
const uint8_t *hal_mock_udp_get_last_tx_payload(void);
const uint8_t *hal_mock_udp_get_last_tx_payload_for(hal_udp_socket_t socket);
uint16_t    hal_mock_udp_get_last_tx_len(void);
uint16_t    hal_mock_udp_get_last_tx_len_for(hal_udp_socket_t socket);
bool        hal_mock_udp_get_last_tx_remote_for(hal_udp_socket_t socket,
                                                hal_net_endpoint_t *out);
bool        hal_mock_udp_was_end_packet_called(void);
```

---

## `hal_tcp` - TCP sockets and listeners  *(opt-in - `HAL_ENABLE_TCP`)*

Handle-based TCP transport API for outbound stream connections and inbound
listener/server sockets.

```c
#include <hal/network/hal_tcp.h>

typedef struct hal_tcp_socket_impl_t *hal_tcp_socket_t;
typedef struct hal_tcp_listener_impl_t *hal_tcp_listener_t;

hal_tcp_socket_t hal_tcp_socket_open(void);
bool hal_tcp_socket_connect(hal_tcp_socket_t socket,
                            const hal_net_endpoint_t *remote,
                            uint32_t timeout_ms);
int  hal_tcp_socket_send(hal_tcp_socket_t socket,
                         const void *data,
                         size_t len);
int  hal_tcp_socket_recv(hal_tcp_socket_t socket,
                         void *buffer,
                         size_t max_len,
                         uint32_t timeout_ms);
bool hal_tcp_socket_can_recv(hal_tcp_socket_t socket);
bool hal_tcp_socket_can_send(hal_tcp_socket_t socket);
bool hal_tcp_socket_is_connected(hal_tcp_socket_t socket);
void hal_tcp_socket_shutdown(hal_tcp_socket_t socket);
void hal_tcp_socket_close(hal_tcp_socket_t socket);

hal_tcp_listener_t hal_tcp_listener_open(void);
bool hal_tcp_listener_bind(hal_tcp_listener_t listener,
                           const hal_net_endpoint_t *local);
bool hal_tcp_listener_listen(hal_tcp_listener_t listener, uint8_t backlog);
hal_tcp_socket_t hal_tcp_listener_accept(hal_tcp_listener_t listener,
                                         hal_net_endpoint_t *remote,
                                         uint32_t timeout_ms);
bool hal_tcp_listener_can_accept(hal_tcp_listener_t listener);
void hal_tcp_listener_close(hal_tcp_listener_t listener);
```

**Behavior notes:**
- Module is available only when `HAL_ENABLE_TCP` is defined.
- `hal_tcp_socket_open()` allocates a client socket from
  `HAL_TCP_SOCKET_MAX_INSTANCES`; close unused sockets with
  `hal_tcp_socket_close()`.
- `hal_tcp_socket_connect(...)` connects to an IPv4 endpoint. The address
  family must be `HAL_NET_AF_INET` and the port must be non-zero.
- `timeout_ms == 0` means an immediate/non-blocking receive poll.
  `HAL_NET_TIMEOUT_FOREVER` requests a blocking receive without a fixed
  deadline.
- `hal_tcp_socket_send(...)` returns the accepted byte count or `<0` on error.
- `hal_tcp_socket_recv(...)` returns bytes read, `0` on timeout/no data/peer
  close, or `<0` for invalid handles/arguments.
- `hal_tcp_socket_can_recv(...)` and `hal_tcp_socket_can_send(...)` are
  non-consuming readiness probes for compatibility layers such as BSD
  `select()`.
- `hal_tcp_socket_shutdown(...)` stops I/O but keeps the handle allocated.
- `hal_tcp_socket_close(...)` stops the backend client and returns the handle
  to the static pool.
- `hal_tcp_listener_open()` allocates a listener from
  `HAL_TCP_LISTENER_MAX_INSTANCES`; close unused listeners with
  `hal_tcp_listener_close()`.
- `hal_tcp_listener_bind(...)` binds an IPv4 local endpoint. The address family
  must be `HAL_NET_AF_INET` and the port must be non-zero.
- `hal_tcp_listener_listen(...)` starts accepting clients with a non-zero
  backlog. The portable mock caps pending clients at
  `HAL_TCP_LISTENER_BACKLOG_MAX`; real backends may apply their own platform
  limit.
- `hal_tcp_listener_accept(...)` returns a connected `hal_tcp_socket_t` from
  the normal TCP socket pool. `timeout_ms == 0` polls immediately and
  `HAL_NET_TIMEOUT_FOREVER` requests a blocking wait.
- `hal_tcp_listener_can_accept(...)` probes pending-client readiness without
  consuming the accepted socket.
- `hal_tcp_listener_close(...)` stops only the listener. Already accepted
  client sockets remain independent and must be closed separately.
- When `hal_wireguard` is active, connections to destinations covered by the
  WireGuard route/AllowedIPs are carried through the encrypted tunnel.

**impl/rp2040:** JaszczurHAL-owned lwIP raw TCP engine with static socket and
listener pools.
**impl/.mock:** deterministic client/listener test double with scripted
connect result, injected RX bytes, captured TX payload, captured remote
endpoint and per-listener pending-client queues.
**Thread safety:** The RP-family backend is thread-safe and multicore-safe for public
APIs. A singleton `hal_mutex_t` serializes access to the static TCP pools and
the owned lwIP engine.

**Mock helpers:**
```c
void        hal_mock_tcp_reset(void);
void        hal_mock_tcp_set_connect_result(bool result);
void        hal_mock_tcp_inject_rx(hal_tcp_socket_t socket,
                                   const uint8_t *payload,
                                   uint16_t len);
const uint8_t *hal_mock_tcp_get_last_tx_payload(hal_tcp_socket_t socket);
uint16_t    hal_mock_tcp_get_last_tx_len(hal_tcp_socket_t socket);
bool        hal_mock_tcp_get_remote_endpoint(hal_tcp_socket_t socket,
                                             hal_net_endpoint_t *out);
bool        hal_mock_tcp_listener_inject_client(hal_tcp_listener_t listener,
                                                const hal_net_endpoint_t *remote);
uint16_t    hal_mock_tcp_listener_get_local_port(hal_tcp_listener_t listener);
uint8_t     hal_mock_tcp_listener_get_backlog(hal_tcp_listener_t listener);
uint8_t     hal_mock_tcp_listener_get_pending_count(hal_tcp_listener_t listener);
```

---

## `hal_tls` - TLS client  *(opt-in - `HAL_ENABLE_TLS`)*

`hal_tls` is a provider-neutral, generation-checked TLS client facade backed by
the bundled BearSSL engine. Enabling it automatically enables TCP and WiFi, but
does not enable or require the optional BSD sockets adapter.

```c
#include <hal/network/tls/hal_tls.h>

hal_tls_client_config_t config;
hal_tls_client_t client = NULL;
hal_tls_trust_anchor_storage_t ca_storage;

hal_tls_trust_anchor_from_der_ex(ca_der, ca_der_length, &ca_storage);

hal_tls_security_config_t security = {
    .trust_anchors = &ca_storage.anchor,
    .trust_anchor_count = 1u,
    .get_time = hal_tls_default_time,
    .get_entropy = hal_tls_default_entropy,
};

hal_tls_client_config_init(&config);
hal_tls_client_create_ex(&config, &client);
hal_tls_client_configure_server_ex(client, "example.com", 443u);
hal_tls_client_configure_security_ex(client, &security);
hal_tls_client_connect_ex(client);

while (hal_tls_client_poll_ex(client) == HAL_EAGAIN) {
  hal_net_service();
}

hal_tls_client_write_ex(client, request, request_length, &written);
hal_tls_client_read_ex(client, response, sizeof(response), &received);
hal_tls_client_shutdown_ex(client);
hal_tls_client_close_ex(client);
```

`hal_tls_client_config_init()` selects poll execution, a 5-second finite
transport timeout, a 15-second operation timeout, and four provider steps per
poll. Applications may select `HAL_TLS_EXECUTION_BOUNDED_WORKER` when a
dedicated worker owns all finite blocking calls. Both timeout fields must
remain finite and non-zero.

Security configuration requires at least one RSA or EC trust anchor plus time
and entropy callbacks. `hal_tls_trust_anchor_from_der_ex()` decodes a DER CA
certificate into fixed caller-owned storage; all referenced trust buffers must
remain alive until the client closes. `hal_tls_default_time()` requires a
plausible synchronized clock, and `hal_tls_default_entropy()` uses the selected
target's secure entropy provider.

BearSSL receives the configured hostname for SNI and certificate identity
verification. It validates the chain, certificate validity period, and
hostname. An optional `server_public_key_sha256` adds a SHA-256 public-key pin
after certificate validation. Cancellation and service callbacks allow long
operations to stop cooperatively and keep the network/watchdog progressing.
Client handles are generation-checked; close releases the pool slot and stale
copies remain invalid.

The core TLS path resolves through `hal_net_resolve_ex()` and owns a native
`hal_tcp_socket_t`. BearSSL record progression uses a small private transport
contract rather than POSIX descriptors. This keeps TLS usable when
`HAL_ENABLE_BSD_SOCKETS` is disabled.

BSD sockets remain independently supported TLS transports. When both flags are
enabled, the BearSSL BSD adapter maps an existing descriptor's non-blocking
`send()`/`recv()` operations into the same private BearSSL transport contract.
Applications and third-party TLS clients that use BSD I/O continue to operate
over the public socket API; enabling native `hal_tls` does not change descriptor
ownership or BSD semantics.

**Implementation:**

- `hal_tls.cpp` owns lifecycle, DNS resolution, native HAL TCP transport and
  provider-independent security configuration;
- `hal/network/tls/BearSSL/jh_bearssl_hal_tcp_io.*` adapts HAL TCP;
- `hal/network/tls/BearSSL/jh_bearssl_bsd_io.*` is the optional
  TLS-over-BSD bridge;
- `hal/network/tls/BearSSL/jh_bearssl_engine.*` advances records through
  either transport without depending on either socket representation.

**Tests:** `test_hal_tls` covers the public lifecycle, `test_bearssl_provider`
covers native transport-independent engine behavior and TLS-over-BSD I/O, and
the configuration probes exercise independent TLS/BSD selection.
`tests/run_bearssl_native_integration.sh` creates a temporary RSA CA and a
`localhost` server certificate with DNS/IP SANs, starts a loopback OpenSSL
server, and verifies the valid case plus wrong-host, before-validity, and
after-validity failures. The generated private keys and certificates are
removed on exit.

---

## BSD sockets adapter  *(opt-in - `HAL_ENABLE_BSD_SOCKETS`)*

Minimal IPv4 BSD/POSIX compatibility layer over `hal_udp` and `hal_tcp`.
Enabling it automatically enables UDP, TCP and WiFi.

```c
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname,
               void *optval, socklen_t *optlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int shutdown(int sockfd, int how);
int fcntl(int fd, int cmd, ...);
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);

uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
uint32_t htonl(uint32_t hostlong);
uint32_t ntohl(uint32_t netlong);
in_addr_t inet_addr(const char *cp);
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

struct addrinfo;
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);
```

**MVP scope:** `AF_INET`, `SOCK_STREAM`, `SOCK_DGRAM`, TCP/UDP protocols,
`sockaddr_in`, byte-order helpers, IPv4 text/binary conversion, and one-result
IPv4 `getaddrinfo()`. Descriptor values start at `HAL_BSD_SOCKET_FD_BASE` and
are stored in a table sized by `HAL_BSD_SOCKET_MAX_FDS`.

**Behavior notes:**
- `socket(AF_INET, SOCK_DGRAM, 0/IPPROTO_UDP)` maps to
  `hal_udp_socket_open()`.
- `socket(AF_INET, SOCK_STREAM, 0/IPPROTO_TCP)` maps to
  `hal_tcp_socket_open()`.
- The adapter inherits normal `hal_udp`/`hal_tcp` routing. When
  `hal_wireguard` is active, traffic to destinations covered by the WireGuard
  route/AllowedIPs is carried through the encrypted tunnel. This is network-layer
  tunneling and does not replace TLS sockets, which provide end-to-end
  application/session-layer encryption.
- BSD sockets can be used as the transport for TLS libraries. The bundled
  BearSSL BSD adapter remains available when both `HAL_ENABLE_BSD_SOCKETS` and
  `HAL_ENABLE_TLS` are selected; the native `hal_tls` facade uses HAL TCP
  directly and therefore does not force BSD sockets into unrelated builds.
- UDP `sendto()` auto-binds to an ephemeral local port when the socket was not
  explicitly bound.
- UDP `connect()` stores a default peer endpoint and auto-binds if needed.
  After that, `send()`/`write()` transmit datagrams to that peer, while
  `recv()`/`read()` receive datagrams without returning a source address. Unlike
  POSIX connected UDP, the adapter does not filter incoming datagrams by that
  peer; it accepts the next datagram delivered by the HAL UDP socket.
- TCP `bind()` stages the local endpoint; `listen()` converts the descriptor to
  a HAL TCP listener. Accepted clients receive separate socket descriptors.
- `getaddrinfo(...)` resolves IPv4 literals or hostnames through
  `hal_net_resolve_ipv4(...)`. `service` must be numeric. Supported hint flags
  are `AI_PASSIVE`, `AI_CANONNAME`, `AI_NUMERICHOST`, `AI_NUMERICSERV` and
  `AI_ADDRCONFIG`; IPv6 remains outside the adapter.
- `setsockopt(...)` accepts `SOL_SOCKET` + `SO_REUSEADDR`/`SO_REUSEPORT`,
  `SO_RCVTIMEO` and `SO_SNDTIMEO`. `getsockopt(...)` reports those values and
  `SO_ERROR`; reading `SO_ERROR` clears the stored adapter error. Timeout
  options are stored with millisecond resolution, so sub-millisecond `timeval`
  values may round up when read back.
- `getsockname(...)` reports the local endpoint known to the adapter. TCP
  clients that did not explicitly `bind()` may report `0.0.0.0:0` because the
  HAL TCP contract does not expose the backend-assigned local port.
- `getpeername(...)` reports the connected TCP or UDP peer, including TCP
  sockets returned by `accept()`. It fails with `ENOTCONN` before a peer is
  known.
- Blocking calls use `HAL_NET_TIMEOUT_FOREVER` by default. `SO_RCVTIMEO` affects
  `accept()`, `recv()`/`read()` and `recvfrom()`; `SO_SNDTIMEO` affects
  `connect()` timeout selection. `fcntl(F_SETFL, O_NONBLOCK)` makes
  `accept()`, `connect()`, `recv()`/`read()` and `recvfrom()` use immediate HAL
  polls; `MSG_DONTWAIT` does the same per call for `recv`, `recvfrom`, `send`
  and `sendto`.
- Minimal `select()` supports read/write readiness for HAL socket descriptors.
  `exceptfds` is accepted and cleared; `poll()` remains outside this stage.
- Non-blocking TCP `connect()` is best-effort, not a full POSIX pending-connect
  state machine. The adapter performs one immediate HAL connect attempt. If it
  succeeds, the descriptor becomes writable and `SO_ERROR` is zero. If it does
  not complete immediately, `connect()` returns `-1`/`EINPROGRESS` and stores
  `EINPROGRESS` in `SO_ERROR`, but no background connect remains pending; retry
  `connect()` later or use blocking/timeout-based connect.
- Closing a descriptor from another task while a blocking `connect()`,
  `accept()`, `recv()` or `recvfrom()` is waiting is not an async cancellation
  contract. The adapter releases its fd-table lock while waiting and
  re-validates descriptors after the backend call returns, but callers that need
  cancellable waits should use `O_NONBLOCK` plus `select()` polling.
- Unsupported flags/operations fail with `errno`.

**shared thematic implementation:** `hal/network/adapters/bsd/hal_bsd_sockets.cpp`
contains the fd-table adapter, address conversion helpers and `netdb.h`
resolver glue.
**impl/.mock tests:** `test_bsd_sockets` covers behavior and errno mapping;
`test_bsd_sockets_c_compile` verifies simple C TCP/UDP client/server shapes,
`getaddrinfo()` and `setsockopt()` compile and link against the compatibility
headers.

---

## `hal_wireguard` - WireGuard tunnel wrapper  *(opt-in - `HAL_ENABLE_WIREGUARD`)*

Thread-safe facade over the shared WireGuard/lwIP engine.

```c
#include <hal/network/wireguard/hal_wireguard.h>

#define HAL_WIREGUARD_IPV4_OCTETS 4u
#define HAL_WIREGUARD_IP_STR_LEN 16u

bool hal_wireguard_parse_ipv4(const char *ip_text,
                              uint8_t out_ip[HAL_WIREGUARD_IPV4_OCTETS]);

bool hal_wireguard_begin(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                         const char *private_key,
                         const char *remote_peer_address,
                         const char *remote_peer_public_key,
                         uint16_t remote_peer_port);

bool hal_wireguard_begin_text(const char *local_ip_text,
                              const char *private_key,
                              const char *remote_peer_address,
                              const char *remote_peer_public_key,
                              uint16_t remote_peer_port);

bool hal_wireguard_begin_advanced(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  const char *private_key,
                                  const char *remote_peer_address,
                                  const char *remote_peer_public_key,
                                  uint16_t remote_peer_port,
                                  const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]);

bool hal_wireguard_begin_advanced_text(const char *local_ip_text,
                                       const char *private_key,
                                       const char *remote_peer_address,
                                       const char *remote_peer_public_key,
                                       uint16_t remote_peer_port,
                                       const char *allowed_ip_text,
                                       const char *allowed_mask_text);

void hal_wireguard_end(void);
bool hal_wireguard_is_initialized(void);

bool hal_wireguard_peer_up(char *endpoint_ip_out,
                           size_t endpoint_ip_out_size,
                           uint16_t *endpoint_port_out);

bool hal_wireguard_peer_up_quick(void);

bool hal_wireguard_kick_handshake(const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  uint16_t probe_port,
                                  uint32_t min_interval_ms);

bool hal_wireguard_kick_handshake_text(const char *probe_ip_text,
                                       uint16_t probe_port,
                                       uint32_t min_interval_ms);
```

**Behavior notes:**
- Module is available only when `HAL_ENABLE_WIREGUARD` is defined.
- `hal_wireguard_parse_ipv4(...)` validates and parses dotted IPv4 text (`a.b.c.d`) into octets.
- `hal_wireguard_begin(...)` uses full-tunnel mode (`AllowedIPs = 0.0.0.0/0`).
- `hal_wireguard_begin_text(...)` parses dotted local IP text and delegates to `hal_wireguard_begin(...)`.
- `hal_wireguard_begin_advanced(...)` enables split-tunnel mode via explicit AllowedIPs.
- `hal_wireguard_begin_advanced_text(...)` parses local/allowed/mask dotted IPv4 text and delegates to `hal_wireguard_begin_advanced(...)`.
- `hal_wireguard_peer_up(...)` can optionally return current endpoint IP/port.
- `hal_wireguard_peer_up_quick(...)` is a no-argument convenience check equivalent to `hal_wireguard_peer_up(NULL, 0u, NULL)`.
- `hal_wireguard_kick_handshake(...)` triggers non-blocking handshake probe.
- `hal_wireguard_kick_handshake_text(...)` parses dotted probe IP text and delegates to `hal_wireguard_kick_handshake(...)`.

**shared thematic implementation:** bundled protocol/crypto engine plus a private lwIP-extension
port used by capability-advertised host-stack backends.
**impl/rp2040:** HAL-owned lwIP extension and secure platform hooks.
**impl/stm32g474:** shared CYW43/lwIP underlay, hardware RNG entropy, and
HAL-synchronized NTP time.
**impl/.mock:** deterministic stateful test double with captured configuration,
peer endpoint injection and handshake-trigger observability.
**Thread safety:** a singleton `hal_mutex_t` serializes all public wrapper
calls; the selected backend serializes private lwIP stack access.

**Mock helpers:**
```c
void        hal_mock_wireguard_reset(void);
void        hal_mock_wireguard_set_begin_result(bool result);
void        hal_mock_wireguard_set_peer_up_result(bool result);
void        hal_mock_wireguard_set_kick_result(bool result);
void        hal_mock_wireguard_set_initialized(bool initialized);
void        hal_mock_wireguard_set_peer_endpoint(const uint8_t ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t port);
uint32_t    hal_mock_wireguard_get_peer_up_quick_call_count(void);
const uint8_t *hal_mock_wireguard_get_last_local_ip(void);
const uint8_t *hal_mock_wireguard_get_last_allowed_ip(void);
const uint8_t *hal_mock_wireguard_get_last_allowed_mask(void);
const char *hal_mock_wireguard_get_last_remote_peer_address(void);
uint16_t    hal_mock_wireguard_get_last_remote_peer_port(void);
bool        hal_mock_wireguard_was_begin_advanced(void);
const uint8_t *hal_mock_wireguard_get_last_probe_ip(void);
uint16_t    hal_mock_wireguard_get_last_probe_port(void);
uint32_t    hal_mock_wireguard_get_last_probe_min_interval_ms(void);
```

---

## `hal_mqtt` - MQTT client  *(opt-in - `HAL_ENABLE_MQTT`)*

Thread-safe MQTT wrapper around bundled PubSubClient with callback dispatch
outside the internal mutex to avoid lock-order deadlocks in user handlers.

```c
#include <hal/network/mqtt/hal_mqtt.h>

typedef void (*hal_mqtt_message_callback_t)(const char *topic,
                                            const uint8_t *payload,
                                            uint16_t length,
                                            void *user);

hal_status_t hal_mqtt_set_server_ex(const char *host, uint16_t port);
hal_status_t hal_mqtt_connect_ex(const char *client_id);
#ifdef HAL_ENABLE_TLS
hal_status_t hal_mqtt_configure_tls_ex(
    const hal_tls_security_config_t *security);
hal_status_t hal_mqtt_disable_tls_ex(void);
#endif

bool hal_mqtt_set_server(const char *host, uint16_t port);
bool hal_mqtt_set_callback(hal_mqtt_message_callback_t callback, void *user);
bool hal_mqtt_set_keepalive(uint16_t keepalive_s);
bool hal_mqtt_set_socket_timeout(uint16_t timeout_s);
bool hal_mqtt_set_buffer_size(uint16_t size);
uint16_t hal_mqtt_get_buffer_size(void);

bool hal_mqtt_connect(const char *client_id);
bool hal_mqtt_connect_auth(const char *client_id, const char *user, const char *pass);
void hal_mqtt_disconnect(void);
bool hal_mqtt_connected(void);
int  hal_mqtt_state(void);

bool hal_mqtt_loop(void);
bool hal_mqtt_publish(const char *topic, const uint8_t *payload, uint16_t payload_len, bool retained);
bool hal_mqtt_publish_str(const char *topic, const char *payload, bool retained);
bool hal_mqtt_subscribe(const char *topic, uint8_t qos);
bool hal_mqtt_unsubscribe(const char *topic);
```

**Behavior notes:**
- Module is available only when `HAL_ENABLE_MQTT` is defined.
- The RP backend uses bundled `PubSubClient` through the HAL TCP client
  adapter.
- STM32G474 uses the same PubSubClient/HAL TCP adapter when its CYW43 gSPI
  backend is configured.
- With `HAL_ENABLE_TLS`, call `hal_mqtt_configure_tls_ex()` before connecting
  to enable MQTTS. The referenced trust anchors and callbacks follow the
  `hal_tls` lifetime rules. Reconfiguration closes the current transport.
  `hal_mqtt_disable_tls_ex()` also disconnects and returns future connections
  to plaintext MQTT.
- MQTTS creates a generation-checked TLS client in bounded-worker mode. The
  MQTT socket timeout supplies both TLS transport and operation deadlines;
  connect polls to completion, then read/write use the TLS client until
  disconnect.
- `hal_mqtt_loop()` must be polled regularly to drive keepalive and receive
  inbound publishes.
- Inbound messages are copied to an internal buffer and delivered from
  `hal_mqtt_loop()` after releasing the internal mutex.

**impl/rp2040/stm32g474:** bundled `PubSubClient`
(`frameworks/PubSubClient`) over `hal_tcp` or `hal_tls`.
**impl/.mock:** deterministic stateful test double with injectable connect result,
loop result and inbound messages.
**Thread safety:** A singleton `hal_mutex_t` serializes all MQTT client calls.
Callbacks are delivered after the internal mutex is released.

**Mock helpers:**
```c
void        hal_mock_mqtt_reset(void);
void        hal_mock_mqtt_set_connect_result(bool result);
void        hal_mock_mqtt_set_loop_result(bool result);
void        hal_mock_mqtt_set_connected(bool connected);
void        hal_mock_mqtt_set_state(int state);
void        hal_mock_mqtt_inject_message(const char *topic, const uint8_t *payload, uint16_t length);
const char *hal_mock_mqtt_get_server_host(void);
uint16_t    hal_mock_mqtt_get_server_port(void);
const char *hal_mock_mqtt_get_last_publish_topic(void);
const uint8_t *hal_mock_mqtt_get_last_publish_payload(void);
uint16_t    hal_mock_mqtt_get_last_publish_len(void);
bool        hal_mock_mqtt_get_last_publish_retained(void);
const char *hal_mock_mqtt_get_last_subscribe_topic(void);
uint8_t     hal_mock_mqtt_get_last_subscribe_qos(void);
const char *hal_mock_mqtt_get_last_unsubscribe_topic(void);
uint16_t    hal_mock_mqtt_get_keepalive(void);
uint16_t    hal_mock_mqtt_get_socket_timeout(void);
```

---

## `hal_time` - Calendar helpers and optional system time/NTP

```c
#include <hal/time/hal_time.h>

// Always available; no network dependency.
uint32_t hal_time_from_components(int year, int month, int day,
                                  int hour, int minute, int second);
bool     hal_time_is_daylight_saving_time(int year, int month, int day);
void     hal_time_adjust_cet_cest(int *year, int *month, int *day,
                                  int *hour, int *minute);
bool     hal_time_is_in_range(long now, long start, long end);
void     hal_time_extract_minutes(long time_in_minutes,
                                  int *hours, int *minutes);

// Available with HAL_ENABLE_TIME.
bool     hal_time_set_timezone(const char *tz);     // POSIX TZ string
bool     hal_time_sync_ntp(const char *primary_server, const char *secondary_server);
uint64_t hal_time_unix(void);                       // seconds since epoch (Y2038-safe)
bool     hal_time_is_synced(uint64_t min_unix);     // true when time >= min_unix
bool     hal_time_get_local(struct tm *out_tm);
bool     hal_time_format_local(char *out, size_t out_size, const char *format);
```

The pure helpers use the shared proleptic-Gregorian calendar core. Component
conversion accepts dates from the Unix epoch through the last second
representable by `uint32_t`; its compatibility return value is `0` for both an
error and the valid epoch start. The CET/CEST helper uses the date-only legacy
policy: daylight saving starts on the last Sunday in March (inclusive) and ends
on the last Sunday in October (exclusive), with each transition taking effect
at 00:00 because no time-of-day argument is available. Invalid dates are
rejected, and adjustment normalizes day/month/year rollover.

`hal_time_is_in_range()` implements the half-open interval `[start, end)`.
`hal_time_extract_minutes()` uses C quotient/remainder semantics and accepts
either output pointer as optional.

**impl/rp2040/stm32g474:** bounded NTP request over HAL UDP, synchronized Unix
epoch tracking, POSIX timezone handling, and `localtime_r()`.
**impl/.mock:** state injection via mock helpers.
**Thread safety:** The pure helpers are reentrant. The optional system/NTP APIs
are not thread-safe; serialize those calls from the caller side.

**Mock helpers:**
```c
void        hal_mock_time_reset(void);
void        hal_mock_time_set_unix(uint64_t unix_time);
void        hal_mock_time_set_local(const struct tm *tm_local);
const char *hal_mock_time_get_timezone(void);
const char *hal_mock_time_get_ntp_primary(void);
const char *hal_mock_time_get_ntp_secondary(void);
```

---


---

*Next: [Utilities](16_utilities.md)*
