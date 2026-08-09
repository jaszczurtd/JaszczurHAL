#include "pn532.h"

#include "hal/hal_system.h"

#include <string.h>

static constexpr uint8_t PN532_SPI_DATAWRITE = 0x01;
static constexpr uint8_t PN532_SPI_STATREAD = 0x02;
static constexpr uint8_t PN532_SPI_DATAREAD = 0x03;
static constexpr uint8_t PN532_SPI_READY = 0x01;

PN532_SPI::PN532_SPI(uint8_t chipSelectPin, uint8_t resetPin, uint8_t bus)
    : _device{{PN532_SPI_DEFAULT_CLOCK_HZ, HAL_SPI_LSBFIRST, HAL_SPI_MODE0},
              bus,
              chipSelectPin,
              true,
              false},
      _resetPin(resetPin) {}

hal_status_t PN532_SPI::begin() {
  const hal_spi_settings_t settings = _device.settings;
  hal_status_t status =
      hal_spi_device_init(&_device, _device.bus, _device.cs_pin, &settings);
  if (hal_status_is_error(status)) {
    return status;
  }

  if (_resetPin != PN532_UNUSED_PIN) {
    hal_gpio_set_mode(_resetPin, HAL_GPIO_OUTPUT_HIGH);
    hal_gpio_write(_resetPin, false);
    hal_delay_ms(10);
    hal_gpio_write(_resetPin, true);
    hal_delay_ms(400);
  }
  return HAL_OK;
}

hal_status_t PN532_SPI::wakeup() {
  static const uint8_t wakeup_frame[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  return writeCommand(wakeup_frame, sizeof(wakeup_frame));
}

hal_status_t PN532_SPI::isReady(bool *ready) {
  if (ready == NULL) {
    return HAL_EINVAL;
  }

  uint8_t value = 0;
  hal_status_t status =
      transferFrame(PN532_SPI_STATREAD, NULL, 0, &value, sizeof(value));
  if (status == HAL_OK) {
    *ready = (value == PN532_SPI_READY);
  }
  return status;
}

hal_status_t PN532_SPI::writeCommand(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0) {
    return HAL_EINVAL;
  }
  return transferFrame(PN532_SPI_DATAWRITE, data, len, NULL, 0);
}

hal_status_t PN532_SPI::readData(uint8_t *data, size_t len) {
  if (data == NULL || len == 0) {
    return HAL_EINVAL;
  }
  return transferFrame(PN532_SPI_DATAREAD, NULL, 0, data, len);
}

hal_status_t PN532_SPI::transferFrame(uint8_t command, const uint8_t *tx,
                                      size_t tx_len, uint8_t *rx,
                                      size_t rx_len) {
  if (rx_len > 0u) {
    memset(rx, 0, rx_len);
  }
  const hal_spi_device_operation_t operations[] = {
      {HAL_SPI_DEVICE_OP_WRITE, &command, NULL, 1u},
      {HAL_SPI_DEVICE_OP_WRITE, tx, NULL, tx_len},
      {HAL_SPI_DEVICE_OP_TRANSFER_IN_PLACE, NULL, rx, rx_len},
  };
  return hal_spi_device_transaction(&_device, operations,
                                    sizeof(operations) / sizeof(operations[0]));
}
