#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK || HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_UART

#include "hal/serial/hal_uart_internal.h"

hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin,
                           uint8_t tx_pin) {
  return jh_hal_uart_create_for_target(port, rx_pin, tx_pin);
}

hal_status_t hal_uart_set_rx_ex(hal_uart_t handle, uint8_t rx_pin) {
  return jh_hal_uart_set_pin_for_target(handle, rx_pin, true);
}

bool hal_uart_set_rx(hal_uart_t handle, uint8_t rx_pin) {
  return hal_status_to_bool(hal_uart_set_rx_ex(handle, rx_pin));
}

hal_status_t hal_uart_set_tx_ex(hal_uart_t handle, uint8_t tx_pin) {
  return jh_hal_uart_set_pin_for_target(handle, tx_pin, false);
}

bool hal_uart_set_tx(hal_uart_t handle, uint8_t tx_pin) {
  return hal_status_to_bool(hal_uart_set_tx_ex(handle, tx_pin));
}

#endif
#endif
