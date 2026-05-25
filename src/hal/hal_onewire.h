#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_ONEWIRE

/**
 * @file hal_onewire.h
 * @brief Thread-safe HAL wrapper for OneWire bus operations.
 *
 * This module wraps the bundled OneWire driver placed under
 * src/hal/impl/arduino/drivers/OneWire and exposes a platform-neutral
 * API for Arduino and host-mock builds.
 *
 * Thread-safety model:
 * - Every public operation is protected by an internal handle mutex.
 * - Arduino backend additionally serializes hardware-timing-critical
 *   driver calls through a shared internal bus mutex.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Opaque handle for one OneWire bus bound to a single GPIO pin. */
typedef struct hal_onewire_impl_s hal_onewire_impl_t;
typedef hal_onewire_impl_t *hal_onewire_t;

/**
 * @brief Create a OneWire bus handle for the selected data pin.
 * @param data_pin GPIO connected to the 1-Wire data line.
 * @return Handle on success, NULL on failure/pool exhaustion.
 */
hal_onewire_t hal_onewire_init(uint8_t data_pin);

/**
 * @brief Destroy a OneWire handle and release its pool slot.
 * @param h Handle returned by hal_onewire_init().
 */
void hal_onewire_deinit(hal_onewire_t h);

/**
 * @brief Perform 1-Wire reset and detect presence pulse.
 * @param h Valid OneWire handle.
 * @return true when at least one device is present.
 */
bool hal_onewire_reset(hal_onewire_t h);

/**
 * @brief Select a specific ROM on the bus (Match ROM sequence).
 * @param h   Valid OneWire handle.
 * @param rom 8-byte device ROM code.
 */
void hal_onewire_select(hal_onewire_t h, const uint8_t rom[8]);

/**
 * @brief Address all devices on the bus (Skip ROM command).
 * @param h Valid OneWire handle.
 */
void hal_onewire_skip(hal_onewire_t h);

/**
 * @brief Write one byte to the bus.
 * @param h     Valid OneWire handle.
 * @param value Byte to transmit.
 * @param power Keep line powered after write when true.
 */
void hal_onewire_write(hal_onewire_t h, uint8_t value, bool power);

/**
 * @brief Write multiple bytes to the bus.
 * @param h     Valid OneWire handle.
 * @param data  Pointer to source buffer.
 * @param len   Number of bytes to write.
 * @param power Keep line powered after write when true.
 * @return Number of bytes written (0 on invalid args/handle).
 */
size_t hal_onewire_write_bytes(hal_onewire_t h, const uint8_t *data, uint16_t len, bool power);

/**
 * @brief Read one byte from the bus.
 * @param h Valid OneWire handle.
 * @return Received byte (0 on invalid handle).
 */
uint8_t hal_onewire_read(hal_onewire_t h);

/**
 * @brief Read multiple bytes from the bus.
 * @param h   Valid OneWire handle.
 * @param out Destination buffer.
 * @param len Number of bytes to read.
 * @return Number of bytes read (0 on invalid args/handle).
 */
size_t hal_onewire_read_bytes(hal_onewire_t h, uint8_t *out, uint16_t len);

/**
 * @brief Write one bit.
 * @param h   Valid OneWire handle.
 * @param bit Bit value; non-zero is treated as 1.
 */
void hal_onewire_write_bit(hal_onewire_t h, uint8_t bit);

/**
 * @brief Read one bit.
 * @param h Valid OneWire handle.
 * @return 0 or 1.
 */
uint8_t hal_onewire_read_bit(hal_onewire_t h);

/**
 * @brief Release parasite-power strong drive.
 * @param h Valid OneWire handle.
 */
void hal_onewire_depower(hal_onewire_t h);

/**
 * @brief Reset device-search state.
 * @param h Valid OneWire handle.
 */
void hal_onewire_reset_search(hal_onewire_t h);

/**
 * @brief Restrict next search passes to one family code.
 * @param h           Valid OneWire handle.
 * @param family_code 1-Wire family byte.
 */
void hal_onewire_target_search(hal_onewire_t h, uint8_t family_code);

/**
 * @brief Search for next ROM on the bus.
 * @param h           Valid OneWire handle.
 * @param out_rom     Output 8-byte ROM buffer.
 * @param search_mode true for normal search, false for conditional search.
 * @return true if a ROM was found and written to out_rom.
 */
bool hal_onewire_search(hal_onewire_t h, uint8_t out_rom[8], bool search_mode);

/**
 * @brief Compute Dallas/Maxim 8-bit CRC.
 * @param data Pointer to bytes.
 * @param len  Number of bytes.
 * @return CRC-8 value.
 */
uint8_t hal_onewire_crc8(const uint8_t *data, uint8_t len);

#endif /* HAL_ENABLE_ONEWIRE */
#ifdef __cplusplus
}
#endif
