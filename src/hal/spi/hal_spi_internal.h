#ifndef JH_HAL_SPI_INTERNAL_H
#define JH_HAL_SPI_INTERNAL_H

#include "hal/spi/hal_spi.h"

hal_status_t jh_hal_spi_transfer16_provider(uint8_t bus, uint16_t data,
                                            uint16_t *out_received);
hal_status_t jh_hal_spi_write_provider(uint8_t bus, const uint8_t *data,
                                       size_t len);
hal_status_t jh_hal_spi_transfer_txrx_generic(uint8_t bus,
                                              const uint8_t *tx_data,
                                              uint8_t *rx_data, size_t len);

#endif
