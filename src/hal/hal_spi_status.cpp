#include "hal_spi.h"

static hal_status_t hal_spi_validate_bus(uint8_t bus) {
  return bus <= 1u ? HAL_OK : HAL_EINVAL;
}

static hal_status_t
hal_spi_validate_settings(const hal_spi_settings_t *settings) {
  if (settings == nullptr) {
    return HAL_OK;
  }
  if (settings->bit_order != HAL_SPI_LSBFIRST &&
      settings->bit_order != HAL_SPI_MSBFIRST) {
    return HAL_EINVAL;
  }
  return settings->data_mode <= HAL_SPI_MODE3 ? HAL_OK : HAL_EINVAL;
}

static hal_status_t hal_spi_validate_buffer(const void *buffer, size_t len) {
  return (len == 0u || buffer != nullptr) ? HAL_OK : HAL_EINVAL;
}

hal_status_t hal_spi_init_ex(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin,
                             uint8_t sck_pin) {
  if (hal_status_is_error(hal_spi_validate_bus(bus))) {
    return HAL_EINVAL;
  }
  hal_spi_init(bus, rx_pin, tx_pin, sck_pin);
  return HAL_OK;
}

hal_status_t hal_spi_begin_transaction_ex(uint8_t bus,
                                          const hal_spi_settings_t *settings) {
  if (hal_status_is_error(hal_spi_validate_bus(bus)) ||
      hal_status_is_error(hal_spi_validate_settings(settings))) {
    return HAL_EINVAL;
  }
  hal_spi_begin_transaction(bus, settings);
  return HAL_OK;
}

hal_status_t hal_spi_end_transaction_ex(uint8_t bus) {
  if (hal_status_is_error(hal_spi_validate_bus(bus))) {
    return HAL_EINVAL;
  }
  hal_spi_end_transaction(bus);
  return HAL_OK;
}

hal_status_t hal_spi_transfer_ex(uint8_t bus, uint8_t data,
                                 uint8_t *out_received) {
  if (hal_status_is_error(hal_spi_validate_bus(bus)) ||
      out_received == nullptr) {
    return HAL_EINVAL;
  }
  *out_received = hal_spi_transfer(bus, data);
  return HAL_OK;
}

hal_status_t hal_spi_transfer16_ex(uint8_t bus, uint16_t data,
                                   uint16_t *out_received) {
  if (hal_status_is_error(hal_spi_validate_bus(bus)) ||
      out_received == nullptr) {
    return HAL_EINVAL;
  }
  *out_received = hal_spi_transfer16(bus, data);
  return HAL_OK;
}

hal_status_t hal_spi_transfer_buffer_ex(uint8_t bus, uint8_t *buffer,
                                        size_t len) {
  if (hal_status_is_error(hal_spi_validate_bus(bus)) ||
      hal_status_is_error(hal_spi_validate_buffer(buffer, len))) {
    return HAL_EINVAL;
  }
  hal_spi_transfer_buffer(bus, buffer, len);
  return HAL_OK;
}

hal_status_t hal_spi_transfer_txrx_ex(uint8_t bus, const uint8_t *tx,
                                      uint8_t *rx, size_t len) {
  if (hal_status_is_error(hal_spi_validate_bus(bus))) {
    return HAL_EINVAL;
  }
  if (len > 0u && tx == nullptr && rx == nullptr) {
    return HAL_EINVAL;
  }
  hal_spi_transfer_txrx(bus, tx, rx, len);
  return HAL_OK;
}

hal_status_t hal_spi_write_ex(uint8_t bus, const uint8_t *data, size_t len) {
  if (hal_status_is_error(hal_spi_validate_bus(bus)) ||
      hal_status_is_error(hal_spi_validate_buffer(data, len))) {
    return HAL_EINVAL;
  }
  hal_spi_write(bus, data, len);
  return HAL_OK;
}

hal_status_t hal_spi_write_dma_async_start_ex(uint8_t bus, const uint8_t *data,
                                              size_t len) {
  if (hal_status_is_error(hal_spi_validate_bus(bus)) ||
      hal_status_is_error(hal_spi_validate_buffer(data, len))) {
    return HAL_EINVAL;
  }
  if (hal_spi_write_dma_async_busy(bus)) {
    return HAL_EBUSY;
  }
  return hal_status_from_bool(hal_spi_write_dma_async_start(bus, data, len),
                              HAL_EIO);
}

hal_status_t hal_spi_write_dma_async_wait_ex(uint8_t bus) {
  if (hal_status_is_error(hal_spi_validate_bus(bus))) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_spi_write_dma_async_wait(bus), HAL_EIO);
}

hal_status_t hal_spi_write_dma_ex(uint8_t bus, const uint8_t *data,
                                  size_t len) {
  hal_status_t status = hal_spi_write_dma_async_start_ex(bus, data, len);
  if (hal_status_is_error(status)) {
    return status;
  }
  return hal_spi_write_dma_async_wait_ex(bus);
}
