#pragma once

/**
 * @file hal_system.h
 * @brief System-level HAL: timing, watchdog, idle, and shared utility helpers.
 *
 * Provides:
 * - Millisecond / microsecond counters (@ref hal_millis, @ref hal_micros,
 *   @ref hal_micros64) and busy-wait delays.
 * - Hardware watchdog control (@ref hal_watchdog_enable,
 *   @ref hal_watchdog_feed, @ref hal_watchdog_caused_reboot).
 * - Free-heap query and on-chip temperature sensor.
 * - Controlled reboot into RP2040 USB bootloader mode
 *   (@ref hal_enter_bootloader).
 * - Shared utility helpers that have no better home and must be visible to
 *   both C and C++ translation units:
 *   - @ref COUNTOF  — element count of a static array.
 *   - @ref hal_u32_to_bytes_be — 32-bit big-endian serialisation.
 *   - @ref NONULL   — null-pointer guard with goto-error idiom.
 *   - Type-conversion macros: @ref SECS, @ref MINS, @ref HOURS.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "hal_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -- Time-conversion macros ------------------------------------------------ */

#define SECONDS_IN_MINUTE 60
#define MILIS_IN_MINUTE   60000.0

/** @brief One second in milliseconds. */
#define SECOND 1000UL

/** @brief Convert seconds to milliseconds. */
#define SECS(t) ((unsigned long)((t) * SECOND))

/** @brief Convert minutes to milliseconds. */
#define MINS(t) (SECS(t) * 60UL)

/** @brief Convert hours to milliseconds. */
#define HOURS(t) (MINS(t) * 60UL)

/**
 * @def COUNTOF(arr)
 * @brief Number of elements in a statically-allocated array.
 * @note Works only for real arrays. Passing a pointer yields an incorrect result.
 */
#ifndef COUNTOF
#define COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/**
 * @brief Convert 32-bit value to 4-byte big-endian representation.
 * @param val Source value.
 * @param buf Destination buffer of at least 4 bytes.
 */
static inline void hal_u32_to_bytes_be(uint32_t val, uint8_t *buf) {
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val);
}

/**
 * @brief Return milliseconds since boot.
 * @return Millisecond counter (wraps after ~49 days).
 */
uint32_t hal_millis(void);

/**
 * @brief Return microseconds since boot (32-bit, wraps after ~71 min).
 * @return Microsecond counter.
 */
uint32_t hal_micros(void);

/**
 * @brief Return microseconds since boot (64-bit, no wrap concern).
 * @return Microsecond counter.
 */
uint64_t hal_micros64(void);

/**
 * @brief Busy-wait for the given number of milliseconds.
 * @param ms Delay duration.
 */
void hal_delay_ms(uint32_t ms);

/**
 * @brief Busy-wait for the given number of microseconds.
 * @param us Delay duration.
 */
void hal_delay_us(uint32_t us);

/**
 * @brief Feed (reset) the hardware watchdog timer.
 */
void hal_watchdog_feed(void);

/**
 * @brief Enable the hardware watchdog with the given timeout.
 * @param ms             Timeout in milliseconds.
 * @param pause_on_debug If true, pause the watchdog when a debugger is attached.
 */
void hal_watchdog_enable(uint32_t ms, bool pause_on_debug);

/**
 * @brief Check whether the last boot was caused by a watchdog reset.
 * @return true if watchdog caused the reboot.
 */
bool hal_watchdog_caused_reboot(void);

/**
 * @brief Yield to the system (cooperative multitasking / idle hook).
 */
void hal_idle(void);

/**
 * @brief Return the amount of free heap memory in bytes.
 * @return Free heap in bytes.
 */
uint32_t hal_get_free_heap(void);

/**
 * @brief Read the on-chip temperature sensor.
 *
 * On RP2040 this wraps the Arduino-pico @c analogReadTemp() function which
 * samples the internal ADC channel connected to the die temperature sensor
 * and converts the raw reading to degrees Celsius.
 *
 * The value is approximate (+/-2 C typical) and reflects the silicon
 * temperature, not the ambient air temperature.
 *
 * @return Die temperature in degrees Celsius as a floating-point value.
 */
float hal_read_chip_temp(void);

/**
 * @brief Reboot into USB bootloader mode (UF2 mass-storage mode).
 *
 * On RP2040 targets this calls the ROM bootloader entry path (BOOTSEL mode),
 * disconnecting the running application and exposing the USB UF2 flashing
 * interface.
 *
 * Typical use:
 * - acknowledge the host command first,
 * - ensure outputs are placed in a safe state,
 * - call this function as the final step.
 *
 * @note This function does not return on real hardware.
 * @note In mock/unit-test builds this is implemented as a non-resetting flag.
 */
void hal_enter_bootloader(void);

/**
 * @brief Length in bytes of the unique device identifier.
 */
#define HAL_DEVICE_UID_BYTES 8u

/**
 * @brief Minimum buffer size (including NUL) for the hex representation
 *        of the unique device identifier returned by
 *        @ref hal_get_device_uid_hex.
 */
#define HAL_DEVICE_UID_HEX_BUF_SIZE 17u

/**
 * @brief Read the 64-bit unique device identifier.
 *
 * On RP2040 hardware this wraps @c pico_get_unique_board_id() which reads the
 * 64-bit unique id stored in the external QSPI flash chip.
 *
 * On mock/unit-test builds this returns a deterministic pattern, overridable
 * via @c hal_mock_set_device_uid().
 *
 * @param uid Output buffer of exactly @ref HAL_DEVICE_UID_BYTES bytes.
 */
void hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]);

/**
 * @brief Write the unique device identifier as an uppercase hex string.
 *
 * The output contains 2 hex chars per UID byte followed by a NUL terminator
 * (16 hex chars + NUL = @ref HAL_DEVICE_UID_HEX_BUF_SIZE bytes total).
 *
 * If @p buflen is smaller than @ref HAL_DEVICE_UID_HEX_BUF_SIZE the call
 * writes nothing and returns false. The buffer is left unchanged on failure
 * only when @p buf is NULL; otherwise it is zero-initialised.
 *
 * @param buf    Output buffer.
 * @param buflen Size of @p buf in bytes.
 * @return true on success, false on NULL buffer or insufficient size.
 */
bool hal_get_device_uid_hex(char *buf, size_t buflen);

/**
 * @def NONULL(x)
 * @brief Guard-pointer helper that jumps to a local `error:` label when `x` is null.
 *
 * Intended for compact early-exit checks in functions that use a shared
 * cleanup/error path.
 *
 * Example:
 * @code
 * char *buf = allocate();
 * NONULL(buf);
 * // normal path
 * return true;
 *
 * error:
 * return false;
 * @endcode
 *
 * @param x Pointer expression to validate against NULL.
 *
 * @note This macro requires the surrounding function to define an `error:`
 *       label.
 * @note Kept for backward compatibility with existing helper-style code.
 * @note Safe to use from both C and C++ translation units.
 */
#ifndef NONULL
#define NONULL(x) do { if ((x) == NULL) { goto error; } } while (0)
#endif

#ifdef __cplusplus
}
#endif
