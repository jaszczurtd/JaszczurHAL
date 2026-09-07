# Cellular modem

*Also available in [Polish](../pl/12_modem.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

This chapter covers AT command exchange and SimCom A76xx operation: startup, network registration, location data, and MQTT communication.

<a id="hal_modem_at---generic-at-command-engine--facade---hal_enable_cellular_modem"></a>

## `hal_modem_at` - AT command exchange  *(facade - `HAL_ENABLE_CELLULAR_MODEM`)*

Send AT commands, receive responses, and wait for modem notifications over UART. The module manages the port, receive buffer, and exchange state. A separate modem-family driver handles startup sequences and vendor-specific commands; the current implementation is `hal_simcom_a76xx`.

```c
#include <hal/modem/hal_modem_at.h>

typedef enum {
    HAL_MODEM_AT_OK = 0,
    HAL_MODEM_AT_ERROR,
    HAL_MODEM_AT_TIMEOUT,
    HAL_MODEM_AT_NO_PROMPT,
    HAL_MODEM_AT_INVALID_ARG,
    HAL_MODEM_AT_BUSY
} hal_modem_at_result_t;

typedef hal_modem_at_impl_t *hal_modem_at_t;

typedef struct {
    hal_uart_t  uart;
    char       *rx_buf;
    size_t      rx_buf_size;
    uint32_t    default_timeout_ms;
    uint32_t    quiet_window_ms;
} hal_modem_at_config_t;

typedef void (*hal_modem_at_urc_cb_t)(const char *line, void *user);
typedef bool (*hal_modem_at_ready_cb_t)(const char *buf, size_t len, void *user);

hal_modem_at_t hal_modem_at_create(const hal_modem_at_config_t *cfg);
void           hal_modem_at_destroy(hal_modem_at_t h);

hal_modem_at_result_t hal_modem_at_send(hal_modem_at_t h, const char *cmd,
                                        const char *expected, uint32_t timeout_ms);
hal_modem_at_result_t hal_modem_at_send_with_data(hal_modem_at_t h, const char *cmd,
                                                  const uint8_t *data, size_t data_len,
                                                  uint32_t prompt_timeout_ms,
                                                  uint32_t resp_timeout_ms);
hal_modem_at_result_t hal_modem_at_listen_until(hal_modem_at_t h,
                                                hal_modem_at_ready_cb_t ready,
                                                void *user,
                                                uint32_t total_timeout_ms);

const char *hal_modem_at_last_response(hal_modem_at_t h);

bool hal_modem_at_urc_register(hal_modem_at_t h, const char *prefix,
                               hal_modem_at_urc_cb_t cb, void *user);
int  hal_modem_at_urc_poll(hal_modem_at_t h);
void hal_modem_at_set_log_filter(hal_modem_at_t h,
                                 const char *const *secrets, size_t count);
void hal_modem_at_set_line_observer(hal_modem_at_t h,
                                    hal_modem_at_urc_cb_t cb, void *user);

typedef void (*hal_modem_at_tick_cb_t)(void *user);
void hal_modem_at_set_tick_callback(hal_modem_at_t h,
                                    hal_modem_at_tick_cb_t cb, void *user);
void hal_modem_at_sleep_ms(hal_modem_at_t h, uint32_t ms);
```

- **Backend:** single implementation (`src/hal/modem/hal_modem_at.cpp`) shared
  between hardware and mock targets - sits entirely on `hal_uart` +
  `hal_millis` + `hal_mutex`.

**Thread safety:** every handle serialises access internally via a
per-instance mutex. Safe to call from multiple threads/cores.

**Watchdog servicing:** Register a function with `hal_modem_at_set_tick_callback()` to service the watchdog during long modem operations. Each internal wait loop (`send`, `send_with_data`, `listen_until`) and each higher-level wait built on it, including `hal_simcom_a76xx_wait_*` and power pulses, invokes the callback at least once every approximately 20 ms. The callback can call `hal_watchdog_feed()` and optionally update a status LED.

---

<a id="hal_simcom_a76xx---simcom-a76xx-modem-driver--optional---hal_enable_a7670"></a>

## `hal_simcom_a76xx` - SimCom A76xx modem support  *(optional - `HAL_ENABLE_A7670`)*

Operate SimCom A76xx modems (A7670E/SA/G, A7672E/S, A7608, and other variants in the family) through `hal_modem_at`. The module controls power and startup, initializes the SIM, registers with the network, and establishes a PDP context. It can retrieve network time, an approximate cellular-network location (LBS), and a GNSS position. Its MQTT client uses `CMQTT*` commands for publishing and subscriptions.

```c
#include <hal/modem/hal_simcom_a76xx.h>

typedef enum {
    HAL_SIMCOM_A76XX_OK = 0,
    HAL_SIMCOM_A76XX_ERROR,
    HAL_SIMCOM_A76XX_TIMEOUT,
    HAL_SIMCOM_A76XX_INVALID_ARG,
    HAL_SIMCOM_A76XX_NOT_READY,
    HAL_SIMCOM_A76XX_PARSE
} hal_simcom_a76xx_result_t;

typedef hal_simcom_a76xx_impl_t *hal_simcom_a76xx_t;

typedef struct {
    hal_uart_t uart;
    int        pwr_pin;   // power-control GPIO (idle HIGH, active-LOW pulse), -1 to disable
    char      *rx_buf;
    size_t     rx_buf_size;
    uint32_t   default_at_timeout_ms;
} hal_simcom_a76xx_config_t;

typedef struct {
    const char *apn;
    const char *user;      // may be NULL
    const char *password;  // may be NULL
} hal_simcom_a76xx_apn_t;

typedef struct {
  float latitude_deg;
  float longitude_deg;
  int   accuracy_m;   // -1 when modem reply omits accuracy
  float speed_kmh;    // HAL-estimated from consecutive fixes, -1 when unavailable
} hal_simcom_a76xx_cell_location_t;

typedef struct {
  double latitude_deg;
  double longitude_deg;
  double altitude_m;      // -1 when unavailable
  double speed_kmh;       // -1 when unavailable
  double course_deg;      // -1 when unavailable
  double hdop;            // -1 when unavailable
  double pdop;            // -1 when unavailable
  double vdop;            // -1 when unavailable
  int    satellites_used; // -1 when unavailable
  int    satellites_view; // -1 when unavailable
  int    fix_mode;        // modem-specific status, -1 when unavailable
  char   utc[24];         // modem UTC text, or empty
} hal_simcom_a76xx_gnss_location_t;

typedef struct {
    bool        enabled;
    int         ssl_context_id;
    const char *ca_cert_name;       // pre-uploaded via AT+CCERTDOWN, or NULL
    bool        ignore_local_time;
    bool        enable_sni;
    int         sslversion;         // 0..4, default 4 (TLS 1.2)
    int         authmode;           // 0..3, default 1 (server only)
} hal_simcom_a76xx_ssl_config_t;

typedef struct {
    const char *broker_host;
    uint16_t    broker_port;
    const char *client_id;
    const char *username;          // may be NULL
    const char *password;          // may be NULL
    uint16_t    keepalive_s;
    bool        clean_session;
    int         client_index;      // 0..1 (SimCom CMQTT slots)
    hal_simcom_a76xx_ssl_config_t ssl;
} hal_simcom_a76xx_mqtt_config_t;

typedef void (*hal_simcom_a76xx_mqtt_message_cb_t)(int client_index,
                                                   const char *topic,
                                                   const uint8_t *payload,
                                                   size_t payload_len,
                                                   void *user);

/* Lifecycle */
hal_simcom_a76xx_t hal_simcom_a76xx_create(const hal_simcom_a76xx_config_t *cfg);
void                hal_simcom_a76xx_destroy(hal_simcom_a76xx_t h);
hal_modem_at_t      hal_simcom_a76xx_get_at(hal_simcom_a76xx_t h);

/* Power (optional helper).

   The waveform is: idle HIGH -> active-LOW pulse -> HIGH. Use pwr_pin
   for any board whose power-control input matches that polarity:
     - SimCom PWRKEY through a transistor, OR
     - relay / load-switch ENABLE where HIGH means "powered"
       (a single power_toggle() is then a full physical power-cycle).

   Set pwr_pin = -1 (and drive the GPIO from application code) when
   the board uses inverted polarity, needs a more elaborate power
   sequence, or you're running unit tests with a UART script. In that
   case both helpers below become no-ops.

   hard_reset() is the SimCom PWRKEY "force off then on" sequence
   (two pulses + 5 s gaps); for relay-gated boards a single
   power_toggle() is usually enough - don't double-power-cycle. */
hal_simcom_a76xx_result_t hal_simcom_a76xx_power_toggle(hal_simcom_a76xx_t h,
                                                        uint32_t pulse_ms);
hal_simcom_a76xx_result_t hal_simcom_a76xx_hard_reset(hal_simcom_a76xx_t h);

/* Boot / SIM / Network */
hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_boot(hal_simcom_a76xx_t h,
                                                     uint32_t total_timeout_ms);
hal_simcom_a76xx_result_t hal_simcom_a76xx_init(hal_simcom_a76xx_t h);
hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_sim_ready(hal_simcom_a76xx_t h,
                                                          uint32_t timeout_ms);
hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_network_registered(hal_simcom_a76xx_t h,
                                                                   uint32_t timeout_ms);
hal_simcom_a76xx_result_t hal_simcom_a76xx_attach_pdp(hal_simcom_a76xx_t h,
                                                      const hal_simcom_a76xx_apn_t *apn);

/* Time */
hal_simcom_a76xx_result_t hal_simcom_a76xx_get_network_time_iso8601(hal_simcom_a76xx_t h,
                                                                    char *out,
                                                                    size_t out_size);
hal_simcom_a76xx_result_t hal_simcom_a76xx_get_cell_location(hal_simcom_a76xx_t h,
                                                             hal_simcom_a76xx_cell_location_t *out_location,
                                                             uint32_t timeout_ms);
void hal_simcom_a76xx_gnss_location_init(hal_simcom_a76xx_gnss_location_t *loc);
hal_simcom_a76xx_result_t hal_simcom_a76xx_gnss_power_on(hal_simcom_a76xx_t h,
                                                         uint32_t timeout_ms);
bool hal_simcom_a76xx_gnss_is_powered(hal_simcom_a76xx_t h);
hal_simcom_a76xx_result_t hal_simcom_a76xx_get_gnss_location(hal_simcom_a76xx_t h,
                                                             hal_simcom_a76xx_gnss_location_t *out_location,
                                                             uint32_t timeout_ms);

/* MQTT */
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_connect(hal_simcom_a76xx_t h,
                                                        const hal_simcom_a76xx_mqtt_config_t *cfg);
const char *hal_simcom_a76xx_mqtt_result_string(int result_code);
int hal_simcom_a76xx_mqtt_last_connect_result(hal_simcom_a76xx_t h,
                                              int client_index);
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_disconnect(hal_simcom_a76xx_t h,
                                                           int client_index);
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_publish(hal_simcom_a76xx_t h,
                                                        int client_index,
                                                        const char *topic,
                                                        const void *payload,
                                                        size_t payload_len,
                                                        int qos);
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_subscribe(hal_simcom_a76xx_t h,
                                                          int client_index,
                                                          const char *topic,
                                                          int qos);
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_unsubscribe(hal_simcom_a76xx_t h,
                                                            int client_index,
                                                            const char *topic);
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_set_message_callback(hal_simcom_a76xx_t h,
                                                                     hal_simcom_a76xx_mqtt_message_cb_t cb,
                                                                     void *user);
int  hal_simcom_a76xx_mqtt_poll(hal_simcom_a76xx_t h);
bool hal_simcom_a76xx_mqtt_is_connected(hal_simcom_a76xx_t h, int client_index);
```

GNSS helpers normalise common SimCom response variants:
`+CGNSSINFO`, `+CGNSINF` and `+CGPSINFO`. `hal_simcom_a76xx_get_gnss_location()`
ensures GNSS is powered first, then queries the fix. It returns
`HAL_SIMCOM_A76XX_NOT_READY` when the modem responds successfully but has no
fix yet, for example `+CGNSSINFO: ,,,,,,,,`. For the A7670E
`+CGNSSINFO: <fix>,<sat_count>,...` shape, the single satellite count is
reported as both `satellites_used` and `satellites_view`.

**Receiving MQTT messages:** The modem reports a message through four URCs (`+CMQTTRXSTART:` / `+CMQTTRXTOPIC:` / `+CMQTTRXPAYLOAD:` / `+CMQTTRXEND:`), interleaved with separate topic and payload lines. The driver assembles them into one message and invokes `hal_simcom_a76xx_mqtt_message_cb_t` from `hal_simcom_a76xx_mqtt_poll()`.

`+CMQTTCONNECT: <client>,<result>` is decoded by the driver. Failed
connections produce a readable console diagnostic, for example:

```text
ERROR! [SIMCOM][MQTT] connect failed: socket connect failed (client=0, code=3)
```

Successful connections are also confirmed after the raw modem response:

```text
[SIMCOM][MQTT] connected successfully (client=0, code=0)
```

The numeric result remains available through
`hal_simcom_a76xx_mqtt_last_connect_result()`, while
`hal_simcom_a76xx_mqtt_result_string()` exposes the complete SimCom result
table for application diagnostics. A result of `3` is a socket-connect
failure before MQTT authentication; bad username/password is `30`, rejected
authorization is `31`, and a TLS handshake failure is `32`.

**Thread safety:** every handle serialises on the underlying
`hal_modem_at` mutex. Safe to call from multiple threads/cores.

---


---

*Next: [Output devices](13_output_devices.md)*
