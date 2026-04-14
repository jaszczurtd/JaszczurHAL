#include "../../hal_config.h"
#ifndef HAL_DISABLE_UART

#include "../../hal_uart.h"

#include <Arduino.h>
#include <SerialUART.h>
#include <string.h>

struct hal_uart_impl_s {
    SerialUART *serial;
    hal_uart_port_t port;
    uint8_t rx_pin;
    uint8_t tx_pin;
    int in_use;
};

static hal_uart_impl_t s_pool[HAL_UART_MAX_INSTANCES];

static SerialUART *hal_uart_select_port(hal_uart_port_t port) {
    switch (port) {
        case HAL_UART_PORT_1:
            return &Serial1;
        case HAL_UART_PORT_2:
            return &Serial2;
        default:
            return NULL;
    }
}

hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin, uint8_t tx_pin) {
    SerialUART *serial = hal_uart_select_port(port);
    HAL_ASSERT(serial != NULL, "hal_uart: selected UART port is not available on this target");
    if (!serial) return NULL;

    for (int i = 0; i < hal_get_config()->uart_max_instances; i++) {
        if (s_pool[i].in_use && s_pool[i].port == port) {
            HAL_ASSERT(0, "hal_uart: port already in use");
            return NULL;
        }
    }
    for (int i = 0; i < hal_get_config()->uart_max_instances; i++) {
        if (!s_pool[i].in_use) {
            memset(&s_pool[i], 0, sizeof(s_pool[i]));
            s_pool[i].serial = serial;
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

bool hal_uart_set_rx(hal_uart_t h, uint8_t rx_pin) {
    if (!h || !h->serial) return false;
    bool ok = h->serial->setRX(rx_pin);
    if (ok) h->rx_pin = rx_pin;
    return ok;
}

bool hal_uart_set_tx(hal_uart_t h, uint8_t tx_pin) {
    if (!h || !h->serial) return false;
    bool ok = h->serial->setTX(tx_pin);
    if (ok) h->tx_pin = tx_pin;
    return ok;
}

void hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config) {
    if (!h || !h->serial) return;
    h->serial->begin(baud, config);
}

int hal_uart_available(hal_uart_t h) {
    if (!h || !h->serial) return 0;
    return h->serial->available();
}

int hal_uart_read(hal_uart_t h) {
    if (!h || !h->serial) return -1;
    return h->serial->read();
}

size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len) {
    if (!h || !h->serial || !data || len == 0u) return 0u;
    return h->serial->write(data, len);
}

size_t hal_uart_println(hal_uart_t h, const char *s) {
    if (!h || !h->serial) return 0u;
    return h->serial->println(s ? s : "");
}

void hal_uart_flush(hal_uart_t h) {
    if (!h || !h->serial) return;
    h->serial->flush();
}

void hal_uart_destroy(hal_uart_t h) {
    if (!h) return;
    h->in_use = 0;
    h->serial = NULL;
}


#endif /* HAL_DISABLE_UART */
