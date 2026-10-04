#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"
#include "hal/serial/hal_uart.h"
#include "hal/serial/hal_uart_common.h"
#include "hal/serial/hal_uart_internal.h"
#include "hal_mock.h"

#include <string.h>

#define HAL_UART_BUF_SIZE 512

struct hal_uart_impl_s {
  uint8_t rx_buf[HAL_UART_BUF_SIZE];
  char last_write[HAL_UART_BUF_SIZE];
  hal_uart_port_t port;
  uint8_t rx_pin;
  uint8_t tx_pin;
  int head;
  int tail;
  int in_use;
  hal_uart_error_counters_t errors;
  hal_mock_uart_write_cb_t write_cb;
  void *write_cb_user;
  hal_status_t next_begin_status;
  hal_status_t next_write_status;
  hal_status_t next_flush_status;
  size_t tx_pending; /* bytes the modelled queue still holds */
};

static hal_uart_impl_t s_pool[HAL_UART_MAX_INSTANCES];

hal_uart_t jh_hal_uart_create_for_target(hal_uart_port_t port, uint8_t rx_pin,
                                         uint8_t tx_pin) {
  return jh_hal_uart_create_from_pool(
      s_pool, hal_get_config()->uart_max_instances, port, rx_pin, tx_pin);
}

hal_status_t jh_hal_uart_set_pin_for_target(hal_uart_t handle, uint8_t pin,
                                            bool receive) {
  return jh_hal_uart_set_pin(handle, pin, receive);
}

hal_status_t hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config) {
  (void)baud;
  (void)config;
  if (!h)
    return HAL_EINVAL;
  const hal_status_t injected_status = h->next_begin_status;
  h->next_begin_status = HAL_NONE;
  if (injected_status != HAL_NONE && injected_status != HAL_OK)
    return injected_status;
  h->head = 0;
  h->tail = 0;
  memset(&h->errors, 0, sizeof(h->errors));
  return HAL_OK;
}

int hal_uart_available(hal_uart_t h) {
  if (!h)
    return 0;
  return (h->tail - h->head + HAL_UART_BUF_SIZE) % HAL_UART_BUF_SIZE;
}

hal_status_t hal_uart_read_bytes_ex(hal_uart_t h, uint8_t *out, size_t size,
                                    size_t *out_read) {
  if (out_read)
    *out_read = 0u;
  if (!h || (!out && size > 0u))
    return HAL_EINVAL;
  size_t count = 0u;
  while (count < size && h->head != h->tail) {
    out[count++] = h->rx_buf[h->head];
    h->head = (h->head + 1) % HAL_UART_BUF_SIZE;
  }
  if (out_read)
    *out_read = count;
  return count > 0u || size == 0u ? HAL_OK : HAL_EAGAIN;
}

hal_status_t hal_uart_read_ex(hal_uart_t h, uint8_t *out_value) {
  if (!out_value)
    return HAL_EINVAL;
  *out_value = 0u;
  return hal_uart_read_bytes_ex(h, out_value, 1u, NULL);
}

int hal_uart_read(hal_uart_t h) {
  uint8_t value = 0u;
  return hal_status_to_bool(hal_uart_read_ex(h, &value)) ? (int)value : -1;
}

hal_status_t hal_uart_write_ex(hal_uart_t h, const uint8_t *data, size_t len,
                               size_t *out_written) {
  if (out_written)
    *out_written = 0u;
  if (!h || (len > 0u && !data))
    return HAL_EINVAL;
  const hal_status_t injected_status = h->next_write_status;
  h->next_write_status = HAL_NONE;
  if (injected_status != HAL_NONE && injected_status != HAL_OK)
    return injected_status;
  if (len == 0u)
    return HAL_OK;
  size_t copy_len = len;
  if (copy_len >= sizeof(h->last_write)) {
    copy_len = sizeof(h->last_write) - 1u;
  }

  memcpy(h->last_write, data, copy_len);
  h->last_write[copy_len] = '\0';
  if (h->write_cb)
    h->write_cb(h, h->last_write, h->write_cb_user);
  if (out_written)
    *out_written = copy_len;
  return (copy_len == len) ? HAL_OK : HAL_EOVERFLOW;
}

size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len) {
  size_t written = 0u;
  (void)hal_uart_write_ex(h, data, len, &written);
  return written;
}

/* The queue fills with every accepted message and empties only through
 * hal_mock_uart_drain_tx(), so a test can drive it full. The capture keeps
 * the message's first bytes, as for write. */
hal_status_t hal_uart_try_write_ex(hal_uart_t h, const uint8_t *data,
                                   size_t len) {
  if (!h || (len > 0u && !data))
    return HAL_EINVAL;
  if (len > (size_t)HAL_UART_TX_BUFFER_SIZE)
    return HAL_EOVERFLOW;
  if (h->tx_pending + len > (size_t)HAL_UART_TX_BUFFER_SIZE)
    return HAL_EAGAIN;
  size_t written = 0u;
  const hal_status_t status = hal_uart_write_ex(h, data, len, &written);
  if (status != HAL_OK && status != HAL_EOVERFLOW)
    return status;
  h->tx_pending += len;
  return HAL_OK;
}

hal_status_t hal_uart_tx_free_ex(hal_uart_t h, size_t *out_free) {
  if (!out_free)
    return HAL_EINVAL;
  *out_free = 0u;
  if (!h)
    return HAL_EINVAL;
  *out_free = (size_t)HAL_UART_TX_BUFFER_SIZE - h->tx_pending;
  return HAL_OK;
}

void hal_mock_uart_drain_tx(hal_uart_t h, size_t bytes) {
  if (h)
    h->tx_pending -= bytes < h->tx_pending ? bytes : h->tx_pending;
}

hal_status_t hal_uart_println_ex(hal_uart_t h, const char *s,
                                 size_t *out_written) {
  if (out_written)
    *out_written = 0u;
  if (!h)
    return HAL_EINVAL;
  const char *text = s ? s : "";
  size_t text_len = strlen(text);
  size_t total = text_len + 2; /* text + \r\n */
  if (total >= sizeof(h->last_write)) {
    total = sizeof(h->last_write) - 1u;
  }
  size_t copy_text = (text_len < total) ? text_len : total;
  memcpy(h->last_write, text, copy_text);
  if (copy_text + 1 < sizeof(h->last_write))
    h->last_write[copy_text] = '\r';
  if (copy_text + 2 < sizeof(h->last_write))
    h->last_write[copy_text + 1] = '\n';
  h->last_write[total] = '\0';
  if (h->write_cb)
    h->write_cb(h, h->last_write, h->write_cb_user);
  if (out_written)
    *out_written = total;
  return (total == text_len + 2u) ? HAL_OK : HAL_EOVERFLOW;
}

size_t hal_uart_println(hal_uart_t h, const char *s) {
  size_t written = 0u;
  (void)hal_uart_println_ex(h, s, &written);
  return written;
}

hal_status_t hal_uart_flush(hal_uart_t h) {
  if (!h)
    return HAL_EINVAL;
  const hal_status_t injected_status = h->next_flush_status;
  h->next_flush_status = HAL_NONE;
  return injected_status == HAL_NONE ? HAL_OK : injected_status;
}

hal_status_t
hal_uart_get_error_counters_ex(hal_uart_t h,
                               hal_uart_error_counters_t *counters) {
  if (!h || !counters)
    return HAL_EINVAL;
  *counters = h->errors;
  return HAL_OK;
}

bool hal_uart_get_error_counters(hal_uart_t h,
                                 hal_uart_error_counters_t *counters) {
  return hal_status_to_bool(hal_uart_get_error_counters_ex(h, counters));
}

void hal_uart_destroy(hal_uart_t h) {
  if (h) {
    h->write_cb = NULL;
    h->write_cb_user = NULL;
    h->in_use = 0;
  }
}

void hal_mock_uart_push(hal_uart_t h, const uint8_t *data, int len) {
  if (!h || !data || len <= 0)
    return;
  for (int i = 0; i < len; i++) {
    int next = (h->tail + 1) % HAL_UART_BUF_SIZE;
    if (next != h->head) {
      h->rx_buf[h->tail] = data[i];
      h->tail = next;
    } else {
      h->errors.rx_buffer_overflow++;
    }
  }
}

void hal_mock_uart_reset(hal_uart_t h) {
  if (!h)
    return;
  h->head = 0;
  h->tail = 0;
  h->last_write[0] = '\0';
  memset(&h->errors, 0, sizeof(h->errors));
  h->next_begin_status = HAL_NONE;
  h->next_write_status = HAL_NONE;
  h->next_flush_status = HAL_NONE;
  h->tx_pending = 0u;
}

const char *hal_mock_uart_last_write(hal_uart_t h) {
  return h ? h->last_write : "";
}

uint8_t hal_mock_uart_get_rx_pin(hal_uart_t h) { return h ? h->rx_pin : 0u; }

uint8_t hal_mock_uart_get_tx_pin(hal_uart_t h) { return h ? h->tx_pin : 0u; }

void hal_mock_uart_set_write_callback(hal_uart_t h, hal_mock_uart_write_cb_t cb,
                                      void *user) {
  if (!h)
    return;
  h->write_cb = cb;
  h->write_cb_user = user;
}

void hal_mock_uart_set_next_begin_status(hal_uart_t h, hal_status_t status) {
  if (h)
    h->next_begin_status = status;
}

void hal_mock_uart_set_next_write_status(hal_uart_t h, hal_status_t status) {
  if (h)
    h->next_write_status = status;
}

void hal_mock_uart_set_next_flush_status(hal_uart_t h, hal_status_t status) {
  if (h)
    h->next_flush_status = status;
}
#endif // HAL_TARGET_IS_MOCK
