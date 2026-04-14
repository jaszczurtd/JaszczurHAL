#pragma once

#include "hal_config.h"
#ifndef HAL_DISABLE_EEPROM

/**
 * @file hal_eeprom.h
 * @brief Unified EEPROM hardware abstraction layer.
 *
 * Provides a single API that works with both the RP2040 internal flash-backed
 * EEPROM (Arduino EEPROM emulation) and the external AT24C256 I2C EEPROM.
 * The backing storage is selected at runtime via hal_eeprom_init().
 *
 * ## Usage
 *
 * @code
 *   // RP2040 internal EEPROM (512 bytes):
 *   hal_eeprom_init(HAL_EEPROM_RP2040, 512);
 *
 *   // AT24C256 external I2C EEPROM (size ignored, always 32 KB):
 *   hal_eeprom_init(HAL_EEPROM_AT24C256, 0);
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
 * For HAL_EEPROM_RP2040 each hal_eeprom_write_byte() buffers the value in
 * RAM; call hal_eeprom_commit() once after a group of writes to flush the
 * buffer to flash.
 * For HAL_EEPROM_AT24C256 writes go straight to the chip; hal_eeprom_commit()
 * is a no-op.
 *
 * ## Thread safety
 *
 * Both back-ends are thread-safe and multicore-safe:
 * - `HAL_EEPROM_AT24C256`: protected by the `hal_i2c` bus mutex.
 * - `HAL_EEPROM_RP2040`: protected by a dedicated internal mutex.
 *
 * `hal_eeprom_init()` must be called from one core only.
 */

#include <stdint.h>

/**
 * @brief Supported EEPROM back-ends.
 */
typedef enum {
    HAL_EEPROM_AT24C256 = 1, /**< External AT24C256 I2C EEPROM (32 KB). */
    HAL_EEPROM_RP2040   = 2, /**< RP2040 internal flash-backed EEPROM emulation. */
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
 * @param size      Memory size in bytes.  Used only for HAL_EEPROM_RP2040 —
 *                  it is passed to EEPROM.begin().  Ignored for
 *                  HAL_EEPROM_AT24C256 (size is always 32768).
 * @param i2c_addr  7-bit I2C address of the AT24C256 chip.  Used only for
 *                  HAL_EEPROM_AT24C256; ignored for HAL_EEPROM_RP2040.
 *                  Pass 0 to use the default address defined by
 *                  @c EEPROM_I2C_ADDRESS in hal_config.h (0x50).
 */
void hal_eeprom_init(hal_eeprom_type_t type, uint16_t size, uint8_t i2c_addr);

/**
 * @brief Write one byte to EEPROM.
 *
 * For HAL_EEPROM_RP2040 the change is buffered in RAM until
 * hal_eeprom_commit() is called.
 * For HAL_EEPROM_AT24C256 the byte is written immediately to the chip
 * (the function waits for the internal write cycle to complete before
 * returning).
 *
 * @param addr EEPROM address.
 * @param val  Byte value to store.
 */
void hal_eeprom_write_byte(uint16_t addr, uint8_t val);

/**
 * @brief Read one byte from EEPROM.
 * @param addr EEPROM address.
 * @return Stored byte value.
 */
uint8_t hal_eeprom_read_byte(uint16_t addr);

/**
 * @brief Write a 32-bit signed integer to EEPROM (little-endian, 4 bytes).
 *
 * Stores val at addr..addr+3, LSB first.
 *
 * @param addr EEPROM address (must leave room for 4 bytes).
 * @param val  Value to store.
 */
void hal_eeprom_write_int(uint16_t addr, int32_t val);

/**
 * @brief Read a 32-bit signed integer from EEPROM (little-endian, 4 bytes).
 * @param addr EEPROM address where the integer starts.
 * @return Stored value.
 */
int32_t hal_eeprom_read_int(uint16_t addr);

/**
 * @brief Commit buffered writes to non-volatile storage.
 *
 * For HAL_EEPROM_RP2040: flushes the RAM buffer to flash (calls EEPROM.commit()).
 * For HAL_EEPROM_AT24C256: no-op (writes are already persistent).
 *
 * Call this once after a group of hal_eeprom_write_byte() /
 * hal_eeprom_write_int() calls when using the RP2040 back-end.
 */
void hal_eeprom_commit(void);

/**
 * @brief Zero-fill the entire EEPROM.
 *
 * For HAL_EEPROM_RP2040: writes 0 to every byte then calls EEPROM.commit().
 * For HAL_EEPROM_AT24C256: writes 0 to every byte (with watchdog feeding).
 *
 * @warning This is a slow operation — avoid calling it in time-critical paths.
 */
void hal_eeprom_reset(void);

/**
 * @brief Return the EEPROM size in bytes.
 *
 * Returns the value passed to hal_eeprom_init() for HAL_EEPROM_RP2040, or
 * 32768 for HAL_EEPROM_AT24C256.
 */
uint16_t hal_eeprom_size(void);

#ifdef __cplusplus
}
#endif


#endif /* HAL_DISABLE_EEPROM */