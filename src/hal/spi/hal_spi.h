#pragma once

/**
 * @file hal_spi.h
 * @brief Hardware abstraction for SPI bus initialisation, transactions and I/O.
 *
 * Wraps platform-specific pin assignment and bus startup so that project code
 * is decoupled from backend-specific SPI peripherals and can be tested on a PC
 * using the mock implementation. The API keeps the common transaction concepts
 * used by SPI device drivers: explicit settings, begin/end transaction,
 * byte/word transfer and in-place/buffer transfer.
 *
 * Two SPI controllers are supported via the @p bus parameter:
 *   - bus 0 -> RP2040 SPI0, STM32G474 SPI1, or ESP32-S3 SPI2
 *   - bus 1 -> RP2040 SPI1, STM32G474 SPI2, or ESP32-S3 SPI3
 * Any other bus value is rejected by status-returning operations. Low-level
 * synchronization and cleanup helpers retain HAL_ASSERT checks.
 */

#include <stddef.h>
#include <stdint.h>

#include "hal/core/hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default SPI clock used when settings request clock 0: 4 MHz. */
#define HAL_SPI_CLOCK_DEFAULT_HZ 4000000UL

/** @brief Bit order values matching LSBFIRST / MSBFIRST semantics. */
#define HAL_SPI_LSBFIRST 0u
#define HAL_SPI_MSBFIRST 1u

/** @brief SPI mode values matching SPI_MODE0..SPI_MODE3 semantics. */
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

/** Status-returning companions for APIs whose historical return value must be
 * preserved. The value-returning APIs below remain compatibility wrappers. */
hal_status_t hal_spi_transfer_ex(uint8_t bus, uint8_t data,
                                 uint8_t *out_received);
hal_status_t hal_spi_transfer16_ex(uint8_t bus, uint16_t data,
                                   uint16_t *out_received);
hal_status_t hal_spi_write_dma_ex(uint8_t bus, const uint8_t *data, size_t len);
hal_status_t hal_spi_write_dma_async_start_ex(uint8_t bus, const uint8_t *data,
                                              size_t len);
hal_status_t hal_spi_write_dma_async_wait_ex(uint8_t bus);

/**
 * @brief Configure SPI pins and start the bus in controller (master) mode.
 * @param bus     SPI controller index (0 = SPI, 1 = SPI1).
 * @param rx_pin  MISO pin number.
 * @param tx_pin  MOSI pin number.
 * @param sck_pin SCK  pin number.
 * @return HAL_OK on success, HAL_EINVAL for an invalid bus, or a backend
 *         setup error.
 */
hal_status_t hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin,
                          uint8_t sck_pin);

/**
 * @brief Stop the selected SPI controller.
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 */
void hal_spi_deinit(uint8_t bus);

/**
 * @brief Acquire the mutex for the selected SPI controller.
 *
 * Use this to guard multi-step interactions with drivers sharing the same SPI
 * controller (for example MCP2515).
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
 * Configures transaction clock,
 * bit order and SPI mode, but does not acquire the HAL mutex. Code that needs
 * cross-driver exclusion should still use hal_spi_lock()/hal_spi_unlock().
 *
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @param settings Transaction settings, or NULL for HAL defaults.
 * @return HAL_OK on success, HAL_EINVAL for invalid settings, or a backend
 *         setup error.
 */
hal_status_t hal_spi_begin_transaction(uint8_t bus,
                                       const hal_spi_settings_t *settings);

/**
 * @brief Finish a transaction on the selected SPI controller.
 *
 * Waits for pending hardware transfer completion where the backend can observe
 * it, but does not release the HAL mutex.
 *
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @return HAL_OK on success or a backend completion error.
 */
hal_status_t hal_spi_end_transaction(uint8_t bus);

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
 * The byte order follows the active bit-order setting, matching the
 * configured bit-order behavior.
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
 * @return HAL_OK on success, HAL_EINVAL for invalid arguments, or a backend
 *         transfer error.
 */
hal_status_t hal_spi_transfer_buffer(uint8_t bus, uint8_t *buffer, size_t len);

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
 * @return HAL_OK on success, HAL_EINVAL for invalid arguments, or a backend
 *         transfer error.
 */
hal_status_t hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx, uint8_t *rx,
                                   size_t len);

/**
 * @brief Write a byte buffer, discarding received bytes.
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @param data Buffer to transmit.
 * @param len Number of bytes to transmit.
 * @return HAL_OK on success, HAL_EINVAL for invalid arguments, or a backend
 *         transfer error.
 */
hal_status_t hal_spi_write(uint8_t bus, const uint8_t *data, size_t len);

/**
 * @brief Write a byte buffer using the fastest backend path available.
 *
 * On RP2040 this uses a blocking SPI TX DMA transfer when possible. Backends
 * without a distinct HAL DMA path fall back to hal_spi_write(); ESP32-S3 uses
 * that fallback even when ESP-IDF allocated a DMA-capable bus.
 *
 * The caller is responsible for holding any required SPI/device transaction
 * state, exactly like for hal_spi_write().
 *
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @param data Buffer to transmit.
 * @param len Number of bytes to transmit.
 * @return true when all bytes were accepted for transmission.
 */
bool hal_spi_write_dma(uint8_t bus, const uint8_t *data, size_t len);

/**
 * @brief Start a byte-buffer write using a backend DMA path when available.
 *
 * On RP2040 and STM32G474 this starts SPI TX DMA and returns before the
 * transfer completes. The caller must keep @p data valid and must not start
 * another async transfer on the same bus until
 * hal_spi_write_dma_async_busy() is false or
 * hal_spi_write_dma_async_wait() has returned. Backends without asynchronous
 * DMA complete the write before returning.
 *
 * @param bus SPI controller index (0 = SPI, 1 = SPI1).
 * @param data Buffer to transmit.
 * @param len Number of bytes to transmit.
 * @return true when the transfer was started or completed.
 */
bool hal_spi_write_dma_async_start(uint8_t bus, const uint8_t *data,
                                   size_t len);

/**
 * @brief Return true while an asynchronous SPI DMA write is still active.
 */
bool hal_spi_write_dma_async_busy(uint8_t bus);

/**
 * @brief Wait for the asynchronous SPI DMA write on @p bus to complete.
 */
bool hal_spi_write_dma_async_wait(uint8_t bus);

#ifdef __cplusplus
}
#endif
