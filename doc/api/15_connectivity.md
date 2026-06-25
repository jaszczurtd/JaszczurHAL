# Network connectivity

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: `hal_wifi`, `hal_udp`, `hal_wireguard`, `hal_mqtt`, `hal_ota`, `hal_time`.

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

Thread-safe wrapper around Arduino-pico `WiFiUDP` for single-socket datagram
receive/send flows.

```c
#include <hal/hal_udp.h>

#define HAL_UDP_IP_STR_LEN 16u

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
- `hal_udp_begin(...)` opens one UDP socket bound to a local port.
- `hal_udp_parse_packet()` returns packet size, `0` when no packet is available.
- `hal_udp_remote_ip(...)` and `hal_udp_remote_port()` expose sender endpoint
  captured from the last successful `hal_udp_parse_packet()`.
- `hal_udp_begin_packet_remote()` sends response datagram to that captured sender.
- `hal_udp_write(...)` / `hal_udp_write_str(...)` append payload bytes to the
  datagram opened by `hal_udp_begin_packet*()`.
- `hal_udp_stop()` clears cached remote endpoint and active packet-send context.

**impl/rp2040:** Arduino-pico `WiFiUDP` backend.
**impl/.mock:** deterministic stateful test double with injected inbound packet,
captured outbound packet metadata and payload.
**Thread safety:** RP2040 backend is thread-safe and multicore-safe for public
APIs. A singleton `hal_mutex_t` serializes all wrapper calls.

**Mock helpers:**
```c
void        hal_mock_udp_reset(void);
void        hal_mock_udp_inject_packet(const char *remote_ip,
                                       uint16_t remote_port,
                                       const uint8_t *payload,
                                       uint16_t len);
void        hal_mock_udp_set_end_packet_result(bool result);
uint16_t    hal_mock_udp_get_local_port(void);
const char *hal_mock_udp_get_last_begin_packet_host(void);
uint16_t    hal_mock_udp_get_last_begin_packet_port(void);
const uint8_t *hal_mock_udp_get_last_tx_payload(void);
uint16_t    hal_mock_udp_get_last_tx_len(void);
bool        hal_mock_udp_was_end_packet_called(void);
```

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
