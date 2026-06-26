# Network connectivity

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: `hal_wifi`, `hal_udp`, `hal_tcp`, `hal_wireguard`, `hal_mqtt`,
`hal_ota`, `hal_time`, and the optional `HAL_ENABLE_BSD_SOCKETS` compatibility
adapter. Shared network types live in `hal_net.h`.

## Shared network types

`hal_net.h` contains plain C value types shared by handle-based UDP, TCP, and
BSD/POSIX compatibility layers. The endpoint/status types have no backend
dependency. The optional IPv4 resolver is available when WiFi support is
enabled, because real targets may need the network stack for DNS.

```c
#include <hal/hal_net.h>

#define HAL_NET_IPV4_ADDR_LEN 4u
#define HAL_NET_TIMEOUT_FOREVER UINT32_MAX

typedef enum {
  HAL_NET_AF_UNSPEC = 0,
  HAL_NET_AF_INET = 2
} hal_net_family_t;

typedef struct {
  hal_net_family_t family;
  uint8_t addr[HAL_NET_IPV4_ADDR_LEN];
  uint16_t port;
} hal_net_endpoint_t;

typedef enum {
  HAL_NET_OK = 0,
  HAL_NET_ERR_INVALID,
  HAL_NET_ERR_UNSUPPORTED,
  HAL_NET_ERR_NO_MEMORY,
  HAL_NET_ERR_NOT_CONNECTED,
  HAL_NET_ERR_TIMEOUT,
  HAL_NET_ERR_WOULD_BLOCK,
  HAL_NET_ERR_BACKEND
} hal_net_status_t;

#ifdef HAL_ENABLE_WIFI
bool hal_net_resolve_ipv4(const char *host_or_ip,
                          uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]);
#endif
```

Endpoint IPv4 octets are stored in network byte order. The `port` field is in
host byte order; POSIX adapters perform their own `htons()` / `ntohs()`
translation at the API boundary.

**Resolver notes:**
- `hal_net_resolve_ipv4(...)` accepts a dotted IPv4 literal or hostname and
  writes four IPv4 octets. The caller keeps the transport port separately.
- The mock backend resolves IPv4 literals, `localhost`, and test entries added
  with `hal_mock_net_set_dns_entry(...)`.
- The RP2040 backend resolves IPv4 literals locally and uses
  Arduino-pico `WiFi.hostByName()` for hostnames.

**Mock resolver helpers:**
```c
void hal_mock_net_reset(void);
bool hal_mock_net_set_dns_entry(const char *host, const char *ip);
```

## `hal_wifi` - WiFi  *(optional - `HAL_ENABLE_WIFI`)*

Arduino builds should select a WiFi-capable board/FQBN (for example
`rp2040:rp2040:rpipicow`). `PICO_W` is not the HAL WiFi enable flag; keep using
`HAL_ENABLE_WIFI` directly or enable a dependent module such as
`HAL_ENABLE_MQTT` / `HAL_ENABLE_WIREGUARD`, which propagates WiFi.

```c
#include <hal/hal_wifi.h>

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
int     hal_wifi_ping(const char *host_or_ip);      // >=0 ok, <0 error (uses timeout set by hal_wifi_set_timeout_ms)
int     hal_wifi_ping_ex(const char *host_or_ip, uint32_t timeout_ms); // >=0 ok, <0 error (per-call timeout)
int     hal_wifi_scan_networks(void);               // >=0 result count, <0 error
bool    hal_wifi_get_scan_result(size_t index, hal_wifi_scan_result_t *out);
const char *hal_wifi_encryption_to_string(hal_wifi_encryption_t encryption);
```

**impl/rp2040:** Arduino-pico WiFi stack (`WiFi.h`).
**impl/.mock:** state injection via mock helpers.
**Thread safety:** RP2040 backend is thread-safe and multicore-safe for public
HAL wrapper calls. An internal singleton `hal_mutex_t` serializes access to
the underlying `WiFi` object and is created with an atomic create-once fallback.
The Arduino-pico WiFi/LWIP internals are still treated as serialized
third-party code rather than a fully HAL-native audited transport. The mock
backend is a simple state-injection test double and does not provide the same
synchronization guarantee for concurrent test-thread access.

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

---


## `hal_ota` - ArduinoOTA wrapper  *(opt-in - `HAL_ENABLE_OTA`)*

Thread-safe wrapper around ArduinoOTA with callback dispatch from
`hal_ota_handle()` (outside internal lock), similar to `hal_mqtt_loop()` style.

```c
#include <hal/hal_ota.h>

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
```

**Behavior notes:**
- Module is available only when `HAL_ENABLE_OTA` is defined.
- Requires WiFi support: enabling `HAL_ENABLE_OTA` automatically defines `HAL_ENABLE_WIFI`.
- `hal_ota_begin()` initializes OTA service and registers internal event hooks.
- `hal_ota_handle()` polls OTA transport and dispatches queued events to user callbacks.
- Callback handlers can be replaced or unregistered by passing `NULL`.
- Re-entering `hal_ota_begin()` clears queued mock/driver events before processing.

**impl/rp2040:** Arduino-pico `ArduinoOTA` backend.
**impl/.mock:** deterministic event-injection test double.
**Thread safety:** RP2040 backend is thread-safe and multicore-safe for public
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

---

## `hal_udp` - UDP datagrams  *(opt-in - `HAL_ENABLE_UDP`)*

Handle-based UDP transport API for independent datagram sockets. The original
single-socket `hal_udp_*` API remains available as a compatibility wrapper on a
default UDP handle.

```c
#include <hal/hal_udp.h>

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

**impl/rp2040:** Arduino-pico `WiFiUDP` backend with a static socket pool.
**impl/.mock:** deterministic multi-socket test double with injected inbound
packets, captured outbound packet metadata and payload.
**Thread safety:** RP2040 backend is thread-safe and multicore-safe for public
APIs. A singleton `hal_mutex_t` serializes access to the static UDP pool and
the underlying `WiFiUDP` instances.

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
#include <hal/hal_tcp.h>

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

**impl/rp2040:** Arduino-pico `WiFiClient`/`WiFiServer` backend with static
socket and listener pools.
**impl/.mock:** deterministic client/listener test double with scripted
connect result, injected RX bytes, captured TX payload, captured remote
endpoint and per-listener pending-client queues.
**Thread safety:** RP2040 backend is thread-safe and multicore-safe for public
APIs. A singleton `hal_mutex_t` serializes access to the static TCP pools and
the underlying `WiFiClient`/`WiFiServer` instances.

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

**impl/shared:** fd-table adapter, IPv4 conversion helpers and minimal
`netdb.h` resolver glue.
**impl/.mock tests:** `test_bsd_sockets` covers behavior and errno mapping;
`test_bsd_sockets_c_compile` verifies simple C TCP/UDP client/server shapes,
`getaddrinfo()` and `setsockopt()` compile and link against the compatibility
headers.

---

## `hal_wireguard` - WireGuard tunnel wrapper  *(opt-in - `HAL_ENABLE_WIREGUARD`)*

Thread-safe wrapper around bundled `arduino-wireguard-pico-w` API.

```c
#include <hal/hal_wireguard.h>

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

**impl/rp2040:** bundled `arduino-wireguard-pico-w` driver.
**impl/.mock:** deterministic stateful test double with captured configuration,
peer endpoint injection and handshake-trigger observability.
**Thread safety:** RP2040 backend is thread-safe and multicore-safe for public
APIs. A singleton `hal_mutex_t` serializes all wrapper calls.

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
#include <hal/hal_mqtt.h>

typedef void (*hal_mqtt_message_callback_t)(const char *topic,
                                            const uint8_t *payload,
                                            uint16_t length,
                                            void *user);

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
- Current RP2040 backend uses `WiFiClient` + bundled `PubSubClient`.
- `hal_mqtt_loop()` must be polled regularly to drive keepalive and receive
  inbound publishes.
- Inbound messages are copied to an internal buffer and delivered from
  `hal_mqtt_loop()` after releasing the internal mutex.

**impl/rp2040:** bundled `PubSubClient` (`frameworks/PubSubClient`) over `WiFiClient`.
**impl/.mock:** deterministic stateful test double with injectable connect result,
loop result and inbound messages.
**Thread safety:** RP2040 backend is thread-safe and multicore-safe for public
APIs. A singleton `hal_mutex_t` serializes all MQTT client calls.

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

## `hal_time` - System time & NTP  *(optional - `HAL_ENABLE_TIME`)*

```c
#include <hal/hal_time.h>

bool     hal_time_set_timezone(const char *tz);     // POSIX TZ string
bool     hal_time_sync_ntp(const char *primary_server, const char *secondary_server);
uint64_t hal_time_unix(void);                       // seconds since epoch (Y2038-safe)
bool     hal_time_is_synced(uint64_t min_unix);     // true when time >= min_unix
bool     hal_time_get_local(struct tm *out_tm);
bool     hal_time_format_local(char *out, size_t out_size, const char *format);
```

**impl/rp2040:** `configTime()` / `time()` / `localtime_r()` (Arduino-pico / lwIP SNTP).
**impl/.mock:** state injection via mock helpers.
**Thread safety:** Not thread-safe. Serialize all calls from the caller side.

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
