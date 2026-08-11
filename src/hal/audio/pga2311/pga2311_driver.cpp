#include "pga2311_driver.h"

#ifdef HAL_ENABLE_PGA2311

#include "hal/gpio/hal_gpio.h"
#include "hal/spi/hal_spi.h"

static hal_spi_settings_t build_spi_settings(const hal_pga2311_config_t *cfg) {
  hal_spi_settings_t s = {
      cfg->spi_clock_hz ? cfg->spi_clock_hz
                        : (uint32_t)HAL_PGA2311_SPI_DEFAULT_HZ,
      cfg->spi_bit_order,
      cfg->spi_mode,
  };
  return s;
}

bool hal_pga2311_driver_validate_config(const hal_pga2311_config_t *cfg) {
  if (cfg == NULL) {
    return false;
  }
  if (cfg->cs_pin == HAL_PGA2311_PIN_NONE) {
    return false;
  }
  if (cfg->mute_pin != HAL_PGA2311_MUTE_PIN_NONE &&
      cfg->mute_pin == cfg->cs_pin) {
    return false;
  }
  if (cfg->spi_mode > HAL_SPI_MODE3) {
    return false;
  }
  if (cfg->spi_bit_order != HAL_SPI_MSBFIRST &&
      cfg->spi_bit_order != HAL_SPI_LSBFIRST) {
    return false;
  }
  if (cfg->mute_polarity != HAL_PGA2311_MUTE_ACTIVE_LOW &&
      cfg->mute_polarity != HAL_PGA2311_MUTE_ACTIVE_HIGH) {
    return false;
  }
  return true;
}

void hal_pga2311_driver_set_hw_mute(const hal_pga2311_config_t *cfg,
                                    bool mute) {
  if (cfg == NULL || cfg->mute_pin == HAL_PGA2311_MUTE_PIN_NONE) {
    return;
  }

  const bool level =
      (cfg->mute_polarity == HAL_PGA2311_MUTE_ACTIVE_HIGH) ? mute : !mute;
  hal_gpio_write(cfg->mute_pin, level);
}

void hal_pga2311_driver_init_pins(const hal_pga2311_config_t *cfg) {
  if (cfg == NULL) {
    return;
  }
  hal_gpio_set_mode(cfg->cs_pin, HAL_GPIO_OUTPUT);
  hal_gpio_write(cfg->cs_pin, true);

  if (cfg->mute_pin != HAL_PGA2311_MUTE_PIN_NONE) {
    hal_gpio_set_mode(cfg->mute_pin, HAL_GPIO_OUTPUT);
    hal_pga2311_driver_set_hw_mute(cfg, false);
  }
}

hal_status_t hal_pga2311_driver_write_codes(const hal_pga2311_config_t *cfg,
                                            uint8_t left_code,
                                            uint8_t right_code) {
  if (cfg == NULL) {
    return HAL_EINVAL;
  }

  const uint8_t bus = cfg->spi_bus;
  /* PGA2311 loads the 16-bit word MSB-first: the first byte is the RIGHT
   * channel gain (D15-D8), the second byte is the LEFT channel (D7-D0). */
  const uint8_t frame[2] = {right_code, left_code};
  const hal_spi_settings_t spi_settings = build_spi_settings(cfg);

  hal_spi_lock(bus);
  hal_status_t status = hal_spi_begin_transaction(bus, &spi_settings);
  if (hal_status_is_error(status)) {
    hal_spi_unlock(bus);
    return status;
  }

  hal_gpio_write(cfg->cs_pin, false);
  status = hal_spi_write(bus, frame, 2u);
  hal_gpio_write(cfg->cs_pin, true);

  const hal_status_t end_status = hal_spi_end_transaction(bus);
  hal_spi_unlock(bus);
  return hal_status_is_error(status) ? status : end_status;
}

#endif /* HAL_ENABLE_PGA2311 */
