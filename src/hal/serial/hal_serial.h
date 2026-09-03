#pragma once

/**
 * @file hal_serial.h
 * @brief Hardware abstraction for serial console I/O and debug logging.
 *
 * One target-independent core owns formatting, synchronization, rate limiting
 * and ISR deferral. A small link-time port routes bytes to RP USB CDC, the
 * ESP-IDF startup-owned USB Serial/JTAG console VFS, STM32 USART2/host stdout,
 * or the mock capture/RX transport.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Legacy debug buffer size used by bounded mock capture/RX helpers.
 *
 * Task-level hal_deb() / hal_derr() output is streamed by the shared core and
 * is not capped by this value. ISR-deferred records remain bounded separately
 * by @ref HAL_DEBUG_ISR_TEXT_MAX.
 */
#ifndef HAL_DEBUG_BUF_SIZE
#define HAL_DEBUG_BUF_SIZE 1024
#endif

/** @brief Maximum length of the debug prefix string. */
#ifndef HAL_DEBUG_PREFIX_SIZE
#define HAL_DEBUG_PREFIX_SIZE 16
#endif

/** @brief Maximum number of independent error sources tracked by rate limiter.
 */
#ifndef HAL_DEBUG_RATE_LIMIT_SOURCES_MAX
#define HAL_DEBUG_RATE_LIMIT_SOURCES_MAX 16
#endif

/** @brief Maximum stored length of an error source label. */
#ifndef HAL_DEBUG_RATE_LIMIT_SOURCE_NAME_MAX
#define HAL_DEBUG_RATE_LIMIT_SOURCE_NAME_MAX 24
#endif

/**
 * @brief Slot count of the shared core's SPSC ring buffer that holds debug
 *        records produced from interrupt context (drained by
 *        @ref hal_debug_loop). Must be >= 2; the effective capacity is
 *        slot_count - 1 because one slot is reserved to distinguish full
 *        from empty.
 */
#ifndef HAL_DEBUG_ISR_SLOT_COUNT
#define HAL_DEBUG_ISR_SLOT_COUNT 64u
#endif

/**
 * @brief Maximum stored payload length (including NUL terminator) of a
 *        single ISR-deferred debug record.
 */
#ifndef HAL_DEBUG_ISR_TEXT_MAX
#define HAL_DEBUG_ISR_TEXT_MAX 160u
#endif

/**
 * @brief Configuration for rate-limiting repeated noncritical debug errors.
 */
typedef struct {
  uint16_t full_logs_limit;  /**< Number of full error logs before suppression
                                starts. */
  uint32_t min_gap_ms;       /**< Minimum time gap between full logs. */
  uint32_t summary_every_ms; /**< Interval for suppressed-count summaries. */
} hal_debug_rate_limit_t;

/**
 * @brief Callback used to provide a textual timestamp for error logs.
 *
 * Return true and fill @p out with a null-terminated label to enable prefixing
 * log lines. Return false to print logs without timestamp.
 */
typedef bool (*hal_debug_timestamp_hook_t)(char *out, size_t out_size,
                                           void *user);

/**
 * @brief Return default rate-limit configuration.
 *
 * Defaults:
 * - full_logs_limit = 5
 * - min_gap_ms = 1000
 * - summary_every_ms = 30000
 */
hal_debug_rate_limit_t hal_debug_rate_limit_defaults(void);

/**
 * @brief Get current global rate-limit configuration.
 * @return Pointer to internal read-only config.
 */
const hal_debug_rate_limit_t *hal_debug_get_rate_limit(void);

/**
 * @brief Register an optional timestamp hook for hal_derr()/hal_derr_limited().
 *
 * Pass nullptr to disable timestamp prefixing.
 *
 * @param hook Callback that formats timestamp text.
 * @param user Opaque pointer forwarded to @p hook.
 */
void hal_debug_set_timestamp_hook(hal_debug_timestamp_hook_t hook, void *user);

/**
 * @brief Initialise the hardware serial port.
 * @param baud Baud rate (e.g. 115200).
 */
void hal_serial_begin(uint32_t baud);

/**
 * @brief Enable or disable flushing after serial writes.
 *
 * RP USB-CDC backends default to disabled. When enabled, they perform an
 * additional @ref hal_usb_cdc_flush after hal_serial_print() /
 * hal_serial_println() while still holding the serial TX mutex.
 *
 * Backends without a USB CDC flush still accept the setting for portable code,
 * but it has no transport effect there.
 *
 * @param enabled true to flush after writes, false to skip flush.
 */
void hal_serial_set_flush(bool enabled);

/**
 * @brief Print a string to the serial console (no newline).
 * @param s Null-terminated string.
 */
void hal_serial_print(const char *s);

/**
 * @brief Print a string followed by a newline to the serial console.
 * @param s Null-terminated string.
 */
void hal_serial_println(const char *s);

/**
 * @brief Return the number of bytes available for reading from the serial port.
 * @return Number of bytes in the receive buffer, or 0 if none.
 */
int hal_serial_available(void);

/**
 * @brief Read one byte from the serial port.
 * @return The first byte of incoming data (0-255), or -1 if nothing available.
 */
int hal_serial_read(void);

/**
 * @brief Initialise the debug output subsystem.
 *
 * If never called explicitly, the first call to hal_deb() / hal_derr()
 * triggers atomically gated lazy initialisation with
 * @ref HAL_DEBUG_DEFAULT_BAUD.
 *
 * @param baud Baud rate for the debug serial port.
 * @param cfg  Optional rate-limit configuration for noncritical repeated
 *             errors. Pass nullptr / 0 to use defaults.
 */
#ifdef __cplusplus
void hal_debug_init(uint32_t baud, const hal_debug_rate_limit_t *cfg = 0);
#else
void hal_debug_init(uint32_t baud, const hal_debug_rate_limit_t *cfg);
#endif

/** @brief Initialise debug output with HAL_DEBUG_DEFAULT_BAUD and defaults. */
void hal_debug_init_default(void);

/**
 * @brief Set a module prefix followed by a colon.
 *
 * The module name is truncated when needed to fit HAL_DEBUG_PREFIX_SIZE.
 * A NULL argument leaves the current prefix unchanged.
 */
void hal_debug_set_module_prefix(const char *module_name);

/**
 * @brief Check whether the debug subsystem has been initialised.
 * @return true if hal_debug_init() has been called (explicitly or via lazy
 * init).
 */
bool hal_deb_is_initialized(void);

/**
 * @brief Enable or disable debug log emission from hal_deb()/hal_derr().
 *
 * When muted, debug helpers return immediately and do not emit text to the
 * underlying serial output. Protocol traffic sent with hal_serial_print*()
 * is unaffected.
 *
 * @param muted true to suppress debug logs, false to allow them.
 */
void hal_debug_set_muted(bool muted);

/**
 * @brief Check whether debug log emission is currently muted.
 * @return true when debug output is muted.
 */
bool hal_debug_is_muted(void);

/**
 * @brief Set the prefix prepended to every hal_deb() / hal_derr() message.
 * @param prefix Null-terminated prefix string (max HAL_DEBUG_PREFIX_SIZE-1
 * chars).
 */
void hal_deb_set_prefix(const char *prefix);

/**
 * @brief Print a formatted debug message (printf-style) to the serial console.
 * @param format printf-compatible format string.
 */
void hal_deb(const char *format, ...);

/**
 * @brief Print a formatted error message (printf-style) to the serial console.
 * @param format printf-compatible format string.
 */
void hal_derr(const char *format, ...);

/**
 * @brief Print a rate-limited error message grouped by source tag.
 *
 * Uses the global rate-limit config from hal_debug_init(..., cfg).
 * Suppression and summaries are tracked independently per @p source.
 *
 * @param source Caller-defined short source tag (free-form, no predefined
 * enum), e.g. "gps", "can", "i2c". nullptr / 0 becomes "global".
 * @param format printf-compatible format string.
 */
void hal_derr_limited(const char *source, const char *format, ...);

/**
 * @brief Log a hex dump of a byte buffer via hal_deb().
 * @param prefix   Label printed before the hex bytes.
 * @param buf      Data buffer.
 * @param len      Total number of bytes in buf.
 * @param maxBytes Maximum number of bytes to display (clamped to 1..48).
 */
void hal_deb_hex(const char *prefix, const uint8_t *buf, int len, int maxBytes);

/* Established short spellings used by firmware. */
#ifndef deb
#define deb hal_deb
#endif
#ifndef derr
#define derr hal_derr
#endif
#ifndef derr_limited
#define derr_limited hal_derr_limited
#endif

/**
 * @brief Flush debug records that were captured from interrupt context.
 *
 * When hal_deb(), hal_derr() or hal_derr_limited() are invoked from an
 * ISR (detected via hal_in_isr()), the formatted message is enqueued in
 * an internal SPSC ring buffer instead of touching the UART. This
 * function drains that buffer and emits each captured record using the
 * normal (mutex-protected) serial path.
 *
 * Call this periodically from the main loop / a low-priority task.
 * Calling it from ISR context is a no-op (the drain is never re-entered
 * from an interrupt).
 *
 * If any ISR records were dropped because the ring was full since the
 * previous drain, a single summary line of the form
 * "ERROR! [ISR] dropped N debug message(s)" is emitted after the
 * regular records and the internal drop counter is reset to zero.
 *
 * When debug output is muted (see hal_debug_set_muted()), pending
 * records are discarded silently and the drop counter is cleared.
 *
 * Safe from the very first iteration of the main loop, even when no
 * hal_debug_init() / hal_deb() / hal_derr() has been called yet: the
 * function performs the same atomically gated lazy init as hal_deb()
 * on the emit path and uses only zero-initialised statics on the in-ISR / muted
 * short circuits.
 */
void hal_debug_loop(void);

#ifdef __cplusplus
}
#endif
