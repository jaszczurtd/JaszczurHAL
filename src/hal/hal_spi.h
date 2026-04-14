#pragma once

/**
 * @file hal_spi.h
 * @brief Hardware abstraction for SPI bus initialisation and synchronization.
 *
 * Wraps platform-specific pin assignment and bus startup so that project
 * code is decoupled from the Arduino SPI object and can be tested on a PC
 * using the mock implementation.
 *
 * Two SPI controllers are supported via the @p bus parameter:
 *   - bus 0 → SPI  (default controller)
 *   - bus 1 → SPI1 (second controller, RP2040)
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure SPI pins and start the bus in controller (master) mode.
 * @param bus     SPI controller index (0 = SPI, 1 = SPI1).
 * @param rx_pin  MISO pin number.
 * @param tx_pin  MOSI pin number.
 * @param sck_pin SCK  pin number.
 */
void hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin, uint8_t sck_pin);

/**
 * @brief Acquire the mutex for the selected SPI controller.
 *
 * Use this to guard multi-step interactions with drivers that access `SPI`
 * directly (for example MCP2515).
 *
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 */
void hal_spi_lock(uint8_t bus);

/**
 * @brief Release the mutex for the selected SPI controller.
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 */
void hal_spi_unlock(uint8_t bus);

#ifdef __cplusplus
}
#endif
