#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_A7670

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal_modem_at.h"
#include "hal_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_simcom_a76xx.h
 * @brief High-level driver for SimCom A76xx-family cellular modems
 *        (A7670E/SA/G, A7672E/S, A7608, ...).
 *
 * This module sits on top of @ref hal_modem_at and provides the
 * vendor-specific state machine commonly needed by an application:
 * power control, boot synchronisation, SIM/network bring-up, PDP
 * context attach, network-time retrieval, and a full MQTT client
 * (publish + subscribe) built on top of the SimCom CMQTT command
 * family.
 *
 * Every handle is multi-thread safe: all entry points serialise on
 * the underlying hal_modem_at handle's mutex.
 *
 * The driver is opt-in via HAL_ENABLE_A7670 (which automatically
 * pulls in HAL_ENABLE_CELLULAR_MODEM and HAL_ENABLE_UART).
 *
 * Naming note: although marketed as "A7670", the chipset family is
 * SIMCom A76xx; the AT command set (CMQTT*, CSSLCFG, CREG, CGACT, ...)
 * is shared across the family, hence the driver name.
 */

/**
 * @brief Result of a SimCom A76xx driver operation.
 */
typedef enum {
  HAL_SIMCOM_A76XX_OK = 0,
  HAL_SIMCOM_A76XX_ERROR,   /**< Modem replied ERROR / CME ERROR / CMS ERROR. */
  HAL_SIMCOM_A76XX_TIMEOUT, /**< Expected state not reached in time. */
  HAL_SIMCOM_A76XX_INVALID_ARG, /**< NULL handle, bad pointer, oversize field,
                                   etc. */
  HAL_SIMCOM_A76XX_NOT_READY,   /**< Pre-condition violated (e.g. publish before
                                   MQTT connect). */
  HAL_SIMCOM_A76XX_PARSE        /**< Modem response could not be parsed. */
} hal_simcom_a76xx_result_t;

typedef struct hal_simcom_a76xx_impl_s hal_simcom_a76xx_impl_t;
typedef hal_simcom_a76xx_impl_t *hal_simcom_a76xx_t;

/**
 * @brief Configuration for hal_simcom_a76xx_create().
 *
 * The caller is responsible for the lifetime of @p uart and @p rx_buf.
 * The driver takes ownership of an internal hal_modem_at instance that
 * is built around them.
 */
typedef struct {
  /** UART previously created and begun by the caller. Must not be NULL. */
  hal_uart_t uart;

  /**
   * GPIO pin used to drive the modem's power-control input. Pulled
   * LOW for pulse_ms by hal_simcom_a76xx_power_toggle(), then back
   * HIGH (waveform: idle HIGH -> active-LOW pulse -> HIGH).
   *
   * Typical wiring:
   *   - SimCom PWRKEY through a transistor (active-low pulse toggles
   *     the module's internal PMU on/off),
   *   - relay coil / load switch whose ENABLE matches the same
   *     polarity (HIGH = powered, LOW = unpowered) - in that case
   *     a power_toggle() becomes a physical power-cycle.
   *
   * Set to -1 to disable the in-driver power control. Choose -1
   * when:
   *   - running unit tests with a UART script,
   *   - the board's power-control signal has the OPPOSITE polarity
   *     (HIGH-active enable), or
   *   - the application gates modem power in a more complex way
   *     (sequencing rails, sibling MCU, etc.) and prefers to do its
   *     own power-cycle / hard-reset sequence.
   *
   * When -1, power_toggle()/hard_reset() below become no-ops and
   * the application is fully responsible for any physical power
   * management.
   */
  int pwr_pin;

  /**
   * Caller-owned scratch buffer used by the underlying AT engine.
   * Must outlive the handle. Recommended size: 1024 bytes (the
   * MQTT-RX URC reassembly needs room for topic + payload of the
   * incoming message).
   */
  char *rx_buf;
  size_t rx_buf_size;

  /**
   * Default per-AT-command timeout (ms). Used internally when the
   * driver issues short commands such as ATE0, CPIN?, CCLK?.
   * Typical: 2000 ms.
   */
  uint32_t default_at_timeout_ms;
} hal_simcom_a76xx_config_t;

/**
 * @brief APN credentials passed to hal_simcom_a76xx_attach_pdp().
 */
typedef struct {
  const char *apn;      /**< APN name. Must not be NULL or empty. */
  const char *user;     /**< Optional CHAP/PAP user, or NULL. */
  const char *password; /**< Optional CHAP/PAP password, or NULL. */
} hal_simcom_a76xx_apn_t;

/**
 * @brief Coarse cellular location resolved by the modem LBS service.
 *
 * Coordinates are approximate and derived from serving-cell context
 * (not GNSS). Expected accuracy heavily depends on network density and
 * operator support.
 */
typedef struct {
  float latitude_deg;  /**< Latitude in decimal degrees. */
  float longitude_deg; /**< Longitude in decimal degrees. */
  int accuracy_m;  /**< Estimated radius in meters, or -1 when omitted by modem
                      reply. */
  float speed_kmh; /**< HAL-estimated speed from consecutive fixes; -1 when
                      unavailable. */
} hal_simcom_a76xx_cell_location_t;

/**
 * @brief GNSS location resolved by the modem's built-in receiver.
 *
 * Values are normalised across common SimCom GNSS response variants
 * (@c +CGNSSINFO, @c +CGNSINF and @c +CGPSINFO). Coordinates are decimal
 * degrees. Optional numeric fields use -1 when omitted by the modem. In the
 * A7670E @c +CGNSSINFO format, the single satellite count after fix mode is
 * mirrored to both satellite fields because the modem does not distinguish
 * "used" and "in view" in that response.
 */
typedef struct {
  double latitude_deg;  /**< Latitude in decimal degrees. */
  double longitude_deg; /**< Longitude in decimal degrees. */
  double altitude_m;    /**< Altitude in meters, or -1 when unavailable. */
  double speed_kmh;     /**< Speed in km/h, or -1 when unavailable. */
  double
      course_deg; /**< Course over ground in degrees, or -1 when unavailable. */
  double hdop;    /**< Horizontal dilution of precision, or -1. */
  double pdop;    /**< Position dilution of precision, or -1. */
  double vdop;    /**< Vertical dilution of precision, or -1. */
  int satellites_used; /**< Satellites used for fix, or -1. */
  int satellites_view; /**< Satellites in view, or -1. */
  int fix_mode;        /**< Modem-specific fix mode/status, or -1. */
  char utc[24];        /**< UTC text from modem when available, else empty. */
} hal_simcom_a76xx_gnss_location_t;

/**
 * @brief SSL/TLS profile for MQTT connections.
 *
 * Applied via AT+CSSLCFG before AT+CMQTTSSLCFG.
 */
typedef struct {
  bool enabled;             /**< Enable SSL/TLS for the MQTT connection. */
  int ssl_context_id;       /**< 0..1, default 0 (SimCom limit). */
  const char *ca_cert_name; /**< Name of the CA cert previously uploaded with
                               AT+CCERTDOWN, or NULL. */
  bool
      ignore_local_time; /**< Skip cert expiry check if modem clock is unset. */
  bool enable_sni;       /**< Send TLS SNI extension. */
  int sslversion;        /**< 0..4 (default 4 = TLS 1.2). */
  int authmode;          /**< 0..3 (default 1 = server only). */
} hal_simcom_a76xx_ssl_config_t;

/**
 * @brief Configuration for hal_simcom_a76xx_mqtt_connect().
 */
typedef struct {
  const char *broker_host; /**< Hostname or IP literal. Must not be NULL. */
  uint16_t broker_port;    /**< TCP port (typically 1883 / 8883). */
  const char *client_id;   /**< MQTT client identifier. Must not be NULL. */
  const char *username;    /**< Optional MQTT username, or NULL. */
  const char *password;    /**< Optional MQTT password, or NULL. */
  uint16_t keepalive_s;    /**< Keep-alive interval in seconds. */
  bool clean_session;      /**< clean-session flag. */
  int client_index;        /**< CMQTT client index, 0..1 (SimCom limit). */
  hal_simcom_a76xx_ssl_config_t ssl;
} hal_simcom_a76xx_mqtt_config_t;

/**
 * @brief Sentinel returned when no CMQTTCONNECT result code is available.
 */
#define HAL_SIMCOM_A76XX_MQTT_RESULT_UNKNOWN (-1)

/**
 * @brief Callback signature for incoming MQTT subscribe messages.
 *
 * Invoked from hal_simcom_a76xx_mqtt_poll() once a full message
 * (CMQTTRXSTART/TOPIC/PAYLOAD/END URC sequence) has been reassembled.
 * The buffers are owned by the driver and remain valid only for the
 * duration of the call - copy what you need before returning.
 *
 * @param client_index  CMQTT client index that received the message.
 * @param topic         NUL-terminated topic string.
 * @param payload       Payload bytes (NOT NUL-terminated; payload_len gives the
 * length).
 * @param payload_len   Number of payload bytes.
 * @param user          Opaque user pointer registered alongside the callback.
 */
typedef void (*hal_simcom_a76xx_mqtt_message_cb_t)(int client_index,
                                                   const char *topic,
                                                   const uint8_t *payload,
                                                   size_t payload_len,
                                                   void *user);

/**
 * @brief Allocate and initialise a driver instance.
 *
 * The function does not touch the modem in any way - it only constructs
 * the AT engine and validates inputs. Call hal_simcom_a76xx_power_toggle()
 * and/or hal_simcom_a76xx_wait_boot() + hal_simcom_a76xx_init() afterwards.
 *
 * @param cfg Configuration. Must not be NULL. uart / rx_buf must be valid.
 * @return Handle on success, NULL on invalid arguments or pool exhaustion.
 */
hal_simcom_a76xx_t
hal_simcom_a76xx_create(const hal_simcom_a76xx_config_t *cfg);

/**
 * @brief Release the driver handle and the underlying AT engine.
 *
 * The UART and rx_buf are NOT freed; they remain the caller's property.
 */
void hal_simcom_a76xx_destroy(hal_simcom_a76xx_t h);

/**
 * @brief Drive the configured pwr_pin LOW for @p pulse_ms, then HIGH.
 *
 * Idle HIGH -> active-LOW pulse -> HIGH. Works for any board whose
 * power-control input matches that polarity:
 *   - PWRKEY through a transistor (standard SimCom wiring), or
 *   - a relay / load switch whose ENABLE is HIGH-active-when-powered
 *     (a single pulse becomes a complete physical power-cycle).
 *
 * No-op (returns OK) if pwr_pin was -1 in the configuration. For
 * boards with inverted polarity or more complex power sequencing,
 * leave pwr_pin = -1 and drive the GPIO from application code.
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_power_toggle(hal_simcom_a76xx_t h,
                                                        uint32_t pulse_ms);

/**
 * @brief Issue two PWRKEY pulses separated by a short delay, used to
 *        force-reboot a SimCom module that is stuck.
 *
 * Equivalent to power_toggle(1500) + 5s + power_toggle(1500) + 5s.
 * This sequence implements the SimCom PWRKEY "force off, then back
 * on" pattern; the modem is expected to keep running between the two
 * pulses (PWRKEY toggles the PMU state machine - it does NOT cut
 * VCC). Does nothing if pwr_pin is -1.
 *
 * Boards where pwr_pin physically gates VCC (relay / load switch)
 * usually do NOT want this - a single power_toggle() already cuts and
 * re-applies power. Use power_toggle() once and skip hard_reset().
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_hard_reset(hal_simcom_a76xx_t h);

/**
 * @brief Passive listener for the boot URC sequence.
 *
 * During cold boot the A76xx emits *ATREADY, +CPIN: READY, SMS DONE,
 * PB DONE (firmware-dependent order). Issuing AT commands while these
 * are in flight causes them to bleed into command responses. This
 * function blocks until any of:
 *   - "PB DONE" or "SMS DONE" is seen (preferred),
 *   - "*ATREADY" or "+CPIN: READY" is seen, followed by >= 2 s silence,
 *   - the RX stream is quiet for >= 3 s after any data was received.
 *
 * Returns HAL_SIMCOM_A76XX_TIMEOUT after @p total_timeout_ms with no
 * resolution; the caller is free to proceed anyway (the AT/OK retry
 * loop in hal_simcom_a76xx_init() will recover).
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_boot(hal_simcom_a76xx_t h,
                                                     uint32_t total_timeout_ms);

/**
 * @brief AT handshake + basic post-boot configuration.
 *
 * Sequence (each step retried as appropriate):
 *   1. up to 10x "AT" / "OK" handshake,
 *   2. ATE0  (disable echo),
 *   3. AT+CLTS=1 (best-effort, ignored on failure),
 *   4. AT+CEREG=0.
 *
 * Does not touch SIM / network / PDP - those are exposed separately so
 * the caller can interleave LED updates, watchdog kicks, etc.
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_init(hal_simcom_a76xx_t h);

/**
 * @brief Wait for AT+CPIN? to report READY.
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_sim_ready(hal_simcom_a76xx_t h,
                                                          uint32_t timeout_ms);

/**
 * @brief Poll AT+CREG? until the modem is registered to a network
 *        (home or roaming).
 */
hal_simcom_a76xx_result_t
hal_simcom_a76xx_wait_network_registered(hal_simcom_a76xx_t h,
                                         uint32_t timeout_ms);

/**
 * @brief Configure APN and activate PDP context #1.
 *
 * Issues AT+CGDCONT=1,"IP","<apn>" followed by AT+CGACT=1,1.
 */
hal_simcom_a76xx_result_t
hal_simcom_a76xx_attach_pdp(hal_simcom_a76xx_t h,
                            const hal_simcom_a76xx_apn_t *apn);

/**
 * @brief Query the modem RTC (AT+CCLK?) and format the result as ISO 8601
 *        with UTC offset, e.g. "2024-03-21T14:30:00+02:00".
 *
 * @param h        Handle.
 * @param out      Output buffer. Must hold at least 26 bytes.
 * @param out_size Capacity of @p out.
 * @return HAL_SIMCOM_A76XX_OK on success, HAL_SIMCOM_A76XX_PARSE if the
 *         modem reply could not be decoded (e.g. RTC not yet synced).
 */
hal_simcom_a76xx_result_t
hal_simcom_a76xx_get_network_time_iso8601(hal_simcom_a76xx_t h, char *out,
                                          size_t out_size);

/**
 * @brief Query coarse cellular location (LBS) via AT+CLBS.
 *
 * Sends @c AT+CLBS=1,1 and parses a successful @c +CLBS line to fill
 * @p out_location.
 *
 * Typical successful modem response shape:
 * @code
 * +CLBS: 0,<lat>,<lon>,<accuracy>
 * @endcode
 *
 * @param h            Handle.
 * @param out_location Destination for parsed location data.
 * @param timeout_ms   Command timeout in ms (0 = driver default).
 * @return HAL_SIMCOM_A76XX_OK on success,
 *         HAL_SIMCOM_A76XX_PARSE when reply is present but malformed,
 *         or a mapped modem error/timeout result otherwise.
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_get_cell_location(
    hal_simcom_a76xx_t h, hal_simcom_a76xx_cell_location_t *out_location,
    uint32_t timeout_ms);

/**
 * @brief Initialise a GNSS location structure with sentinel defaults.
 *
 * Numeric optional fields are set to -1, coordinates to 0, and utc to empty.
 */
void hal_simcom_a76xx_gnss_location_init(hal_simcom_a76xx_gnss_location_t *loc);

/**
 * @brief Power on the modem's GNSS receiver.
 *
 * Tries the common A76xx command variants in order:
 * @c AT+CGNSSPWR=1, @c AT+CGNSSPWR=1,1, @c AT+CGNSPWR=1,
 * @c AT+CGPS=1,1, @c AT+CGPS=1.
 *
 * @param h          Handle.
 * @param timeout_ms Per-command timeout in ms (0 = driver default).
 * @return HAL_SIMCOM_A76XX_OK when one variant succeeds; mapped modem
 *         error/timeout otherwise. A handle remembers a successful power-on.
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_gnss_power_on(hal_simcom_a76xx_t h,
                                                         uint32_t timeout_ms);

/**
 * @brief Return whether GNSS has been powered on successfully for this handle.
 */
bool hal_simcom_a76xx_gnss_is_powered(hal_simcom_a76xx_t h);

/**
 * @brief Query the modem GNSS fix.
 *
 * Ensures GNSS is powered, then tries @c AT+CGNSSINFO, @c AT+CGNSINF and
 * @c AT+CGPSINFO. Returns @c HAL_SIMCOM_A76XX_NOT_READY when the modem
 * responds but has no fix yet.
 *
 * @param h            Handle.
 * @param out_location Destination for parsed fix data.
 * @param timeout_ms   Per-query timeout in ms (0 = driver default).
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_get_gnss_location(
    hal_simcom_a76xx_t h, hal_simcom_a76xx_gnss_location_t *out_location,
    uint32_t timeout_ms);

/**
 * @brief Return the underlying AT engine handle for advanced/raw use.
 *
 * Useful for issuing vendor commands not yet wrapped by this driver
 * (e.g. AT+CIPSEND for raw TCP, AT+CHTTP* for HTTP). The driver remains
 * the owner; do NOT call hal_modem_at_destroy() on the returned handle.
 */
hal_modem_at_t hal_simcom_a76xx_get_at(hal_simcom_a76xx_t h);

/* ===================== MQTT ===================== */

/**
 * @brief Tear down any previous CMQTT session and establish a fresh one.
 *
 * Sequence: CMQTTDISC / CMQTTREL / CMQTTSTOP (cleanup, errors ignored),
 * optional CSSLCFG block when @p cfg->ssl.enabled, CMQTTSTART,
 * CMQTTACCQ, CMQTTSSLCFG (when SSL), CMQTTCONNECT.
 *
 * On success, the driver also installs URC handlers for the
 * +CMQTTRXSTART / RXTOPIC / RXPAYLOAD / RXEND family so that
 * hal_simcom_a76xx_mqtt_poll() can reassemble incoming messages.
 */
hal_simcom_a76xx_result_t
hal_simcom_a76xx_mqtt_connect(hal_simcom_a76xx_t h,
                              const hal_simcom_a76xx_mqtt_config_t *cfg);

/**
 * @brief Return the human-readable meaning of a SimCom CMQTT result code.
 *
 * The returned static string follows the A76xx MQTT(S) application-note
 * result table. Unknown values return "unknown MQTT result".
 */
const char *hal_simcom_a76xx_mqtt_result_string(int result_code);

/**
 * @brief Return the most recent CMQTTCONNECT result for a client.
 *
 * @return 0 on the last successful connect, a positive SimCom result code on
 *         a rejected/failed connect, or
 *         @ref HAL_SIMCOM_A76XX_MQTT_RESULT_UNKNOWN when no result URC has
 *         been received (also returned for an invalid handle/index).
 */
int hal_simcom_a76xx_mqtt_last_connect_result(hal_simcom_a76xx_t h,
                                              int client_index);

/**
 * @brief Disconnect the named CMQTT client and release its slot.
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_disconnect(hal_simcom_a76xx_t h,
                                                           int client_index);

/**
 * @brief Publish a binary or text payload to @p topic.
 *
 * @param qos  0..2.
 */
hal_simcom_a76xx_result_t
hal_simcom_a76xx_mqtt_publish(hal_simcom_a76xx_t h, int client_index,
                              const char *topic, const void *payload,
                              size_t payload_len, int qos);

/**
 * @brief Subscribe to a topic filter.
 *
 * Uses the AT+CMQTTSUBTOPIC + AT+CMQTTSUB pair (topic provided in two
 * steps; CMQTTSUB is then a single command). Incoming messages will
 * be delivered to the message callback installed via
 * hal_simcom_a76xx_mqtt_set_message_callback() each time
 * hal_simcom_a76xx_mqtt_poll() (or any other driver entry point that
 * drains the UART) observes a complete CMQTTRXSTART..RXEND sequence.
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_subscribe(hal_simcom_a76xx_t h,
                                                          int client_index,
                                                          const char *topic,
                                                          int qos);

/**
 * @brief Cancel a previous subscription using AT+CMQTTUNSUB.
 */
hal_simcom_a76xx_result_t
hal_simcom_a76xx_mqtt_unsubscribe(hal_simcom_a76xx_t h, int client_index,
                                  const char *topic);

/**
 * @brief Install / remove the callback that receives reassembled
 *        incoming MQTT messages.
 *
 * Passing cb == NULL disables delivery (incoming messages are still
 * consumed off the UART; they just have nowhere to go).
 */
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_set_message_callback(
    hal_simcom_a76xx_t h, hal_simcom_a76xx_mqtt_message_cb_t cb, void *user);

/**
 * @brief Drain pending CMQTT RX URCs, dispatching complete messages
 *        to the registered callback.
 *
 * Safe to call from the main loop. Internally calls
 * hal_modem_at_urc_poll(); returns the number of complete messages
 * dispatched on this invocation.
 */
int hal_simcom_a76xx_mqtt_poll(hal_simcom_a76xx_t h);

/**
 * @brief Best-effort connection-state probe.
 *
 * Returns the cached MQTT-connected flag maintained by the driver
 * (set on successful CMQTTCONNECT, cleared on CMQTTDISC / disconnect
 * URC / explicit driver-side failure).
 */
bool hal_simcom_a76xx_mqtt_is_connected(hal_simcom_a76xx_t h, int client_index);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_A7670 */
