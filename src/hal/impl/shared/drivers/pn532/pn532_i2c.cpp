#include "pn532.h"

#ifdef HAL_ENABLE_I2C

#include "hal/hal_system.h"

#include <cstring>

static constexpr uint8_t PN532_I2C_READY = 0x01;

PN532_I2C::PN532_I2C(uint8_t resetPin, uint8_t address, uint8_t bus)
    : _resetPin(resetPin), _address(address), _bus(bus) {}

hal_status_t PN532_I2C::begin() {
  if (_resetPin != PN532_UNUSED_PIN) {
    hal_gpio_set_mode(_resetPin, HAL_GPIO_OUTPUT_HIGH);
    hal_gpio_write(_resetPin, false);
    hal_delay_ms(10);
    hal_gpio_write(_resetPin, true);
    hal_delay_ms(400);
  }
  return HAL_OK;
}

hal_status_t PN532_I2C::wakeup() {
  static const uint8_t wakeup_frame[] = {0x00};
  return writeCommand(wakeup_frame, sizeof(wakeup_frame));
}

hal_status_t PN532_I2C::isReady(bool *ready) {
  if (ready == NULL) {
    return HAL_EINVAL;
  }

  uint8_t value = 0;
  hal_i2c_lock_bus(_bus);
  bool ok = hal_i2c_read_bytes_bus(_bus, _address, &value, sizeof(value));
  hal_i2c_unlock_bus(_bus);
  if (!ok) {
    return HAL_EIO;
  }

  *ready = (value == PN532_I2C_READY);
  return HAL_OK;
}

hal_status_t PN532_I2C::writeCommand(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0) {
    return HAL_EINVAL;
  }

  hal_i2c_lock_bus(_bus);
  hal_i2c_begin_transmission_bus(_bus, _address);
  bool ok = true;
  for (size_t i = 0; i < len; ++i) {
    ok = (hal_i2c_write_bus(_bus, data[i]) == 1u) && ok;
  }
  ok = (hal_i2c_end_transmission_bus(_bus) == 0u) && ok;
  hal_i2c_unlock_bus(_bus);
  return ok ? HAL_OK : HAL_EIO;
}

hal_status_t PN532_I2C::readData(uint8_t *data, size_t len) {
  if (data == NULL || len == 0) {
    return HAL_EINVAL;
  }
  if (len > PN532_PACKETBUFFER_SIZE) {
    return HAL_EOVERFLOW;
  }

  uint8_t buffer[PN532_PACKETBUFFER_SIZE + 1] = {};
  hal_i2c_lock_bus(_bus);
  bool ok = hal_i2c_read_bytes_bus(_bus, _address, buffer, len + 1);
  hal_i2c_unlock_bus(_bus);
  if (!ok) {
    return HAL_EIO;
  }
  if (buffer[0] != PN532_I2C_READY) {
    return HAL_EAGAIN;
  }

  std::memcpy(data, buffer + 1, len);
  return HAL_OK;
}

#endif
