#ifndef JH_HAL_UART_INTERNAL_H
#define JH_HAL_UART_INTERNAL_H

#include "hal/serial/hal_uart.h"

hal_uart_t jh_hal_uart_create_for_target(hal_uart_port_t port, uint8_t rx_pin,
                                         uint8_t tx_pin);
hal_status_t jh_hal_uart_set_pin_for_target(hal_uart_t handle, uint8_t pin,
                                            bool receive);

#endif
