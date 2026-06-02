#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_CELLULAR_MODEM

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "hal_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_modem_at.h
 * @brief Generic AT-command engine for cellular / serial-AT modems.
 *
 * This module is the transport-level layer of the JaszczurHAL cellular
 * modem stack. It owns the UART, the receive buffer and the protocol
 * state, and exposes a small set of primitives that higher-level
 * modem drivers (e.g. SimCom A76xx, Quectel BGxx) compose into product
 * behaviour:
 *
 *  - hal_modem_at_send()           - one-shot AT command, wait for
 *                                    OK/ERROR/expected-substring
 *  - hal_modem_at_send_with_data() - three-phase command (cmd -> '>'
 *                                    prompt -> binary/text payload -> OK)
 *  - hal_modem_at_listen_until()   - passive listener that drains the
 *                                    RX stream until a user-supplied
 *                                    predicate returns true or a quiet
 *                                    window elapses (boot/URC waiting)
 *  - URC router                    - prefix-keyed dispatch of Unsolicited
 *                                    Result Codes to user callbacks
 *
 * Every handle is independently usable from multiple threads; the
 * implementation serialises access internally with a hal_mutex per
 * instance (mirrors hal_can / hal_mqtt convention).
 *
 * The engine is intentionally vendor-agnostic. Vendor-specific behaviour
 * (PWRKEY pulse shape, boot URC vocabulary, MQTT-over-AT command grammar,
 * SIM/network state machine, etc.) lives in a driver under
 * `impl/<backend>/drivers/modem/<family>/`.
 */


/**
 * @brief Result of an AT-engine operation.
 */
typedef enum {
    HAL_MODEM_AT_OK = 0,        /**< Modem replied "OK" (or expected substring). */
    HAL_MODEM_AT_ERROR,         /**< Modem replied "ERROR" or "+CME ERROR" / "+CMS ERROR". */
    HAL_MODEM_AT_TIMEOUT,       /**< No terminating response within the timeout. */
    HAL_MODEM_AT_NO_PROMPT,     /**< send_with_data: '>' prompt never arrived. */
    HAL_MODEM_AT_INVALID_ARG,   /**< NULL handle, NULL command, etc. */
    HAL_MODEM_AT_BUSY           /**< Another thread is currently holding the engine. */
} hal_modem_at_result_t;

/**
 * @brief Opaque handle for a single AT-engine instance.
 *
 * One handle per physical modem (i.e. per UART). Obtain via
 * hal_modem_at_create(); release with hal_modem_at_destroy().
 */
typedef struct hal_modem_at_impl_s hal_modem_at_impl_t;
typedef hal_modem_at_impl_t *hal_modem_at_t;

/**
 * @brief Configuration for hal_modem_at_create().
 */
typedef struct {
    /** UART previously created and begun by the caller. Must not be NULL. */
    hal_uart_t uart;

    /**
     * Caller-owned scratch buffer used by the engine to accumulate the
     * current response. Must outlive the handle. Recommended size:
     * 512 bytes for typical MQTT/SMS workloads, 2048+ for HTTP bodies.
     */
    char *rx_buf;

    /** Capacity of @p rx_buf in bytes (>= 64). */
    size_t rx_buf_size;

    /**
     * Default per-command timeout (ms) used by hal_modem_at_send()
     * when the caller passes 0. Typical: 1000-2000 ms.
     */
    uint32_t default_timeout_ms;

    /**
     * Quiet-window length used by hal_modem_at_listen_until() to decide
     * the stream has settled. Typical: 200-500 ms.
     */
    uint32_t quiet_window_ms;
} hal_modem_at_config_t;

/**
 * @brief Signature of a URC handler.
 *
 * @param line   Full unsolicited line received from the modem, NUL-terminated,
 *               trailing CR/LF already stripped.
 * @param user   Opaque user pointer passed to hal_modem_at_urc_register().
 */
typedef void (*hal_modem_at_urc_cb_t)(const char *line, void *user);

/**
 * @brief Predicate used by hal_modem_at_listen_until().
 *
 * Called every time new bytes are appended to the internal scratch buffer.
 * Return true to stop listening (success). The buffer contents (including
 * everything received so far) are accessible via hal_modem_at_last_response().
 *
 * @param buf   Current contents of the scratch buffer (NUL-terminated).
 * @param len   Number of valid bytes in @p buf (excluding the NUL).
 * @param user  Opaque user pointer passed to hal_modem_at_listen_until().
 */
typedef bool (*hal_modem_at_ready_cb_t)(const char *buf, size_t len, void *user);

/**
 * @brief Create and initialise an AT-engine instance.
 *
 * Does not power-cycle the modem or send any bytes. The caller is
 * expected to have already configured and begun the UART.
 *
 * @param cfg Configuration block. Must not be NULL. All required fields
 *            (uart, rx_buf, rx_buf_size) must be valid.
 * @return Handle on success, NULL on invalid configuration or pool exhaustion.
 */
hal_modem_at_t hal_modem_at_create(const hal_modem_at_config_t *cfg);

/**
 * @brief Release all resources associated with the handle.
 *
 * The associated UART is NOT closed; that is the caller's responsibility.
 *
 * @param h Handle obtained from hal_modem_at_create(). Must not be used after this call.
 */
void hal_modem_at_destroy(hal_modem_at_t h);

/**
 * @brief Send a single AT command and wait for a terminating response.
 *
 * Writes `cmd` followed by CR-LF to the UART, then drains the RX stream
 * until one of the following happens:
 *   - the modem replies "OK"             -> returns HAL_MODEM_AT_OK
 *   - the modem replies "ERROR" / "+CME ERROR" / "+CMS ERROR"
 *                                        -> returns HAL_MODEM_AT_ERROR
 *   - @p expected (when non-NULL) appears in the response
 *                                        -> returns HAL_MODEM_AT_OK
 *   - @p timeout_ms elapses              -> returns HAL_MODEM_AT_TIMEOUT
 *
 * The full response (including any intermediate URC lines that arrived
 * during the wait) is available via hal_modem_at_last_response().
 *
 * @param h           Handle.
 * @param cmd         AT command without trailing CR/LF (e.g. "AT+CGMI"). Must not be NULL.
 * @param expected    Optional substring that signals success early (NULL to use OK/ERROR only).
 * @param timeout_ms  Timeout in ms (0 = use default_timeout_ms from config).
 * @return One of hal_modem_at_result_t.
 */
hal_modem_at_result_t hal_modem_at_send(hal_modem_at_t h,
                                        const char *cmd,
                                        const char *expected,
                                        uint32_t timeout_ms);

/**
 * @brief Send a three-phase command: header, wait for prompt, then payload.
 *
 * Used for modem commands that accept binary or arbitrary text data,
 * e.g. AT+CMQTTPAYLOAD on SimCom A76xx or AT+QMTPUB on Quectel BGxx.
 *
 * Sequence:
 *   1. Write `cmd` + CR-LF.
 *   2. Wait up to @p prompt_timeout_ms for a '>' character. If it does
 *      not appear, return HAL_MODEM_AT_NO_PROMPT.
 *   3. Write `data` (@p data_len bytes, no terminator added).
 *   4. Wait up to @p resp_timeout_ms for OK/ERROR (no flush in between -
 *      the OK that arrives between phases must not be lost).
 *
 * @param h                  Handle.
 * @param cmd                AT command without trailing CR/LF. Must not be NULL.
 * @param data               Payload bytes (may contain any byte value, including 0x00).
 *                           Must not be NULL when data_len > 0.
 * @param data_len           Number of payload bytes.
 * @param prompt_timeout_ms  Max time to wait for '>' (typical: 1000 ms).
 * @param resp_timeout_ms    Max time to wait for OK/ERROR after payload (typical: 5000 ms).
 * @return One of hal_modem_at_result_t.
 */
hal_modem_at_result_t hal_modem_at_send_with_data(hal_modem_at_t h,
                                                  const char *cmd,
                                                  const uint8_t *data,
                                                  size_t data_len,
                                                  uint32_t prompt_timeout_ms,
                                                  uint32_t resp_timeout_ms);

/**
 * @brief Passive listener that drains the RX stream until a predicate fires
 *        or a quiet window elapses.
 *
 * Used during modem warmup, to wait for boot URCs (e.g. "PB DONE",
 * "*ATREADY", "+CPIN: READY") without issuing any AT command. Every time
 * new bytes arrive the predicate @p ready is called; if it returns true
 * the listener stops with HAL_MODEM_AT_OK. If no new bytes arrive for
 * the configured quiet_window_ms AND the predicate has returned true at
 * least once previously, the listener also stops with HAL_MODEM_AT_OK.
 *
 * If @p ready is NULL the listener simply waits for the quiet window
 * (i.e. "stream has fully settled") and returns HAL_MODEM_AT_OK, or
 * HAL_MODEM_AT_TIMEOUT if @p total_timeout_ms elapses first.
 *
 * @param h                 Handle.
 * @param ready             Optional predicate. May be NULL.
 * @param user              Opaque pointer passed to @p ready.
 * @param total_timeout_ms  Hard upper bound on the total wait (typical: 15000 ms).
 * @return HAL_MODEM_AT_OK when the predicate fired or the stream settled,
 *         HAL_MODEM_AT_TIMEOUT otherwise.
 */
hal_modem_at_result_t hal_modem_at_listen_until(hal_modem_at_t h,
                                                hal_modem_at_ready_cb_t ready,
                                                void *user,
                                                uint32_t total_timeout_ms);

/**
 * @brief Same as hal_modem_at_listen_until() but does NOT discard the
 *        existing scratch buffer first.
 *
 * Use this when the previous call (typically hal_modem_at_send()) returned
 * with a partial response in the buffer — for example, an `expected`
 * substring matched in the middle of a URC line whose payload was split
 * across UART writes — and you need to keep collecting the tail of that
 * line. Bytes already in the buffer are preserved; new bytes are appended.
 * If @p ready is non-NULL it is invoked once on the existing content
 * before any new bytes are drained, so a predicate that is already
 * satisfied returns immediately after the configured quiet window.
 *
 * @param h                 Handle.
 * @param ready             Optional predicate. May be NULL.
 * @param user              Opaque pointer passed to @p ready.
 * @param total_timeout_ms  Hard upper bound on the total wait.
 * @return HAL_MODEM_AT_OK when the predicate fired or the stream went
 *         quiet, HAL_MODEM_AT_TIMEOUT otherwise.
 */
hal_modem_at_result_t hal_modem_at_listen_more(hal_modem_at_t h,
                                               hal_modem_at_ready_cb_t ready,
                                               void *user,
                                               uint32_t total_timeout_ms);

/**
 * @brief Pointer to the NUL-terminated buffer holding the response of the
 *        most recent successful or failed operation.
 *
 * Valid until the next call that writes to the engine on the same handle.
 *
 * @param h Handle.
 * @return Pointer to the rx_buf provided in the config, or NULL if @p h
 *         is NULL.
 */
const char *hal_modem_at_last_response(hal_modem_at_t h);

/**
 * @brief Register a URC handler for lines starting with a given prefix.
 *
 * URC handlers are invoked from hal_modem_at_urc_poll() and from inside
 * the send / send_with_data / listen_until paths whenever a line that
 * matches @p prefix is observed in the RX stream. Matching is by exact
 * left-anchored substring (case-sensitive).
 *
 * Calling this with @p cb == NULL removes any existing handler for the
 * given prefix.
 *
 * @param h       Handle.
 * @param prefix  Line prefix to match (e.g. "+CMQTTRXSTART:"). Must not be NULL.
 * @param cb      Handler callback, or NULL to unregister.
 * @param user    Opaque pointer passed to @p cb.
 * @return true on success, false if @p h or @p prefix is NULL, or the
 *         per-handle URC table is full.
 */
bool hal_modem_at_urc_register(hal_modem_at_t h,
                               const char *prefix,
                               hal_modem_at_urc_cb_t cb,
                               void *user);

/**
 * @brief Drain any URC lines currently sitting in the UART RX buffer.
 *
 * Intended to be called from the application's main loop when no
 * command is in flight, so that asynchronous events (incoming MQTT
 * messages, network registration changes, SMS notifications) are
 * dispatched promptly.
 *
 * @param h Handle.
 * @return Number of URC lines processed (zero if the UART was empty
 *         or no registered prefix matched).
 */
int hal_modem_at_urc_poll(hal_modem_at_t h);

/**
 * @brief Install a filter that redacts sensitive substrings from debug logs.
 *
 * When the engine logs traffic via hal_deb(), every occurrence of any
 * registered secret is replaced with "***". Useful for hiding APN
 * passwords, MQTT credentials, ICCID, IMSI, etc.
 *
 * The engine keeps a pointer to @p secrets; the array (and the strings
 * it points to) must outlive the handle. Pass count = 0 to remove any
 * previously installed filter.
 *
 * @param h        Handle.
 * @param secrets  Array of NUL-terminated strings to redact, or NULL.
 * @param count    Number of entries in @p secrets.
 */
void hal_modem_at_set_log_filter(hal_modem_at_t h,
                                 const char *const *secrets,
                                 size_t count);

/**
 * @brief Install a raw per-line observer callback.
 *
 * Unlike prefix-keyed URC handlers (which fire only when a registered
 * prefix matches), the line observer is invoked for every assembled
 * line received from the modem: URCs, response bodies, and bare-text
 * lines such as the topic / payload sent by SimCom's CMQTTRX URC
 * family. Used by higher-level drivers that need to capture multi-line
 * unsolicited payloads.
 *
 * Pass @p cb == NULL to remove the observer.
 *
 * Only one observer per handle; subsequent calls replace the previous.
 */
void hal_modem_at_set_line_observer(hal_modem_at_t h,
                                    hal_modem_at_urc_cb_t cb,
                                    void *user);

/**
 * @brief Application "tick" callback invoked from inside blocking waits.
 *
 * Several engine entry points (hal_modem_at_send, _send_with_data,
 * _listen_until) and higher-level drivers built on top of the engine
 * (e.g. hal_simcom_a76xx) sit in short hal_delay_ms() polling loops
 * while waiting for the modem to respond. When a watchdog is active in
 * the application, those waits will trip it if they last longer than
 * the watchdog window.
 *
 * Register a tick callback to be invoked periodically (at least every
 * ~20 ms) from inside those waits. Typical use: feed the application
 * watchdog and refresh status indicators (LEDs).
 *
 * The callback runs with the engine mutex held — it must NOT call any
 * hal_modem_at_* / hal_simcom_a76xx_* function on the same handle.
 *
 * Pass @p cb == NULL to remove the callback.
 */
typedef void (*hal_modem_at_tick_cb_t)(void *user);

void hal_modem_at_set_tick_callback(hal_modem_at_t h,
                                    hal_modem_at_tick_cb_t cb,
                                    void *user);

/**
 * @brief Watchdog-friendly sleep helper for higher-level drivers.
 *
 * Sleeps for @p ms milliseconds in short slices, invoking the tick
 * callback (if installed via hal_modem_at_set_tick_callback) at the
 * start of every slice. Equivalent to hal_delay_ms(ms) when no tick is
 * installed, but never blocks for longer than ~20 ms without giving
 * the application a chance to feed its watchdog.
 *
 * Safe to call with @p h == NULL — degrades to a plain hal_delay_ms.
 */
void hal_modem_at_sleep_ms(hal_modem_at_t h, uint32_t ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HAL_ENABLE_CELLULAR_MODEM */

