#pragma once

/**
 * @file hal_system.h
 * @brief System-level HAL: timing, watchdog, idle, and shared utility helpers.
 *
 * Provides:
 * - Millisecond / microsecond counters (@ref hal_millis, @ref hal_micros,
 *   @ref hal_micros64), millisecond delays, and timing-safe microsecond
 *   busy-wait delays.
 * - Hardware watchdog control (@ref hal_watchdog_enable,
 *   @ref hal_watchdog_feed, @ref hal_watchdog_caused_reboot).
 * - Free-heap query and on-chip temperature sensor.
 * - Controlled reboot into RP2040 USB bootloader mode
 *   (@ref hal_enter_bootloader).
 * - Shared utility helpers that have no better home and must be visible to
 *   both C and C++ translation units:
 *   - @ref COUNTOF  - element count of a static array.
 *   - @ref hal_u32_to_bytes_be - 32-bit big-endian serialisation.
 *   - @ref NONULL   - null-pointer guard with goto-error idiom.
 *   - Type-conversion macros: @ref SECS, @ref MINS, @ref HOURS.
 */

#include "hal/core/hal_array.h"
#include "hal/core/hal_math.h"
#include "hal/core/hal_status.h"
#include "hal/core/jh_endian.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- Time-conversion macros ------------------------------------------------ */

#define SECONDS_IN_MINUTE 60
#define MILIS_IN_MINUTE 60000.0

/** @brief One second in milliseconds. */
#define SECOND 1000UL

/** @brief Convert seconds to milliseconds. */
#define SECS(t) ((unsigned long)((t) * SECOND))

/** @brief Convert minutes to milliseconds. */
#define MINS(t) (SECS(t) * 60UL)

/** @brief Convert hours to milliseconds. */
#define HOURS(t) (MINS(t) * 60UL)

/** @brief Ownership/topology of the active IP and socket stack. */
typedef enum {
  HAL_SYSTEM_NETWORK_STACK_TYPE_NONE = 0,
  HAL_SYSTEM_NETWORK_STACK_TYPE_HOST,
  HAL_SYSTEM_NETWORK_STACK_TYPE_SOCKET_OFFLOAD
} hal_system_network_stack_type_t;

/**
 * @brief Snapshot of the currently running HAL architecture/backend.
 *
 * Static target identity and memory capacities come from generated target and
 * board descriptors. Runtime fields are supplied by the active backend.
 * String fields point to generated or backend-owned static storage. Numeric
 * fields use zero when a value is not known or meaningful for the target.
 *
 * Memory fields deliberately separate physical capacity from the region
 * available to the application. For example, STM32G474 reports the whole
 * internal flash in @c flash_total_bytes and the linker/application region in
 * @c flash_usable_bytes after HAL storage reservations are subtracted.
 */
typedef struct {
  const char *target_name;  /**< Exact HAL target name. */
  const char *backend_name; /**< Backend/runtime carrier name. */
  const char *mcu;          /**< MCU or host family name. */
  const char *mcu_subtype;  /**< Board/chip/package subtype if known. */
  const char *cpu_arch;     /**< CPU architecture/core description. */
  const char *rtos_name;    /**< Active RTOS/runtime scheduler name. */
  uint8_t cpu_cores;        /**< Number of CPU cores visible to HAL. */
  bool is_hardware;         /**< true on real MCU hardware. */
  bool has_fpu;          /**< true when hardware floating point is enabled. */
  bool has_rtos;         /**< true when an RTOS-aware HAL path is active. */
  uint32_t cpu_clock_hz; /**< Current/core CPU clock in Hz. */
  uint32_t peripheral_clock_hz; /**< Primary peripheral/bus clock in Hz. */
  uint32_t flash_total_bytes;   /**< Total non-volatile program storage. */
  uint32_t flash_usable_bytes;  /**< Program flash available to application. */
  uint32_t
      flash_reserved_bytes; /**< Flash reserved by HAL storage/filesystems. */
  uint32_t ram_total_bytes; /**< Total RAM relevant to the HAL runtime. */
  uint32_t
      ram_usable_bytes; /**< RAM region normally available to app/linker. */
  uint32_t heap_total_bytes;  /**< Heap capacity if known. */
  uint32_t heap_free_bytes;   /**< Current free heap reported by backend. */
  uint32_t stack_total_bytes; /**< Main/core stack reservation if known. */
  uint32_t uid_bytes;         /**< Unique-device-ID width in bytes. */
  const char *network_backend_name; /**< Selected network backend or "none". */
  const char *network_stack_name;   /**< IP/socket stack name or "none". */
  hal_system_network_stack_type_t
      network_stack_type; /**< Host stack, socket offload, or none. */
} hal_system_architecture_t;

/**
 * @brief Return a snapshot describing the currently running HAL architecture.
 *
 * @param out Destination structure.
 * @return HAL_OK on success, HAL_EINVAL when @p out is NULL.
 */
hal_status_t
hal_system_get_current_architecture(hal_system_architecture_t *out);

/**
 * @brief Convert 32-bit value to 4-byte big-endian representation.
 * @param val Source value.
 * @param buf Destination buffer of at least 4 bytes.
 * @return HAL_OK on success or HAL_EINVAL when @p buf is NULL.
 */
static inline hal_status_t hal_u32_to_bytes_be(uint32_t val, uint8_t *buf) {
  if (buf == NULL) {
    return HAL_EINVAL;
  }
  jh_store_be32(buf, val);
  return HAL_OK;
}

/**
 * @brief Return milliseconds since boot.
 * @return Millisecond counter (wraps after ~49 days).
 */
uint32_t hal_millis(void);

/**
 * @brief Check whether an unsigned 32-bit interval has elapsed.
 *
 * The subtraction is wrap-safe as long as intervals do not exceed half of
 * the counter range.
 */
static inline bool hal_elapsed_u32(uint32_t now, uint32_t started,
                                   uint32_t interval) {
  return (uint32_t)(now - started) >= interval;
}

/** @brief Check a millisecond deadline against the current system tick. */
static inline bool hal_millis_deadline_expired(uint32_t started,
                                               uint32_t timeout_ms) {
  return hal_elapsed_u32(hal_millis(), started, timeout_ms);
}

/** @brief Callback signature used by millis interval helpers. */
typedef void (*hal_millis_interval_callback_t)(void *user_data);

/**
 * @brief Check whether a non-blocking millis interval elapsed.
 *
 * This helper implements the established wrap-safe pattern:
 *
 * @code
 * if ((now_ms - last_ms) >= interval_ms) { ... }
 * @endcode
 *
 * On success it updates @p last_ms to @p now_ms so the next interval starts
 * from the current tick.
 *
 * @param now_ms      Current timestamp (typically @ref hal_millis()).
 * @param last_ms     Pointer to caller-owned last-fire timestamp.
 * @param interval_ms Interval in milliseconds (>0).
 * @return true when elapsed and @p last_ms was updated.
 */
static inline bool hal_millis_interval_elapsed(uint32_t now_ms,
                                               uint32_t *last_ms,
                                               uint32_t interval_ms) {
  if (last_ms == NULL || interval_ms == 0u) {
    return false;
  }
  if (!hal_elapsed_u32(now_ms, *last_ms, interval_ms)) {
    return false;
  }
  *last_ms = now_ms;
  return true;
}

/**
 * @brief Check whether a non-blocking millis interval elapsed using now().
 *
 * Equivalent to calling @ref hal_millis_interval_elapsed with
 * @ref hal_millis() as @p now_ms.
 */
static inline bool hal_millis_interval_elapsed_now(uint32_t *last_ms,
                                                   uint32_t interval_ms) {
  return hal_millis_interval_elapsed(hal_millis(), last_ms, interval_ms);
}

/**
 * @brief Fire callback when a non-blocking millis interval elapsed.
 *
 * Equivalent to:
 *
 * @code
 * if (hal_millis_interval_elapsed(now_ms, &last_ms, interval_ms)) {
 *   callback(user_data);
 * }
 * @endcode
 *
 * When elapsed, @p last_ms is updated and the callback is invoked when not
 * NULL.
 */
static inline bool hal_millis_interval_call(
    uint32_t now_ms, uint32_t *last_ms, uint32_t interval_ms,
    hal_millis_interval_callback_t callback, void *user_data) {
  if (!hal_millis_interval_elapsed(now_ms, last_ms, interval_ms)) {
    return false;
  }
  if (callback != NULL) {
    callback(user_data);
  }
  return true;
}

/**
 * @brief Fire callback when a non-blocking millis interval elapsed using now().
 */
static inline bool
hal_millis_interval_call_now(uint32_t *last_ms, uint32_t interval_ms,
                             hal_millis_interval_callback_t callback,
                             void *user_data) {
  return hal_millis_interval_call(hal_millis(), last_ms, interval_ms, callback,
                                  user_data);
}

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
 * @brief Delay for the given number of milliseconds.
 *
 * FreeRTOS-aware backends may yield/block the current task when the scheduler
 * is running and the call is made from task context. Timing-sensitive callers
 * that require an interrupt-independent wait should use @ref hal_delay_us.
 *
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
 * @param pause_on_debug If true, pause the watchdog when a debugger is
 *                       attached.
 * @note On ESP32-S3 @p pause_on_debug is informational because OpenOCD owns
 *       watchdog behavior while a core is halted; ESP-IDF exposes no
 *       equivalent per-TWDT runtime setting.
 * @return HAL_OK on success, HAL_EINVAL for an unsupported timeout value,
 *         HAL_ETIMEOUT when hardware configuration does not settle, or
 *         HAL_EUNSUPPORTED when the backend has no watchdog implementation.
 */
hal_status_t hal_watchdog_enable(uint32_t ms, bool pause_on_debug);

/**
 * @brief Check whether the last boot was caused by a watchdog *timeout*.
 *
 * Reports true only for a genuine watchdog starvation while the application
 * watchdog was armed (via @ref hal_watchdog_enable). A programmatic/commanded
 * reboot -- firmware upload (picotool / UF2 / BOOTSEL) or an explicit reboot
 * request -- is intentionally NOT reported as a watchdog event, even though on
 * some MCUs (e.g. RP2040) it is implemented on top of the watchdog hardware.
 *
 * @return true if a watchdog timeout caused the reboot.
 */
bool hal_watchdog_caused_reboot(void);

/* -- Crash / fault diagnostics --------------------------------------------- */

/**
 * @brief Coarse-grained classification of the reason for the last MCU reset.
 *
 * Values are intentionally backend-agnostic. Not every reason is detectable
 * on every target -- backends that cannot distinguish a specific cause
 * collapse it into @ref HAL_RESET_REASON_UNKNOWN or the closest superset.
 *
 * Notes per backend:
 * - RP2040: @ref HAL_RESET_REASON_BROWNOUT is *not* reported
 *   by silicon (POR and BOR share one flag). See
 *   @ref hal_last_boot_was_brownout for a heuristic that flags suspected
 *   brown-outs using a retained alive marker.
 * - RP2350: brown-out and supply glitches have dedicated flags.
 * - @ref HAL_RESET_REASON_HARDFAULT is synthesised: the HardFault handler
 *   installed by the HAL records a marker into retained scratch storage and
 *   triggers a reboot, which the HAL recognises on the next boot.
 */
typedef enum {
  HAL_RESET_REASON_UNKNOWN = 0,
  HAL_RESET_REASON_POWER_ON,
  HAL_RESET_REASON_RUN_PIN,
  HAL_RESET_REASON_SOFT,
  HAL_RESET_REASON_WATCHDOG,
  HAL_RESET_REASON_DEBUG,
  HAL_RESET_REASON_GLITCH,
  HAL_RESET_REASON_BROWNOUT,
  HAL_RESET_REASON_HARDFAULT,
  HAL_RESET_REASON_STACK_OVERFLOW
} hal_reset_reason_t;

/**
 * @brief Captured CPU state at the moment of the last fatal fault.
 *
 * Populated by the HAL's HardFault handler from the exception stack frame
 * before a reboot is forced. Survives the reboot via retained scratch
 * registers (RP2040: watchdog scratch[0..3]).
 *
 * PC plus LR is usually enough to identify the crashing call site via
 * @c arm-none-eabi-addr2line. Cortex-M4/M33 backends also retain their fault
 * status/address registers; targets without them report those fields as zero.
 */
typedef struct {
  bool valid;     /**< true when the previous boot was preceded by a fault */
  uint32_t pc;    /**< stacked PC, or best-effort compiler failure caller */
  uint32_t lr;    /**< stacked LR (caller return address)                 */
  uint32_t psr;   /**< stacked xPSR; RP2350 RISC-V reports mcause here */
  uint32_t cfsr;  /**< Cortex-M CFSR, or 0 when unavailable              */
  uint32_t hfsr;  /**< Cortex-M HFSR, or 0 when unavailable              */
  uint32_t mmfar; /**< Cortex-M MMFAR, or 0/invalid when unavailable     */
  uint32_t bfar;  /**< Cortex-M BFAR, or 0/invalid when unavailable      */
} hal_fault_info_t;

/**
 * @brief Initialise the fault-diagnostic subsystem.
 *
 * Call this as the very first thing in @c app_start() (before any code that may
 * write to retained scratch storage or arm the watchdog with custom magic).
 * On backends that need it, this installs the HardFault handler, latches the
 * boot reason, snapshots any captured fault info into RAM, and clears the
 * retained alive marker so the next @ref hal_alive_mark call seeds it again.
 *
 * Idempotent. No effect on backends that do not provide this facility.
 */
void hal_fault_subsystem_init(void);

/**
 * @brief Return the cause of the last MCU reset.
 *
 * Stable after @ref hal_fault_subsystem_init. Backends without diagnostic
 * support return @ref HAL_RESET_REASON_UNKNOWN.
 *
 * @return Reset reason classification.
 */
hal_reset_reason_t hal_get_reset_reason(void);

/**
 * @brief Human-readable, statically-allocated name for a reset reason.
 * @return Pointer to a literal string. Never NULL.
 */
const char *hal_reset_reason_str(hal_reset_reason_t reason);

/**
 * @brief Retrieve the CPU state captured by the previous fatal fault.
 *
 * This is the status-returning implementation used by
 * @ref hal_get_last_fault.
 *
 * @param out Destination structure.
 * @return HAL_OK when a snapshot was copied, HAL_EINVAL when @p out is NULL,
 *         or HAL_ENOENT when no captured fault is available.
 * @note ESP32-S3 retains the Xtensa PC, return address, and processor-status
 *       register across a fatal exception reset; Cortex-specific fields are
 *       reported as zero.
 */
hal_status_t hal_get_last_fault_ex(hal_fault_info_t *out);

/**
 * @brief Retrieve the CPU state captured by the previous fatal fault.
 *
 * Compatibility wrapper over @ref hal_get_last_fault_ex.
 *
 * @param out Destination, populated only when the function returns true.
 * @return true if a fault snapshot is available (i.e. the previous boot
 *         followed a fatal fault captured by this HAL). false otherwise, in
 *         which case @p out is left untouched.
 */
bool hal_get_last_fault(hal_fault_info_t *out);

/**
 * @brief Discard any captured fatal-fault snapshot.
 *
 * Subsequent calls to @ref hal_get_last_fault return false until another
 * fault occurs and the device reboots through the HAL handler.
 */
void hal_clear_last_fault(void);

/**
 * @brief Heuristic: was the previous boot caused by a brown-out?
 *
 * On targets where the silicon does not distinguish brown-out from power-on
 * reset (notably RP2040), the HAL uses a retained "alive marker". The
 * application calls @ref hal_alive_mark from its main loop after early init
 * completes. If the next boot reports a power-on reset but the alive marker
 * is still set in retained storage, the previous run was clearly past the
 * point where the marker was written, which strongly suggests a supply
 * dip rather than a true cold boot.
 *
 * Backends with hardware BOR detection (e.g. RP2350) use the hardware flag
 * directly and ignore the marker. ESP32-S3 uses the ESP-IDF reset reason and
 * does not retain a HAL alive marker.
 *
 * The function returns a stable value once @ref hal_fault_subsystem_init has
 * run; it does not depend on the marker being refreshed in the current boot.
 *
 * @return true if a brown-out is suspected. false otherwise, including on
 *         backends with no supported detection path.
 */
bool hal_last_boot_was_brownout(void);

/**
 * @brief Refresh the retained alive marker used by the brown-out heuristic.
 *
 * Cheap (a single 32-bit store on RP2040). Call periodically from the main
 * loop. The exact interval is not critical; once per second is more than
 * enough. The marker is cleared by @ref hal_fault_subsystem_init so that
 * the very first call after boot is what arms the heuristic for the *next*
 * boot. This function is a no-op on ESP32-S3.
 */
void hal_alive_mark(void);

/**
 * @brief Confirm that synchronous stack-overflow protection is active.
 *
 * Define @c HAL_ENABLE_STACK_GUARD in the project configuration to enable the
 * target implementation. Native RP builds use the Pico SDK hardware guard
 * (MPU on RP2040, stack limit/PMP on RP2350); STM32G474 protects the bottom
 * 32 bytes of the main stack with an MPU region. FreeRTOS builds additionally
 * enable kernel task-stack overflow checking.
 *
 * ESP32-S3 uses the FreeRTOS end-of-stack watchpoint and canary configured at
 * build time; this function confirms that those settings are active.
 *
 * Protection is installed during platform startup. This function is
 * idempotent and provides a target-independent way to verify availability.
 *
 * @return HAL_OK when protection is active, HAL_EUNSUPPORTED when the feature
 *         is disabled or unavailable, or HAL_EHW when the compiled feature's
 *         live MPU/MSPLIM/PMP configuration does not match the required guard.
 */
hal_status_t hal_stack_guard_init_ex(void);

/**
 * @brief Boolean wrapper over @ref hal_stack_guard_init_ex.
 *
 * @return true when protection is active; false when disabled, unsupported,
 *         or present but misconfigured.
 */
bool hal_stack_guard_init(void);

/**
 * @brief Legacy polling point retained as a target-independent no-op.
 *
 * Stack violations are reported synchronously by hardware (and by FreeRTOS
 * for task stacks), so applications do not need to call this periodically.
 */
void hal_stack_guard_check(void);

/**
 * @brief Yield to the system (cooperative multitasking / idle hook).
 */
void hal_idle(void);

/**
 * @brief Return true when the current execution context is an interrupt
 *        service routine (hardware exception/IRQ handler).
 *
 * Implementations:
 * - On Cortex-M targets (RP2040, STM32G474) this reads the IPSR register;
 *   any non-zero exception number means handler mode.
 * - On ESP32-S3 this uses the ESP-IDF FreeRTOS port's ISR-context query.
 * - On mock/host builds the value is controlled by a test hook
 *   (default false). See hal_mock_set_in_isr().
 *
 * Intended use: HAL primitives that have an ISR-safe fast path
 * (e.g. hal_deb() routing log lines to a deferred ring buffer instead
 * of touching the underlying UART) can branch on this helper.
 *
 * @return true if called from interrupt context.
 */
bool hal_in_isr(void);

/**
 * @brief Return the amount of free heap memory in bytes.
 * @return Free heap in bytes.
 */
uint32_t hal_get_free_heap(void);

/**
 * @brief Read the on-chip temperature sensor into a caller-owned output.
 *
 * This is the status-returning implementation used by
 * @ref hal_read_chip_temp.
 *
 * @param out_celsius Destination for the measured die temperature.
 * @return HAL_OK on success, HAL_EINVAL when @p out_celsius is NULL, or
 *         HAL_EUNSUPPORTED when the backend has no implemented sensor path.
 */
hal_status_t hal_read_chip_temp_ex(float *out_celsius);

/**
 * @brief Read the on-chip temperature sensor.
 *
 * Compatibility wrapper over @ref hal_read_chip_temp_ex. RP targets sample
 * the native ADC channel connected to the die sensor; ESP32-S3 uses the
 * ESP-IDF temperature-sensor driver.
 *
 * The value is approximate and reflects the silicon temperature. Its accuracy
 * depends on the chip sensor and the board's ADC reference voltage.
 *
 * @return Die temperature in degrees Celsius, or 0.0f when unsupported.
 */
float hal_read_chip_temp(void);

/**
 * @brief Reboot into USB bootloader mode (UF2 mass-storage mode).
 *
 * On RP targets this calls the ROM bootloader entry path (BOOTSEL mode),
 * disconnecting the running application and exposing the USB UF2 flashing
 * interface.
 * ESP32-S3 requests ROM serial-download mode on the next reset when download
 * mode has not been disabled by eFuse policy; otherwise it reports
 * HAL_EUNSUPPORTED.
 *
 * Typical use:
 * - acknowledge the host command first,
 * - ensure outputs are placed in a safe state,
 * - call this function as the final step.
 *
 * @note This function does not return on real hardware.
 * @note In mock/unit-test builds this is implemented as a non-resetting flag.
 * @return HAL_OK when the request was accepted or HAL_EUNSUPPORTED when the
 *         backend has no bootloader-entry implementation.
 */
hal_status_t hal_enter_bootloader(void);

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
 * 64-bit unique id stored in the external QSPI flash chip. ESP32-S3 returns an
 * 8-byte value formed by zero-extending its 48-bit factory eFuse MAC.
 *
 * On mock/unit-test builds this returns a deterministic pattern, overridable
 * via @c hal_mock_set_device_uid().
 *
 * @param uid Output buffer of exactly @ref HAL_DEVICE_UID_BYTES bytes.
 * @return HAL_OK on success or HAL_EINVAL when @p uid is NULL.
 */
hal_status_t hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]);

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
 * @return HAL_OK on success, HAL_EINVAL for NULL buffer, or HAL_EOVERFLOW
 *         when @p buflen is smaller than @ref HAL_DEVICE_UID_HEX_BUF_SIZE.
 */
hal_status_t hal_get_device_uid_hex_ex(char *buf, size_t buflen);

/**
 * @brief Write the unique device identifier as an uppercase hex string.
 *
 * Compatibility wrapper over hal_get_device_uid_hex_ex().
 *
 * @param buf    Output buffer.
 * @param buflen Size of @p buf in bytes.
 * @return true on success, false on NULL buffer or insufficient size.
 */
bool hal_get_device_uid_hex(char *buf, size_t buflen);

/**
 * @def NONULL(x)
 * @brief Guard-pointer helper that jumps to a local `error:` label when `x` is
 * null.
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
#define NONULL(x)                                                              \
  do {                                                                         \
    if ((x) == NULL) {                                                         \
      goto error;                                                              \
    }                                                                          \
  } while (0)
#endif

#ifdef __cplusplus
}
#endif
