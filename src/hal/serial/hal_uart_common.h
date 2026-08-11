#ifndef JH_HAL_UART_COMMON_H
#define JH_HAL_UART_COMMON_H

#include "hal/serial/hal_uart.h"

#include <string.h>

template <typename State>
static hal_uart_t jh_hal_uart_create_from_pool(State *pool, int capacity,
                                               hal_uart_port_t port,
                                               uint8_t rx_pin, uint8_t tx_pin) {
  for (int i = 0; i < capacity; ++i) {
    if (pool[i].in_use && pool[i].port == port) {
      HAL_ASSERT(0, "hal_uart: port already in use");
      return NULL;
    }
  }
  for (int i = 0; i < capacity; ++i) {
    if (!pool[i].in_use) {
      memset(&pool[i], 0, sizeof(pool[i]));
      pool[i].port = port;
      pool[i].rx_pin = rx_pin;
      pool[i].tx_pin = tx_pin;
      pool[i].in_use = 1;
      return &pool[i];
    }
  }
  HAL_ASSERT(0, "hal_uart: pool exhausted - increase HAL_UART_MAX_INSTANCES");
  return NULL;
}

template <typename Handle>
static hal_status_t jh_hal_uart_set_pin(Handle handle, uint8_t pin,
                                        bool receive) {
  if (handle == NULL) {
    return HAL_EINVAL;
  }
  if (receive) {
    handle->rx_pin = pin;
  } else {
    handle->tx_pin = pin;
  }
  return HAL_OK;
}

#endif
