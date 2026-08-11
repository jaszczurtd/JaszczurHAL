#pragma once

/**
 * @file hal_crc.h
 * @brief Backend-agnostic CRC checksums (integrity, not cryptography).
 *
 * A small, table-free home for the CRC variants used across the library and by
 * downstream projects. CRC is a *family* parameterized by width, polynomial,
 * init value, input/output reflection and final XOR, so every entry here is
 * named after the concrete variant it computes - there is deliberately no
 * unqualified "the CRC16" that would silently squat one polynomial.
 *
 * Currently provided:
 * - CRC-8/MAXIM-DOW      (`hal_crc8_maxim`)        - Dallas/Maxim 1-Wire CRC-8.
 * - Maxim 1-Wire CRC-16  (`hal_crc16_maxim`)       - iButton CRC-16, seedable.
 * - CRC-16/CCITT-FALSE   (`hal_crc16_ccitt`)       - poly 0x1021, init 0xFFFF.
 * - CRC-32/ISO-HDLC      (`hal_crc32`)             - zlib/Ethernet CRC-32.
 *
 * The module is opt-in: define @c HAL_ENABLE_CRC in `hal_project_config.h` (or
 * via `-D`) to compile it in. Enabling @c HAL_ENABLE_ONEWIRE (directly or via
 * @c HAL_ENABLE_DS18B20) also enables it, since the 1-Wire path needs CRC.
 */

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_CRC

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute Dallas/Maxim CRC-8 (CRC-8/MAXIM-DOW, reflected poly 0x8C).
 *
 * Catalog check value: CRC-8/MAXIM-DOW of "123456789" is 0xA1.
 *
 * @param data Pointer to the input bytes.
 * @param len  Number of bytes.
 * @return CRC-8 value, or 0 when @p data is NULL or @p len is 0.
 */
uint8_t hal_crc8_maxim(const uint8_t *data, size_t len);

/**
 * @brief Compute the Maxim/Dallas 1-Wire CRC-16 (before bus-level inversion).
 *
 * Poly 0xA001 (reflected), init/xorout handled by the caller through @p crc and
 * the separate @ref hal_crc16_maxim_check helper. Pass @p crc = 0 to start a
 * fresh CRC, or the previous result to continue over a split buffer.
 *
 * @param data Pointer to the input bytes.
 * @param len  Number of bytes.
 * @param crc  Starting CRC value (usually 0).
 * @return CRC-16 value; returns @p crc unchanged when @p data is NULL.
 */
uint16_t hal_crc16_maxim(const uint8_t *data, size_t len, uint16_t crc);

/**
 * @brief Verify a Maxim 1-Wire CRC-16 against the inverted bytes on the bus.
 *
 * 1-Wire devices transmit the CRC-16 bit-inverted; this recomputes the CRC and
 * compares it against @p inverted_crc the same way the original driver did.
 *
 * @param data         Pointer to the bytes included in the CRC.
 * @param len          Number of bytes.
 * @param inverted_crc Two CRC bytes as transmitted by the device.
 * @param crc          Starting CRC value (usually 0).
 * @return true when the CRC matches; false on NULL arguments or mismatch.
 */
bool hal_crc16_maxim_check(const uint8_t *data, size_t len,
                           const uint8_t inverted_crc[2], uint16_t crc);

/** @brief Recommended initial value for CRC-16/CCITT-FALSE. */
#define HAL_CRC16_CCITT_INIT 0xFFFFu

/**
 * @brief Compute CRC-16/CCITT-FALSE (poly 0x1021, non-reflected).
 *
 * With @p crc = @ref HAL_CRC16_CCITT_INIT the catalog check value for
 * "123456789" is 0x29B1. Pass a previous result to continue over a split
 * buffer.
 *
 * @param data Pointer to the input bytes.
 * @param len  Number of bytes.
 * @param crc  Starting CRC value (use @ref HAL_CRC16_CCITT_INIT for a fresh
 * CRC).
 * @return CRC-16 value; returns @p crc unchanged when @p data is NULL.
 */
uint16_t hal_crc16_ccitt(const uint8_t *data, size_t len, uint16_t crc);

/**
 * @brief Compute CRC-32/ISO-HDLC (zlib/Ethernet CRC-32, reflected poly
 * 0xEDB88320).
 *
 * Self-contained one-shot with the standard 0xFFFFFFFF init and final XOR.
 * Catalog check value: CRC-32 of "123456789" is 0xCBF43926.
 *
 * @param data Pointer to the input bytes.
 * @param len  Number of bytes.
 * @return CRC-32 value, or 0 when @p data is NULL or @p len is 0.
 */
uint32_t hal_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_CRC */
