#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_S3

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_UART

#include "hal/core/hal_mutex_once.h"
#include "hal/impl/esp32/jh_esp32_status.h"
#include "hal/serial/hal_uart.h"
#include "hal/system/hal_sync.h"
#include "jh_esp32_gpio.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <limits.h>
#include <string.h>

namespace {

constexpr int kUartRxBufferSize = 512;
constexpr int kUartEventQueueSize = 32;
constexpr BaseType_t kNoOwnerCore = -1;

struct Esp32UartFormat {
  uart_word_length_t data_bits;
  uart_parity_t parity;
  uart_stop_bits_t stop_bits;
};

} // namespace

struct hal_uart_impl_s {
  hal_uart_port_t port;
  uart_port_t idf_port;
  uint8_t rx_pin;
  uint8_t tx_pin;
  bool in_use;
  bool running;
  BaseType_t owner_core;
  QueueHandle_t event_queue;
  hal_uart_error_counters_t errors;
  hal_mutex_t mutex;
};

namespace {

hal_uart_impl_t s_pool[HAL_UART_MAX_INSTANCES] = {};
hal_mutex_t s_pool_mutex = nullptr;

bool board_pin_available(uint8_t pin) {
  const bool board_accessible =
      jh_esp32_mask_has_pin(HAL_BOARD_GPIO_EXPOSED_MASK, pin) ||
      jh_esp32_mask_has_pin(HAL_BOARD_GPIO_SOFT_RESERVED_MASK, pin);
  return jh_esp32_mask_has_pin(HAL_TARGET_GPIO_VALID_MASK, pin) &&
         board_accessible &&
         !jh_esp32_mask_has_pin(HAL_BOARD_GPIO_HARD_RESERVED_MASK, pin);
}

bool uart_rx_pin_valid(uint8_t pin) {
  return board_pin_available(pin) && GPIO_IS_VALID_GPIO((int)pin);
}

bool uart_tx_pin_valid(uint8_t pin) {
  return board_pin_available(pin) &&
         !jh_esp32_mask_has_pin(HAL_TARGET_GPIO_INPUT_ONLY_MASK, pin) &&
         GPIO_IS_VALID_OUTPUT_GPIO((int)pin);
}

bool uart_select_port(hal_uart_port_t port, uart_port_t *idf_port) {
  if (idf_port == nullptr) {
    return false;
  }
  switch (port) {
  case HAL_UART_PORT_1:
    *idf_port = UART_NUM_1;
    return true;
  case HAL_UART_PORT_2:
    *idf_port = UART_NUM_2;
    return true;
  default:
    return false;
  }
}

bool uart_handle_valid(hal_uart_t handle) {
  if (handle == nullptr) {
    return false;
  }
  for (const hal_uart_impl_t &candidate : s_pool) {
    if (&candidate == handle) {
      return candidate.in_use;
    }
  }
  return false;
}

void uart_lock(hal_uart_t handle) {
  if (handle != nullptr && handle->mutex != nullptr) {
    hal_mutex_lock(handle->mutex);
  }
}

void uart_unlock(hal_uart_t handle) {
  if (handle != nullptr && handle->mutex != nullptr) {
    hal_mutex_unlock(handle->mutex);
  }
}

bool uart_decode_format(uint16_t config, Esp32UartFormat *format) {
  if (format == nullptr) {
    return false;
  }

  switch (config & UINT16_C(0x0F00)) {
  case HAL_UART_DATA_5:
    format->data_bits = UART_DATA_5_BITS;
    break;
  case HAL_UART_DATA_6:
    format->data_bits = UART_DATA_6_BITS;
    break;
  case HAL_UART_DATA_7:
    format->data_bits = UART_DATA_7_BITS;
    break;
  case HAL_UART_DATA_8:
    format->data_bits = UART_DATA_8_BITS;
    break;
  default:
    return false;
  }

  switch (config & UINT16_C(0x000F)) {
  case HAL_UART_PARITY_NONE:
    format->parity = UART_PARITY_DISABLE;
    break;
  case HAL_UART_PARITY_EVEN:
    format->parity = UART_PARITY_EVEN;
    break;
  case HAL_UART_PARITY_ODD:
    format->parity = UART_PARITY_ODD;
    break;
  default:
    return false;
  }

  switch (config & UINT16_C(0x00F0)) {
  case HAL_UART_STOP_BIT_1:
    format->stop_bits = UART_STOP_BITS_1;
    break;
  case HAL_UART_STOP_BIT_2:
    format->stop_bits = UART_STOP_BITS_2;
    break;
  default:
    return false;
  }

  return (config & UINT16_C(0xF000)) == 0u;
}

void uart_drain_events_locked(hal_uart_t handle) {
  if (!handle->running || handle->event_queue == nullptr) {
    return;
  }

  uart_event_t event = {};
  while (xQueueReceive(handle->event_queue, &event, 0u) == pdTRUE) {
    switch (event.type) {
    case UART_FIFO_OVF:
      ++handle->errors.rx_overrun;
      break;
    case UART_BUFFER_FULL:
      ++handle->errors.rx_buffer_overflow;
      break;
    case UART_BREAK:
      ++handle->errors.rx_break;
      break;
    case UART_FRAME_ERR:
      ++handle->errors.rx_framing;
      break;
    case UART_PARITY_ERR:
      ++handle->errors.rx_parity;
      break;
    default:
      break;
    }
  }
}

hal_status_t uart_stop_locked(hal_uart_t handle) {
  if (!handle->running) {
    return HAL_OK;
  }
  if (handle->owner_core != xPortGetCoreID()) {
    return HAL_ESTATE;
  }

  uart_drain_events_locked(handle);
  esp_err_t error = uart_wait_tx_done(handle->idf_port, portMAX_DELAY);
  if (error != ESP_OK) {
    return jh_esp32_status_from_esp_err(error);
  }
  error = uart_driver_delete(handle->idf_port);
  if (error != ESP_OK) {
    return jh_esp32_status_from_esp_err(error);
  }
  handle->running = false;
  handle->owner_core = kNoOwnerCore;
  handle->event_queue = nullptr;
  return HAL_OK;
}

hal_status_t uart_write_locked(hal_uart_t handle, const uint8_t *data,
                               size_t length, size_t *written) {
  if (length == 0u) {
    return HAL_OK;
  }
  if (length > (size_t)INT_MAX) {
    return HAL_EOVERFLOW;
  }

  const int result = uart_write_bytes(handle->idf_port, data, length);
  if (result < 0) {
    return HAL_EIO;
  }
  *written = (size_t)result;
  return *written == length ? HAL_OK : HAL_EIO;
}

} // namespace

hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin,
                           uint8_t tx_pin) {
  uart_port_t idf_port = UART_NUM_MAX;
  if (!uart_select_port(port, &idf_port) || !uart_rx_pin_valid(rx_pin) ||
      !uart_tx_pin_valid(tx_pin)) {
    return nullptr;
  }

  hal_mutex_t pool_mutex = jh_hal_mutex_create_once(&s_pool_mutex);
  if (pool_mutex == nullptr) {
    return nullptr;
  }
  hal_mutex_lock(pool_mutex);

  const int capacity = hal_get_config()->uart_max_instances;
  for (int index = 0; index < capacity; ++index) {
    if (s_pool[index].in_use && s_pool[index].port == port) {
      hal_mutex_unlock(pool_mutex);
      HAL_ASSERT(false, "hal_uart: port already in use");
      return nullptr;
    }
  }

  for (int index = 0; index < capacity; ++index) {
    if (!s_pool[index].in_use) {
      hal_mutex_t mutex = hal_mutex_create();
      if (mutex == nullptr) {
        hal_mutex_unlock(pool_mutex);
        return nullptr;
      }
      hal_uart_impl_t &slot = s_pool[index];
      memset(&slot, 0, sizeof(slot));
      slot.port = port;
      slot.idf_port = idf_port;
      slot.rx_pin = rx_pin;
      slot.tx_pin = tx_pin;
      slot.in_use = true;
      slot.owner_core = kNoOwnerCore;
      slot.mutex = mutex;
      hal_mutex_unlock(pool_mutex);
      return &slot;
    }
  }

  hal_mutex_unlock(pool_mutex);
  HAL_ASSERT(false,
             "hal_uart: pool exhausted - increase HAL_UART_MAX_INSTANCES");
  return nullptr;
}

hal_status_t hal_uart_set_rx_ex(hal_uart_t handle, uint8_t rx_pin) {
  if (!uart_handle_valid(handle) || !uart_rx_pin_valid(rx_pin)) {
    return HAL_EINVAL;
  }
  uart_lock(handle);
  if (handle->running && handle->rx_pin != rx_pin) {
    uart_unlock(handle);
    return HAL_ESTATE;
  }
  handle->rx_pin = rx_pin;
  uart_unlock(handle);
  return HAL_OK;
}

bool hal_uart_set_rx(hal_uart_t handle, uint8_t rx_pin) {
  return hal_status_to_bool(hal_uart_set_rx_ex(handle, rx_pin));
}

hal_status_t hal_uart_set_tx_ex(hal_uart_t handle, uint8_t tx_pin) {
  if (!uart_handle_valid(handle) || !uart_tx_pin_valid(tx_pin)) {
    return HAL_EINVAL;
  }
  uart_lock(handle);
  if (handle->running && handle->tx_pin != tx_pin) {
    uart_unlock(handle);
    return HAL_ESTATE;
  }
  handle->tx_pin = tx_pin;
  uart_unlock(handle);
  return HAL_OK;
}

bool hal_uart_set_tx(hal_uart_t handle, uint8_t tx_pin) {
  return hal_status_to_bool(hal_uart_set_tx_ex(handle, tx_pin));
}

hal_status_t hal_uart_begin(hal_uart_t handle, uint32_t baud, uint16_t config) {
  Esp32UartFormat format = {};
  if (!uart_handle_valid(handle) || baud == 0u || baud > UART_BITRATE_MAX ||
      !uart_decode_format(config, &format)) {
    return HAL_EINVAL;
  }

  uart_lock(handle);
  if (!uart_rx_pin_valid(handle->rx_pin) ||
      !uart_tx_pin_valid(handle->tx_pin)) {
    uart_unlock(handle);
    return HAL_EINVAL;
  }

  hal_status_t status = uart_stop_locked(handle);
  if (status != HAL_OK) {
    uart_unlock(handle);
    return status;
  }
  if (uart_is_driver_installed(handle->idf_port)) {
    uart_unlock(handle);
    return HAL_EBUSY;
  }

  uart_config_t idf_config = {};
  idf_config.baud_rate = (int)baud;
  idf_config.data_bits = format.data_bits;
  idf_config.parity = format.parity;
  idf_config.stop_bits = format.stop_bits;
  idf_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  idf_config.rx_flow_ctrl_thresh = 0u;
  idf_config.source_clk = UART_SCLK_DEFAULT;

  esp_err_t error = uart_param_config(handle->idf_port, &idf_config);
  if (error == ESP_OK) {
    error =
        uart_set_pin(handle->idf_port, (int)handle->tx_pin, (int)handle->rx_pin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  }

  QueueHandle_t event_queue = nullptr;
  if (error == ESP_OK) {
    error = uart_driver_install(handle->idf_port, kUartRxBufferSize, 0,
                                kUartEventQueueSize, &event_queue, 0);
  }
  if (error != ESP_OK) {
    uart_unlock(handle);
    return jh_esp32_status_from_esp_err(error);
  }

  handle->event_queue = event_queue;
  handle->owner_core = xPortGetCoreID();
  handle->errors = {};
  handle->running = true;
  uart_unlock(handle);
  return HAL_OK;
}

int hal_uart_available(hal_uart_t handle) {
  if (!uart_handle_valid(handle)) {
    return 0;
  }
  uart_lock(handle);
  if (!handle->running) {
    uart_unlock(handle);
    return 0;
  }

  uart_drain_events_locked(handle);
  size_t available = 0u;
  const esp_err_t error =
      uart_get_buffered_data_len(handle->idf_port, &available);
  uart_unlock(handle);
  return error == ESP_OK ? (int)available : 0;
}

hal_status_t hal_uart_read_ex(hal_uart_t handle, uint8_t *out_value) {
  if (out_value == nullptr) {
    return HAL_EINVAL;
  }
  *out_value = 0u;
  if (!uart_handle_valid(handle)) {
    return HAL_EINVAL;
  }

  uart_lock(handle);
  if (!handle->running) {
    uart_unlock(handle);
    return HAL_EUNINIT;
  }
  uart_drain_events_locked(handle);
  const int result = uart_read_bytes(handle->idf_port, out_value, 1u, 0u);
  uart_unlock(handle);
  if (result == 1) {
    return HAL_OK;
  }
  *out_value = 0u;
  return result == 0 ? HAL_EAGAIN : HAL_EIO;
}

int hal_uart_read(hal_uart_t handle) {
  uint8_t value = 0u;
  return hal_status_to_bool(hal_uart_read_ex(handle, &value)) ? (int)value : -1;
}

hal_status_t hal_uart_write_ex(hal_uart_t handle, const uint8_t *data,
                               size_t length, size_t *out_written) {
  if (out_written != nullptr) {
    *out_written = 0u;
  }
  if (!uart_handle_valid(handle) || (length > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }

  uart_lock(handle);
  if (!handle->running) {
    uart_unlock(handle);
    return HAL_EUNINIT;
  }
  uart_drain_events_locked(handle);
  size_t written = 0u;
  const hal_status_t status = uart_write_locked(handle, data, length, &written);
  uart_unlock(handle);
  if (out_written != nullptr) {
    *out_written = written;
  }
  return status;
}

size_t hal_uart_write(hal_uart_t handle, const uint8_t *data, size_t length) {
  size_t written = 0u;
  (void)hal_uart_write_ex(handle, data, length, &written);
  return written;
}

hal_status_t hal_uart_println_ex(hal_uart_t handle, const char *text,
                                 size_t *out_written) {
  if (out_written != nullptr) {
    *out_written = 0u;
  }
  if (!uart_handle_valid(handle)) {
    return HAL_EINVAL;
  }

  const char *line = text != nullptr ? text : "";
  const size_t line_length = strlen(line);
  uart_lock(handle);
  if (!handle->running) {
    uart_unlock(handle);
    return HAL_EUNINIT;
  }

  size_t line_written = 0u;
  hal_status_t status =
      uart_write_locked(handle, reinterpret_cast<const uint8_t *>(line),
                        line_length, &line_written);
  size_t newline_written = 0u;
  if (status == HAL_OK) {
    static const uint8_t newline[] = {'\r', '\n'};
    status =
        uart_write_locked(handle, newline, sizeof(newline), &newline_written);
  }
  uart_unlock(handle);

  if (out_written != nullptr) {
    *out_written = line_written + newline_written;
  }
  return status;
}

size_t hal_uart_println(hal_uart_t handle, const char *text) {
  size_t written = 0u;
  (void)hal_uart_println_ex(handle, text, &written);
  return written;
}

hal_status_t hal_uart_flush(hal_uart_t handle) {
  if (!uart_handle_valid(handle)) {
    return HAL_EINVAL;
  }
  uart_lock(handle);
  if (!handle->running) {
    uart_unlock(handle);
    return HAL_EUNINIT;
  }
  uart_drain_events_locked(handle);
  const esp_err_t error = uart_wait_tx_done(handle->idf_port, portMAX_DELAY);
  uart_unlock(handle);
  return jh_esp32_status_from_esp_err(error);
}

hal_status_t
hal_uart_get_error_counters_ex(hal_uart_t handle,
                               hal_uart_error_counters_t *counters) {
  if (!uart_handle_valid(handle) || counters == nullptr) {
    return HAL_EINVAL;
  }
  uart_lock(handle);
  uart_drain_events_locked(handle);
  *counters = handle->errors;
  uart_unlock(handle);
  return HAL_OK;
}

bool hal_uart_get_error_counters(hal_uart_t handle,
                                 hal_uart_error_counters_t *counters) {
  return hal_status_to_bool(hal_uart_get_error_counters_ex(handle, counters));
}

void hal_uart_destroy(hal_uart_t handle) {
  if (!uart_handle_valid(handle)) {
    return;
  }
  hal_mutex_t pool_mutex = jh_hal_mutex_create_once(&s_pool_mutex);
  if (pool_mutex == nullptr) {
    return;
  }

  hal_mutex_lock(pool_mutex);
  hal_mutex_t handle_mutex = handle->mutex;
  hal_mutex_lock(handle_mutex);
  const hal_status_t status = uart_stop_locked(handle);
  if (status != HAL_OK) {
    hal_mutex_unlock(handle_mutex);
    hal_mutex_unlock(pool_mutex);
    HAL_ASSERT(false, "hal_uart_destroy: call from the UART owner core");
    return;
  }

  memset(handle, 0, sizeof(*handle));
  hal_mutex_unlock(handle_mutex);
  hal_mutex_destroy(handle_mutex);
  hal_mutex_unlock(pool_mutex);
}

#endif /* HAL_ENABLE_UART */
#endif /* HAL_TARGET_IS_ESP32_S3 */
