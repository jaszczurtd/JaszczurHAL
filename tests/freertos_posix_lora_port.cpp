/**
 * @file freertos_posix_lora_port.cpp
 * @brief Link-only GPIO/SPI port for the FreeRTOS LoRa facade regression.
 */

#include "hal/gpio/hal_gpio.h"
#include "hal/spi/hal_spi.h"

void hal_gpio_set_mode(uint8_t, hal_gpio_mode_t) {}

void hal_gpio_write(uint8_t, bool) {}

bool hal_gpio_read(uint8_t) { return false; }

void hal_gpio_attach_interrupt(uint8_t, void (*)(void), hal_gpio_irq_mode_t) {}

void hal_gpio_detach_interrupt(uint8_t) {}

hal_status_t hal_spi_init(uint8_t, uint8_t, uint8_t, uint8_t) { return HAL_OK; }

void hal_spi_lock(uint8_t) {}

void hal_spi_unlock(uint8_t) {}

hal_status_t hal_spi_begin_transaction(uint8_t, const hal_spi_settings_t *) {
  return HAL_OK;
}

hal_status_t hal_spi_end_transaction(uint8_t) { return HAL_OK; }

hal_status_t hal_spi_transfer_ex(uint8_t, uint8_t, uint8_t *out_received) {
  if (out_received == nullptr) {
    return HAL_EINVAL;
  }
  *out_received = 0u;
  return HAL_OK;
}

hal_status_t hal_spi_write(uint8_t, const uint8_t *data, size_t length) {
  return length == 0u || data != nullptr ? HAL_OK : HAL_EINVAL;
}
