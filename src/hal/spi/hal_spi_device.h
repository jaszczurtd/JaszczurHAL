#pragma once

/**
 * @file hal_spi_device.h
 * @brief Target-neutral SPI device configuration and transactions.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal/spi/hal_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sentinel for an SPI device without a managed chip-select pin. */
#define HAL_SPI_DEVICE_CS_NONE UINT8_MAX

/** @brief SPI device bound to one bus, chip-select pin and settings. */
typedef struct {
  hal_spi_settings_t settings;
  uint8_t bus;
  uint8_t cs_pin;
  bool initialized;
  bool acquired;
} hal_spi_device_t;

/** @brief Operation executed inside one SPI device transaction. */
typedef enum {
  HAL_SPI_DEVICE_OP_READ = 0,
  HAL_SPI_DEVICE_OP_WRITE = 1,
  HAL_SPI_DEVICE_OP_TRANSFER = 2,
  HAL_SPI_DEVICE_OP_TRANSFER_IN_PLACE = 3,
} hal_spi_device_operation_type_t;

/** @brief Buffer operation for hal_spi_device_transaction(). */
typedef struct {
  hal_spi_device_operation_type_t type;
  const uint8_t *tx_data;
  uint8_t *rx_data;
  size_t length;
} hal_spi_device_operation_t;

/**
 * @brief Initialise a target-neutral SPI device descriptor.
 *
 * A connected chip-select pin is configured as an active-low output in its
 * inactive high state. Passing NULL settings selects the HAL SPI defaults.
 *
 * @param device Destination descriptor.
 * @param bus SPI controller index (0 or 1).
 * @param cs_pin Active-low chip-select pin or HAL_SPI_DEVICE_CS_NONE.
 * @param settings Device settings or NULL for defaults.
 * @return HAL_OK on success or HAL_EINVAL for invalid arguments/settings.
 * @note Call from single-owner initialisation context.
 */
hal_status_t hal_spi_device_init(hal_spi_device_t *device, uint8_t bus,
                                 uint8_t cs_pin,
                                 const hal_spi_settings_t *settings);

/**
 * @brief Lock the bus, apply device settings and assert chip select.
 * @return HAL_OK on success, HAL_EINVAL for an invalid descriptor, HAL_ESTATE
 *         when already acquired, or a backend transaction status.
 * @note Pair with hal_spi_device_release() in the same execution context.
 */
hal_status_t hal_spi_device_acquire(hal_spi_device_t *device);

/**
 * @brief Finish the backend transaction, deassert CS and unlock the bus.
 * @return HAL_OK on success, HAL_EINVAL/HAL_ESTATE for invalid lifecycle use,
 *         or a backend completion status.
 */
hal_status_t hal_spi_device_release(hal_spi_device_t *device);

/**
 * @brief Release a device while preserving an earlier operation status.
 *
 * Cleanup always runs. A failed operation takes priority over a release error;
 * otherwise the release status is returned.
 *
 * @param device Acquired device descriptor.
 * @param operation_status HAL_OK or the status produced by device I/O.
 * @return operation_status on operation failure, otherwise the release status.
 */
hal_status_t hal_spi_device_finish(hal_spi_device_t *device,
                                   hal_status_t operation_status);

/**
 * @brief Execute buffer operations under one device acquisition.
 *
 * READ clocks bytes with 0xFF, WRITE discards input, TRANSFER uses separate
 * buffers and TRANSFER_IN_PLACE overwrites rx_data. Zero-length operations may
 * use NULL buffers.
 *
 * @return HAL_OK on success, HAL_EINVAL for invalid operations, or the first
 *         operation/backend error. Cleanup is guaranteed after acquisition.
 */
hal_status_t
hal_spi_device_transaction(hal_spi_device_t *device,
                           const hal_spi_device_operation_t *operations,
                           size_t operation_count);

#ifdef __cplusplus
}
#endif
