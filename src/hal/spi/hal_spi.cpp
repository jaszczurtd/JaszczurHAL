#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_SPI

#include "hal/spi/hal_spi.h"
#include "hal/spi/hal_spi_internal.h"

uint8_t hal_spi_transfer(uint8_t bus, uint8_t data) {
  uint8_t received = 0xFFu;
  (void)hal_spi_transfer_ex(bus, data, &received);
  return received;
}

hal_status_t hal_spi_transfer16_ex(uint8_t bus, uint16_t data,
                                   uint16_t *out_received) {
  if (bus > 1u || out_received == nullptr) {
    return HAL_EINVAL;
  }
  return jh_hal_spi_transfer16_provider(bus, data, out_received);
}

uint16_t hal_spi_transfer16(uint8_t bus, uint16_t data) {
  uint16_t received = 0xFFFFu;
  (void)hal_spi_transfer16_ex(bus, data, &received);
  return received;
}

hal_status_t hal_spi_transfer_buffer(uint8_t bus, uint8_t *buffer, size_t len) {
  if (bus > 1u || (len > 0u && buffer == nullptr)) {
    return HAL_EINVAL;
  }
  return hal_spi_transfer_txrx(bus, buffer, buffer, len);
}

hal_status_t hal_spi_write(uint8_t bus, const uint8_t *data, size_t len) {
  if (bus > 1u || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  return jh_hal_spi_write_provider(bus, data, len);
}

hal_status_t jh_hal_spi_transfer_txrx_generic(uint8_t bus,
                                              const uint8_t *tx_data,
                                              uint8_t *rx_data, size_t len) {
  if (bus > 1u || (len > 0u && tx_data == nullptr && rx_data == nullptr)) {
    return HAL_EINVAL;
  }

  for (size_t i = 0; i < len; ++i) {
    const uint8_t tx_byte = tx_data != nullptr ? tx_data[i] : 0xFFu;
    uint8_t rx_byte = 0u;
    const hal_status_t status = hal_spi_transfer_ex(bus, tx_byte, &rx_byte);
    if (hal_status_is_error(status)) {
      return status;
    }
    if (rx_data != nullptr) {
      rx_data[i] = rx_byte;
    }
  }
  return HAL_OK;
}

hal_status_t hal_spi_write_dma_ex(uint8_t bus, const uint8_t *data,
                                  size_t len) {
  const hal_status_t status = hal_spi_write_dma_async_start_ex(bus, data, len);
  return hal_status_is_error(status) ? status
                                     : hal_spi_write_dma_async_wait_ex(bus);
}

bool hal_spi_write_dma(uint8_t bus, const uint8_t *data, size_t len) {
  return hal_status_to_bool(hal_spi_write_dma_ex(bus, data, len));
}

bool hal_spi_write_dma_async_start(uint8_t bus, const uint8_t *data,
                                   size_t len) {
  return hal_status_to_bool(hal_spi_write_dma_async_start_ex(bus, data, len));
}

bool hal_spi_write_dma_async_wait(uint8_t bus) {
  return hal_status_to_bool(hal_spi_write_dma_async_wait_ex(bus));
}

#endif
