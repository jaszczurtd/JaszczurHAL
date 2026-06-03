#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474
#include "../../hal_config.h"
#ifdef HAL_ENABLE_GPS

/* STM32G474 GPS backend: hardware-UART transport only. NMEA parsing and the
 * hal_gps_* getters live in the shared engine (impl/shared/hal_gps_core.cpp).
 * The GPS receiver is wired to USART1 (HAL_UART_PORT_1); the rx/tx pins passed
 * to hal_gps_init() are forwarded to hal_uart_create(). */

#include "../../hal_gps.h"
#include "../../hal_uart.h"
#include "../shared/hal_gps_core.h"

#ifndef HAL_GPS_UART_PORT
#define HAL_GPS_UART_PORT HAL_UART_PORT_1
#endif

static hal_uart_t s_uart = nullptr;

void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud, uint16_t config) {
    if (s_uart) return;

    hal_gps_engine_reset();
    s_uart = hal_uart_create(HAL_GPS_UART_PORT, rx_pin, tx_pin);
    if (!s_uart) {
        return;
    }
    hal_uart_begin(s_uart, baud, config);
}

void hal_gps_update(void) {
    if (!s_uart) {
        return;
    }
    while (hal_uart_available(s_uart) > 0) {
        int b = hal_uart_read(s_uart);
        if (b < 0) break;
        hal_gps_encode((char)b);
    }
}

int hal_gps_serial_available(void) {
    return s_uart ? hal_uart_available(s_uart) : -1;
}

#endif /* HAL_ENABLE_GPS */
#endif  // HAL_TARGET_IS_STM32G474
