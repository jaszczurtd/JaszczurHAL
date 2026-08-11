#ifndef JH_HAL_SPI_SETTINGS_H
#define JH_HAL_SPI_SETTINGS_H

#include "hal/spi/hal_spi.h"

inline bool spi_settings_valid(const hal_spi_settings_t *settings) {
  return settings == nullptr || ((settings->bit_order == HAL_SPI_LSBFIRST ||
                                  settings->bit_order == HAL_SPI_MSBFIRST) &&
                                 settings->data_mode <= HAL_SPI_MODE3);
}

inline hal_spi_settings_t spi_default_settings(void) {
  return {HAL_SPI_CLOCK_DEFAULT_HZ, HAL_SPI_MSBFIRST, HAL_SPI_MODE0};
}

inline hal_spi_settings_t
spi_normalize_settings(const hal_spi_settings_t *settings) {
  hal_spi_settings_t normalized =
      settings != nullptr ? *settings : spi_default_settings();
  if (normalized.clock_hz == 0u) {
    normalized.clock_hz = HAL_SPI_CLOCK_DEFAULT_HZ;
  }
  if (normalized.bit_order != HAL_SPI_LSBFIRST) {
    normalized.bit_order = HAL_SPI_MSBFIRST;
  }
  if (normalized.data_mode > HAL_SPI_MODE3) {
    normalized.data_mode = HAL_SPI_MODE0;
  }
  return normalized;
}

#endif
