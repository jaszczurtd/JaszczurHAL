#include "pn532.h"

#ifdef HAL_ENABLE_UART

#include "hal/hal_system.h"

PN532_UART::PN532_UART(hal_uart_port_t port, uint8_t rxPin, uint8_t txPin,
                       uint8_t resetPin)
    : _uart(NULL), _port(port), _rxPin(rxPin), _txPin(txPin),
      _resetPin(resetPin), _ownsUart(true) {}

PN532_UART::PN532_UART(hal_uart_t uart, uint8_t resetPin)
    : _uart(uart), _port(HAL_UART_PORT_1), _rxPin(0), _txPin(0),
      _resetPin(resetPin), _ownsUart(false) {}

PN532_UART::~PN532_UART() {
  if (_ownsUart && _uart != NULL) {
    hal_uart_destroy(_uart);
    _uart = NULL;
  }
}

hal_status_t PN532_UART::begin() {
  if (_resetPin != PN532_UNUSED_PIN) {
    hal_gpio_set_mode(_resetPin, HAL_GPIO_OUTPUT_HIGH);
    hal_gpio_write(_resetPin, false);
    hal_delay_ms(10);
    hal_gpio_write(_resetPin, true);
    hal_delay_ms(400);
  }

  if (_uart == NULL && _ownsUart) {
    _uart = hal_uart_create(_port, _rxPin, _txPin);
  }
  if (_uart == NULL) {
    return HAL_ENOMEM;
  }
  hal_uart_begin(_uart, 115200, HAL_UART_CFG_8N1);
  return HAL_OK;
}

hal_status_t PN532_UART::wakeup() {
  static const uint8_t wakeup_frame[] = {0x55, 0x55, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00};
  return writeCommand(wakeup_frame, sizeof(wakeup_frame));
}

hal_status_t PN532_UART::isReady(bool *ready) {
  if (ready == NULL) {
    return HAL_EINVAL;
  }
  *ready = (_uart != NULL && hal_uart_available(_uart) > 0);
  return HAL_OK;
}

hal_status_t PN532_UART::writeCommand(const uint8_t *data, size_t len) {
  if (_uart == NULL || data == NULL || len == 0) {
    return HAL_EINVAL;
  }
  size_t written = hal_uart_write(_uart, data, len);
  hal_uart_flush(_uart);
  return (written == len) ? HAL_OK : HAL_EIO;
}

hal_status_t PN532_UART::readData(uint8_t *data, size_t len) {
  if (_uart == NULL || data == NULL || len == 0) {
    return HAL_EINVAL;
  }

  for (size_t i = 0; i < len; ++i) {
    hal_status_t status = readByte(&data[i], PN532_DEFAULT_TIMEOUT_MS);
    if (status != HAL_OK) {
      return status;
    }
  }
  return HAL_OK;
}

hal_status_t PN532_UART::readByte(uint8_t *value, uint16_t timeout_ms) {
  if (value == NULL || _uart == NULL) {
    return HAL_EINVAL;
  }

  const uint32_t start = hal_millis();
  while ((uint32_t)(hal_millis() - start) < timeout_ms) {
    int read = hal_uart_read(_uart);
    if (read >= 0) {
      *value = (uint8_t)read;
      return HAL_OK;
    }
    hal_delay_ms(1);
  }
  return HAL_ETIMEOUT;
}

#endif
