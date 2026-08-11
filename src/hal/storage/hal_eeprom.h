#pragma once

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_EEPROM

/**
 * @file hal_eeprom.h
 * @brief Unified EEPROM hardware abstraction layer.
 *
 * Provides a single API that works with target-native flash-backed EEPROM
 * emulation (native RP coordinated flash, RP Arduino compatibility EEPROM, or
 * STM32G474 internal flash reservation) and the external AT24C256 I2C EEPROM.
 * The backing storage is selected at runtime via hal_eeprom_init().
 * Use HAL_EEPROM_FLASH for portable internal-flash storage across targets.
 * HAL_EEPROM_RP2040 remains available for existing RP2040 applications.
 *
 * ## Usage
 *
 * @code
 *   // Target-native internal flash EEPROM (512 bytes):
 *   hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);
 *
 *   // AT24C256 external I2C EEPROM (size ignored, always 32 KB):
 *   hal_eeprom_init(HAL_EEPROM_AT24C256, 0, 0);
 * @endcode
 *
 * ## Integer byte order
 *
 * hal_eeprom_write_int() / hal_eeprom_read_int() store values in
 * **little-endian** order (LSB at the lowest address), consistent with
 * how the RP2040 internal EEPROM was used in prior projects.
 *
 * ## Commit semantics
 *
 * For flash-backed EEPROM back-ends each hal_eeprom_write_byte() buffers the
 * value in RAM; call hal_eeprom_commit() once after a group of writes to flush
 * the buffer to flash.
 * For HAL_EEPROM_AT24C256 writes go straight to the chip in page-sized chunks;
 * hal_eeprom_commit() is a no-op.
 *
 * ## Thread safety
 *
 * Both back-ends are thread-safe and multicore-safe:
 * - `HAL_EEPROM_AT24C256`: protected by the `hal_i2c` bus mutex.
 * - `HAL_EEPROM_FLASH` / native flash: protected by a dedicated internal
 *   mutex.
 *
 * `hal_eeprom_init()` must be called from one core only.
 */

#include "hal/core/hal_status.h"
#include <stdint.h>

/**
 * @brief Optional callback invoked during long EEPROM operations.
 *
 * The callback runs from inside the EEPROM critical section. Keep it short:
 * feed an application-owned watchdog, update a counter, or set a flag.
 */
typedef void (*hal_eeprom_progress_callback_t)(void *ctx);

/**
 * @brief Supported EEPROM back-ends.
 */
typedef enum {
  HAL_EEPROM_DEFAULT = 0,  /**< Target default persistent storage. */
  HAL_EEPROM_AT24C256 = 1, /**< External AT24C256 I2C EEPROM (32 KB). */
  HAL_EEPROM_RP2040 = 2, /**< RP2040 internal flash-backed EEPROM emulation. */
  HAL_EEPROM_STM32_FLASH =
      3, /**< STM32G474 internal flash-backed EEPROM emulation. */
  HAL_EEPROM_FLASH = 4, /**< Target-native internal flash EEPROM. */
} hal_eeprom_type_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the EEPROM subsystem.
 *
 * Must be called before any other hal_eeprom_* function.
 *
 * @param type      Which EEPROM back-end to use.
 * @param size      Memory size in bytes. Used for flash-backed EEPROM
 *                  back-ends. Pass 0 to use the whole target reservation.
 *                  Ignored for HAL_EEPROM_AT24C256 (size is always 32768).
 * @param i2c_addr  7-bit I2C address of the AT24C256 chip.  Used only for
 *                  HAL_EEPROM_AT24C256; ignored for flash-backed EEPROM.
 *                  Pass 0 to use the default address defined by
 *                  @c EEPROM_I2C_ADDRESS in hal_config.h (0x50).
 * @return HAL_OK, or HAL_EINVAL for an unknown back-end type. Existing callers
 *         that ignore the return value are unaffected.
 */
hal_status_t hal_eeprom_init(hal_eeprom_type_t type, uint16_t size,
                             uint8_t i2c_addr);

/**
 * @brief Register an optional callback for long EEPROM operations.
 *
 * Passing NULL disables progress notifications. HAL never feeds the watchdog
 * implicitly; applications that intentionally allow long EEPROM operations may
 * call hal_watchdog_feed() from this callback.
 *
 * Configure this before starting concurrent EEPROM access.
 * @return HAL_OK.
 */
hal_status_t
hal_eeprom_set_progress_callback(hal_eeprom_progress_callback_t callback,
                                 void *ctx);

/**
 * @brief Write one byte to EEPROM.
 *
 * For flash-backed EEPROM the change is buffered in RAM until
 * hal_eeprom_commit() is called.
 * For HAL_EEPROM_AT24C256 the byte is written immediately to the chip and the
 * function waits for the internal write cycle to complete, up to
 * HAL_AT24C256_WRITE_TIMEOUT_US.
 *
 * @param addr EEPROM address.
 * @param val  Byte value to store.
 * @return HAL_OK; HAL_EUNINIT before init; HAL_EOVERFLOW when @p addr is
 *         outside the device (no write happens, matching the legacy clip).
 */
hal_status_t hal_eeprom_write_byte(uint16_t addr, uint8_t val);

/**
 * @brief Read one byte from EEPROM.
 * @param addr EEPROM address.
 * @return Stored byte value (0 when out of range). Use
 * hal_eeprom_read_byte_ex() for a typed status.
 */
uint8_t hal_eeprom_read_byte(uint16_t addr);

/**
 * @brief Write a 32-bit signed integer to EEPROM (little-endian, 4 bytes).
 *
 * Stores val at addr..addr+3, LSB first.
 *
 * @param addr EEPROM address (must leave room for 4 bytes).
 * @param val  Value to store.
 * @return HAL_OK; HAL_EUNINIT before init; HAL_EOVERFLOW when the 4-byte range
 *         does not fully fit (the in-range prefix is still written, matching
 *         the legacy clip).
 */
hal_status_t hal_eeprom_write_int(uint16_t addr, int32_t val);

/**
 * @brief Read a 32-bit signed integer from EEPROM (little-endian, 4 bytes).
 * @param addr EEPROM address where the integer starts.
 * @return Stored value. Use hal_eeprom_read_int_ex() for a typed status.
 */
int32_t hal_eeprom_read_int(uint16_t addr);

/**
 * @brief Write a contiguous block of bytes to EEPROM under a single internal
 *        lock acquisition.
 *
 * Equivalent to calling hal_eeprom_write_byte() in a loop, but the EEPROM
 * mutex is taken only once for the whole batch. Use this when a higher-level
 * module needs to push a structured record into EEPROM without thrashing the
 * mutex per byte.
 *
 * For flash-backed EEPROM the data is buffered until hal_eeprom_commit() is
 * called. For HAL_EEPROM_AT24C256 the data is committed synchronously in
 * writes that do not cross AT24 page boundaries.
 *
 * @param addr EEPROM start address.
 * @param data Source buffer (must contain at least @p len bytes).
 * @param len  Number of bytes to write.
 * @return HAL_OK; HAL_EINVAL when @p data is NULL; HAL_EUNINIT before init;
 *         HAL_EOVERFLOW when the range is clipped (in-range prefix is still
 *         written, matching the legacy clip).
 */
hal_status_t hal_eeprom_write_bytes(uint16_t addr, const uint8_t *data,
                                    uint16_t len);

/**
 * @brief Read a contiguous block of bytes from EEPROM under a single internal
 *        lock acquisition.
 *
 * Equivalent to calling hal_eeprom_read_byte() in a loop, but the EEPROM
 * mutex is taken only once for the whole batch.
 *
 * @param addr EEPROM start address.
 * @param out  Destination buffer (must hold at least @p len bytes).
 * @param len  Number of bytes to read.
 * @return HAL_OK; HAL_EINVAL when @p out is NULL; HAL_EUNINIT before init;
 *         HAL_EOVERFLOW when the range is clipped (out-of-range tail is still
 *         zero-filled, matching the legacy behaviour).
 */
hal_status_t hal_eeprom_read_bytes(uint16_t addr, uint8_t *out, uint16_t len);

/**
 * @brief Commit buffered writes to non-volatile storage.
 *
 * For flash-backed EEPROM: flushes the RAM buffer to flash.
 * For HAL_EEPROM_AT24C256: no-op (writes are already persistent).
 *
 * Call this once after a group of hal_eeprom_write_byte() /
 * hal_eeprom_write_int() calls when using the RP2040 back-end.
 * @return HAL_OK on success; a backend flash/I2C error code otherwise.
 */
hal_status_t hal_eeprom_commit(void);

/**
 * @brief Zero-fill the entire EEPROM.
 *
 * For flash-backed EEPROM: writes 0 to every byte then commits to flash.
 * For HAL_EEPROM_AT24C256: writes 0 to every byte in page-sized chunks.
 *
 * @warning This is a slow operation - avoid calling it in time-critical paths.
 * @return HAL_OK on success; a backend flash/I2C error code otherwise.
 */
hal_status_t hal_eeprom_reset(void);

/**
 * @brief Return the EEPROM size in bytes.
 *
 * Returns the active flash-backed EEPROM size or 32768 for HAL_EEPROM_AT24C256.
 * Use hal_eeprom_size_ex() for a typed status (HAL_EUNINIT before init).
 */
uint16_t hal_eeprom_size(void);

/* ---- Status forms of the value-returning getters ------------------------ */
/*
 * The write/commit/reset/init/callback entry points above already return
 * hal_status_t directly (their historical `void` was upgraded in place, which
 * is source-compatible: existing callers simply ignore the new return value).
 *
 * The three getters below still return their value by convention, so they gain
 * `_ex` companions that report the value through an output parameter and return
 * a typed status: HAL_EINVAL for a NULL output, HAL_EUNINIT before init and
 * HAL_EOVERFLOW when the address is outside the device.
 */
hal_status_t hal_eeprom_read_byte_ex(uint16_t addr, uint8_t *out_val);
hal_status_t hal_eeprom_read_int_ex(uint16_t addr, int32_t *out_val);
hal_status_t hal_eeprom_size_ex(uint16_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_EEPROM */
