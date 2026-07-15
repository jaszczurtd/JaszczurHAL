#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#include "../../hal_uart.h"
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
};

static hal_uart_impl_t s_pool[HAL_UART_MAX_INSTANCES];

hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin,
                           uint8_t tx_pin) {
  for (int i = 0; i < hal_get_config()->uart_max_instances; i++) {
    if (s_pool[i].in_use && s_pool[i].port == port) {
      HAL_ASSERT(0, "hal_uart: port already in use");
      return NULL;
    }
  }
  for (int i = 0; i < hal_get_config()->uart_max_instances; i++) {
    if (!s_pool[i].in_use) {
      memset(&s_pool[i], 0, sizeof(s_pool[i]));
      s_pool[i].port = port;
      s_pool[i].rx_pin = rx_pin;
      s_pool[i].tx_pin = tx_pin;
      s_pool[i].in_use = 1;
      return &s_pool[i];
    }
  }

  HAL_ASSERT(0, "hal_uart: pool exhausted - increase HAL_UART_MAX_INSTANCES");
  return NULL;
}

hal_status_t hal_uart_set_rx_ex(hal_uart_t h, uint8_t rx_pin) {
  if (!h)
    return HAL_EINVAL;
  h->rx_pin = rx_pin;
  return HAL_OK;
}

bool hal_uart_set_rx(hal_uart_t h, uint8_t rx_pin) {
  return hal_status_to_bool(hal_uart_set_rx_ex(h, rx_pin));
}

hal_status_t hal_uart_set_tx_ex(hal_uart_t h, uint8_t tx_pin) {
  if (!h)
    return HAL_EINVAL;
  h->tx_pin = tx_pin;
  return HAL_OK;
}

bool hal_uart_set_tx(hal_uart_t h, uint8_t tx_pin) {
  return hal_status_to_bool(hal_uart_set_tx_ex(h, tx_pin));
}

hal_status_t hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config) {
  (void)baud;
  (void)config;
  if (!h)
    return HAL_EINVAL;
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

hal_status_t hal_uart_read_ex(hal_uart_t h, uint8_t *out_value) {
  if (!out_value)
    return HAL_EINVAL;
  *out_value = 0u;
  if (!h)
    return HAL_EINVAL;
  if (hal_uart_available(h) == 0)
    return HAL_EAGAIN;
  *out_value = h->rx_buf[h->head];
  h->head = (h->head + 1) % HAL_UART_BUF_SIZE;
  return HAL_OK;
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
  return HAL_OK;
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
#endif // HAL_TARGET_IS_MOCK
