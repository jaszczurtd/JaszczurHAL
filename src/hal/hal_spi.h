#pragma once

/**
 * @file hal_spi.h
 * @brief Hardware abstraction for SPI bus initialisation, transactions and I/O.
 *
 * Wraps platform-specific pin assignment and bus startup so that project
 * code is decoupled from the Arduino SPI object and can be tested on a PC
 * using the mock implementation.  The API mirrors the parts of Arduino's SPI
 * layer that device drivers commonly use: SPISettings, begin/endTransaction,
 * byte/word transfer and in-place/buffer transfer.
 *
 * Two SPI controllers are supported via the @p bus parameter:
 *   - bus 0 -> SPI  (default controller; STM32G474 hardware SPI1)
 *   - bus 1 -> SPI1 (second Arduino-compatible object; STM32G474 hardware SPI2)
 * Any other bus value is invalid and triggers HAL_ASSERT in checked builds.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default SPI clock used by Arduino SPISettings(): 4 MHz. */
#define HAL_SPI_CLOCK_DEFAULT_HZ 4000000UL

/** @brief Bit order values matching Arduino's LSBFIRST / MSBFIRST. */
#define HAL_SPI_LSBFIRST 0u
#define HAL_SPI_MSBFIRST 1u

/** @brief SPI mode values matching Arduino's SPI_MODE0..SPI_MODE3. */
#define HAL_SPI_MODE0 0u
#define HAL_SPI_MODE1 1u
#define HAL_SPI_MODE2 2u
#define HAL_SPI_MODE3 3u

typedef struct {
  uint32_t clock_hz; /**< Target bus clock in Hz. 0 selects
                        HAL_SPI_CLOCK_DEFAULT_HZ. */
  uint8_t bit_order; /**< HAL_SPI_MSBFIRST or HAL_SPI_LSBFIRST. */
  uint8_t data_mode; /**< HAL_SPI_MODE0..HAL_SPI_MODE3. */
} hal_spi_settings_t;

/**
 * @brief Configure SPI pins and start the bus in controller (master) mode.
 * @param bus     SPI controller index (0 = SPI, 1 = SPI1).
 * @param rx_pin  MISO pin number.
 * @param tx_pin  MOSI pin number.
 * @param sck_pin SCK  pin number.
 */
void hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin, uint8_t sck_pin);

/**
 * @brief Stop the selected SPI controller.
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 */
void hal_spi_deinit(uint8_t bus);

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

/**
 * @brief Apply transaction settings to the selected SPI controller.
 *
 * This mirrors Arduino SPIClass::beginTransaction(): it configures clock,
 * bit order and SPI mode, but does not acquire the HAL mutex. Code that needs
 * cross-driver exclusion should still use hal_spi_lock()/hal_spi_unlock().
 *
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @param settings Transaction settings, or NULL for Arduino defaults.
 */
void hal_spi_begin_transaction(uint8_t bus, const hal_spi_settings_t *settings);

/**
 * @brief Finish a transaction on the selected SPI controller.
 *
 * This mirrors Arduino SPIClass::endTransaction(): it waits for pending
 * hardware transfer completion where the backend can observe it, but does not
 * release the HAL mutex.
 *
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 */
void hal_spi_end_transaction(uint8_t bus);

/**
 * @brief Full-duplex transfer of one byte.
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @param data Byte to transmit.
 * @return Byte received while transmitting.
 */
uint8_t hal_spi_transfer(uint8_t bus, uint8_t data);

/**
 * @brief Full-duplex transfer of one 16-bit word.
 *
 * The byte order follows the active bit-order setting, matching Arduino's
 * transfer16() behavior.
 *
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @param data Word to transmit.
 * @return Word received while transmitting.
 */
uint16_t hal_spi_transfer16(uint8_t bus, uint16_t data);

/**
 * @brief Full-duplex in-place transfer of a byte buffer.
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @param buffer Buffer to transmit and overwrite with received bytes.
 * @param len Number of bytes to transfer.
 */
void hal_spi_transfer_buffer(uint8_t bus, uint8_t *buffer, size_t len);

/**
 * @brief Full-duplex buffer transfer.
 *
 * Pass rx == NULL for write-only traffic. Pass tx == NULL to clock in bytes
 * while transmitting 0xFF.
 *
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @param tx Optional transmit buffer.
 * @param rx Optional receive buffer.
 * @param len Number of bytes to transfer.
 */
void hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                           size_t len);

/**
 * @brief Write a byte buffer, discarding received bytes.
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @param data Buffer to transmit.
 * @param len Number of bytes to transmit.
 */
void hal_spi_write(uint8_t bus, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
