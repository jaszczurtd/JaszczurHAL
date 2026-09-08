#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK || HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_UART

#include "hal/gpio/hal_gpio_common.h"
#include "hal/serial/hal_uart_internal.h"

hal_status_t jh_hal_uart_validate_config_for_target(hal_uart_port_t port,
                                                    uint8_t rx_pin,
                                                    uint8_t tx_pin) {
  return (port == HAL_UART_PORT_1 || port == HAL_UART_PORT_2) &&
                 jh_hal_gpio_pin_valid(rx_pin) && jh_hal_gpio_pin_valid(tx_pin)
             ? HAL_OK
             : HAL_EINVAL;
}

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
