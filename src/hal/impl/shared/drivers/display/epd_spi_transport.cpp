#include "epd_spi_transport.h"

/* SPDX-License-Identifier: Apache-2.0 */

#include "hal/hal_config.h"

#if defined(HAL_ENABLE_SSD16XX) || defined(HAL_ENABLE_UC81XX)

#include "hal/hal_gpio.h"
#include "hal/hal_spi.h"
#include "hal/hal_spi_device.h"
#include "hal/hal_system.h"

#include <string.h>

static bool pin_is_connected(int16_t pin) { return pin >= 0 && pin <= 255; }
static uint8_t pin_to_u8(int16_t pin) { return (uint8_t)pin; }

static uint32_t normalized_clock(const jh_epd_spi_config_t *config) {
  return config->clock_hz == 0u ? JH_EPD_DEFAULT_SPI_HZ : config->clock_hz;
}

static uint32_t normalized_timeout(const jh_epd_spi_config_t *config) {
  return config->busy_timeout_ms == 0u ? JH_EPD_DEFAULT_BUSY_TIMEOUT_MS
                                       : config->busy_timeout_ms;
}

static uint8_t normalized_spi_mode(const jh_epd_spi_config_t *config) {
  return config->spi_mode <= HAL_SPI_MODE3 ? config->spi_mode : HAL_SPI_MODE0;
}

static hal_status_t begin_transaction(jh_epd_spi_t *transport) {
  hal_status_t status = jh_epd_spi_wait_idle(transport);
  if (hal_status_is_error(status)) {
    return status;
  }
  return hal_spi_device_acquire(&transport->spi_device);
}

static hal_status_t write_command_prefix(jh_epd_spi_t *transport,
                                         uint8_t command) {
  hal_gpio_write(pin_to_u8(transport->config.dc_pin), false);
  return hal_spi_write(transport->spi_device.bus, &command, 1u);
}

hal_status_t jh_epd_spi_init(jh_epd_spi_t *transport,
                             const jh_epd_spi_config_t *config) {
  if (transport == NULL || config == NULL ||
      !pin_is_connected(config->dc_pin)) {
    return HAL_EINVAL;
  }
  memset(transport, 0, sizeof(*transport));
  transport->config = *config;
  transport->config.clock_hz = normalized_clock(config);
  transport->config.busy_timeout_ms = normalized_timeout(config);
  transport->config.spi_mode = normalized_spi_mode(config);

  const hal_spi_settings_t settings = {
      transport->config.clock_hz, HAL_SPI_MSBFIRST, transport->config.spi_mode};
  const uint8_t cs_pin = pin_is_connected(config->cs_pin)
                             ? pin_to_u8(config->cs_pin)
                             : HAL_SPI_DEVICE_CS_NONE;
  hal_status_t status = hal_spi_device_init(
      &transport->spi_device, transport->config.bus, cs_pin, &settings);
  if (hal_status_is_error(status)) {
    return status;
  }
  hal_gpio_set_mode(pin_to_u8(config->dc_pin), HAL_GPIO_OUTPUT);
  hal_gpio_write(pin_to_u8(config->dc_pin), true);
  if (pin_is_connected(config->rst_pin)) {
    hal_gpio_set_mode(pin_to_u8(config->rst_pin), HAL_GPIO_OUTPUT);
    hal_gpio_write(pin_to_u8(config->rst_pin), true);
  }
  if (pin_is_connected(config->busy_pin)) {
    hal_gpio_set_mode(pin_to_u8(config->busy_pin), HAL_GPIO_INPUT);
  }
  transport->initialized = true;
  return HAL_OK;
}

hal_status_t jh_epd_spi_reset(jh_epd_spi_t *transport,
                              uint32_t pulse_delay_ms) {
  if (transport == NULL || !transport->initialized) {
    return HAL_EUNINIT;
  }
  if (!pin_is_connected(transport->config.rst_pin)) {
    return jh_epd_spi_wait_idle(transport);
  }
  const uint8_t rst = pin_to_u8(transport->config.rst_pin);
  const uint32_t delay_ms = pulse_delay_ms == 0u ? 1u : pulse_delay_ms;
  hal_gpio_write(rst, true);
  hal_delay_ms(delay_ms);
  hal_gpio_write(rst, false);
  hal_delay_ms(delay_ms);
  hal_gpio_write(rst, true);
  hal_delay_ms(delay_ms);
  return jh_epd_spi_wait_idle(transport);
}

hal_status_t jh_epd_spi_wait_idle(jh_epd_spi_t *transport) {
  if (transport == NULL || !transport->initialized) {
    return HAL_EUNINIT;
  }
  if (!pin_is_connected(transport->config.busy_pin)) {
    return HAL_OK;
  }
  const uint32_t started = hal_millis();
  const uint8_t busy_pin = pin_to_u8(transport->config.busy_pin);
  while (hal_gpio_read(busy_pin) == transport->config.busy_active_high) {
    if ((uint32_t)(hal_millis() - started) >=
        transport->config.busy_timeout_ms) {
      return HAL_ETIMEOUT;
    }
    hal_delay_ms(1u);
  }
  return HAL_OK;
}

hal_status_t jh_epd_spi_command(jh_epd_spi_t *transport, uint8_t command,
                                const uint8_t *data, size_t len) {
  if (transport == NULL || !transport->initialized ||
      (len > 0u && data == NULL)) {
    return transport == NULL || !transport->initialized ? HAL_EUNINIT
                                                        : HAL_EINVAL;
  }
  hal_status_t status = begin_transaction(transport);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = write_command_prefix(transport, command);
  if (hal_status_is_ok(status) && len > 0u) {
    hal_gpio_write(pin_to_u8(transport->config.dc_pin), true);
    status = hal_spi_write(transport->spi_device.bus, data, len);
  }
  return hal_spi_device_finish(&transport->spi_device, status);
}

hal_status_t jh_epd_spi_command_pattern(jh_epd_spi_t *transport,
                                        uint8_t command, uint8_t pattern,
                                        size_t len) {
  if (transport == NULL || !transport->initialized) {
    return HAL_EUNINIT;
  }
  hal_status_t status = begin_transaction(transport);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = write_command_prefix(transport, command);
  if (hal_status_is_ok(status)) {
    uint8_t chunk[64];
    memset(chunk, pattern, sizeof(chunk));
    hal_gpio_write(pin_to_u8(transport->config.dc_pin), true);
    while (len > 0u && hal_status_is_ok(status)) {
      const size_t write_len = len < sizeof(chunk) ? len : sizeof(chunk);
      status = hal_spi_write(transport->spi_device.bus, chunk, write_len);
      len -= write_len;
    }
  }
  return hal_spi_device_finish(&transport->spi_device, status);
}

#endif
