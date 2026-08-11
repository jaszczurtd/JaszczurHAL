#ifndef JH_HAL_SWSERIAL_COMMON_H
#define JH_HAL_SWSERIAL_COMMON_H

#include "hal/serial/hal_swserial.h"
#include "hal/system/hal_sync.h"

#include <string.h>

static inline bool jh_swserial_config_valid(uint16_t config) {
  const uint16_t data = config & 0x0700u;
  const uint16_t stop = config & 0x0030u;
  const uint16_t parity = config & 0x0003u;
  return (config & (uint16_t)~0x0733u) == 0u &&
         (data == HAL_UART_DATA_5 || data == HAL_UART_DATA_6 ||
          data == HAL_UART_DATA_7 || data == HAL_UART_DATA_8) &&
         (stop == HAL_UART_STOP_BIT_1 || stop == HAL_UART_STOP_BIT_2) &&
         (parity == HAL_UART_PARITY_NONE || parity == HAL_UART_PARITY_EVEN ||
          parity == HAL_UART_PARITY_ODD);
}

static inline int jh_swserial_parity(uint32_t data) {
  data ^= data >> 4u;
  data &= 0x0Fu;
  return (int)((0x6996u >> data) & 1u);
}

static inline uint8_t jh_swserial_data_bits(uint16_t config) {
  switch (config & 0x0700u) {
  case HAL_UART_DATA_5:
    return 5u;
  case HAL_UART_DATA_6:
    return 6u;
  case HAL_UART_DATA_7:
    return 7u;
  default:
    return 8u;
  }
}

static inline uint8_t jh_swserial_stop_bits(uint16_t config) {
  return (config & 0x0030u) == HAL_UART_STOP_BIT_2 ? 2u : 1u;
}

template <typename Handle, typename PinValidator>
static hal_status_t jh_swserial_set_pin(Handle handle, uint8_t pin,
                                        bool receive, PinValidator pin_valid) {
  if (handle == NULL || handle->mutex == NULL || !pin_valid(pin)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  uint8_t &selected = receive ? handle->rx_pin : handle->tx_pin;
  const uint8_t other = receive ? handle->tx_pin : handle->rx_pin;
  hal_status_t status = HAL_OK;
  if (pin == selected) {
    status = HAL_OK;
  } else if (pin == other) {
    status = HAL_EINVAL;
  } else if (handle->started) {
    status = HAL_ESTATE;
  } else {
    selected = pin;
  }
  hal_mutex_unlock(handle->mutex);
  return status;
}

template <typename Handle, typename Writer>
static hal_status_t jh_swserial_write_common(Handle handle, const uint8_t *data,
                                             size_t len, size_t *out_written,
                                             Writer write_locked) {
  if (out_written != NULL) {
    *out_written = 0u;
  }
  if (handle == NULL || handle->mutex == NULL || (len > 0u && data == NULL)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  if (!handle->started) {
    hal_mutex_unlock(handle->mutex);
    return HAL_EUNINIT;
  }
  const size_t written = write_locked(handle, data, len);
  hal_mutex_unlock(handle->mutex);
  if (out_written != NULL) {
    *out_written = written;
  }
  return HAL_OK;
}

template <typename Handle, typename Writer>
static hal_status_t jh_swserial_println_common(Handle handle, const char *text,
                                               size_t *out_written,
                                               Writer write_locked) {
  if (out_written != NULL) {
    *out_written = 0u;
  }
  if (handle == NULL || handle->mutex == NULL) {
    return HAL_EINVAL;
  }
  text = text != NULL ? text : "";
  const size_t len = strlen(text);
  static const uint8_t crlf[] = {'\r', '\n'};
  hal_mutex_lock(handle->mutex);
  if (!handle->started) {
    hal_mutex_unlock(handle->mutex);
    return HAL_EUNINIT;
  }
  const size_t written = write_locked(handle, (const uint8_t *)text, len);
  (void)write_locked(handle, crlf, sizeof(crlf));
#if HAL_TARGET_IS_MOCK
  size_t copy_len = len;
  if (copy_len >= sizeof(handle->last_write)) {
    copy_len = sizeof(handle->last_write) - 1u;
  }
  memcpy(handle->last_write, text, copy_len);
  handle->last_write[copy_len] = '\0';
#endif
  hal_mutex_unlock(handle->mutex);
  if (out_written != NULL) {
    *out_written = written;
  }
  return HAL_OK;
}

template <typename Handle, typename FlushLocked>
static hal_status_t jh_swserial_flush_common(Handle handle,
                                             FlushLocked flush_locked) {
  if (handle == NULL || handle->mutex == NULL) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  if (!handle->started) {
    hal_mutex_unlock(handle->mutex);
    return HAL_EUNINIT;
  }
  flush_locked(handle);
  hal_mutex_unlock(handle->mutex);
  return HAL_OK;
}

#endif
