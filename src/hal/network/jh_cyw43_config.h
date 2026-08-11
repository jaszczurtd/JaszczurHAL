#pragma once

#include "hal/core/hal_status.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t pin_wl_on;
  uint32_t pin_data_out;
  uint32_t pin_data_in;
  uint32_t pin_host_wake;
  uint32_t pin_clock;
  uint32_t pin_chip_select;
  uint32_t gpio_count;
  uint32_t pio_clock_div_int;
  uint8_t pio_clock_div_frac8;
} jh_cyw43_bus_config_t;

static inline hal_status_t
jh_cyw43_bus_config_validate(const jh_cyw43_bus_config_t *config) {
  if (config == NULL) {
    return HAL_EINVAL;
  }
  if (config->gpio_count == 0u || config->pio_clock_div_int == 0u) {
    return HAL_ECONFIG;
  }
  if (config->pin_wl_on >= config->gpio_count ||
      config->pin_data_out >= config->gpio_count ||
      config->pin_data_in >= config->gpio_count ||
      config->pin_host_wake >= config->gpio_count ||
      config->pin_clock >= config->gpio_count ||
      config->pin_chip_select >= config->gpio_count) {
    return HAL_EINVAL;
  }
  return HAL_OK;
}

#ifdef __cplusplus
}
#endif
