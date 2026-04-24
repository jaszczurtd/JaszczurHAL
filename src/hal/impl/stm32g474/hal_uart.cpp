#include "../../hal_config.h"
#ifndef HAL_DISABLE_UART

#include "../../hal_uart.h"

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
};

static hal_uart_impl_t s_pool[HAL_UART_MAX_INSTANCES] = {};

hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin, uint8_t tx_pin) {
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

bool hal_uart_set_rx(hal_uart_t h, uint8_t rx_pin) {
    if (!h) {
        return false;
    }
    h->rx_pin = rx_pin;
    return true;
}

bool hal_uart_set_tx(hal_uart_t h, uint8_t tx_pin) {
    if (!h) {
        return false;
    }
    h->tx_pin = tx_pin;
    return true;
}

void hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config) {
    (void)h;
    (void)baud;
    (void)config;
}

int hal_uart_available(hal_uart_t h) {
    if (!h) {
        return 0;
    }
    return (h->tail - h->head + HAL_UART_BUF_SIZE) % HAL_UART_BUF_SIZE;
}

int hal_uart_read(hal_uart_t h) {
    if (!h || hal_uart_available(h) == 0) {
        return -1;
    }

    int val = h->rx_buf[h->head];
    h->head = (h->head + 1) % HAL_UART_BUF_SIZE;
    return val;
}

size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len) {
    if (!h || !data || len == 0u) {
        return 0u;
    }

    size_t copy_len = len;
    if (copy_len >= sizeof(h->last_write)) {
        copy_len = sizeof(h->last_write) - 1u;
    }

    memcpy(h->last_write, data, copy_len);
    h->last_write[copy_len] = '\0';
    return copy_len;
}

size_t hal_uart_println(hal_uart_t h, const char *s) {
    if (!h) {
        return 0u;
    }

    const char *text = s ? s : "";
    size_t text_len = strlen(text);
    size_t total = text_len + 2u; /* text + CRLF */
    if (total >= sizeof(h->last_write)) {
        total = sizeof(h->last_write) - 1u;
    }

    size_t copy_text = (text_len < total) ? text_len : total;
    memcpy(h->last_write, text, copy_text);
    if (copy_text + 1u < sizeof(h->last_write)) {
        h->last_write[copy_text] = '\r';
    }
    if (copy_text + 2u < sizeof(h->last_write)) {
        h->last_write[copy_text + 1u] = '\n';
    }
    h->last_write[total] = '\0';
    return total;
}

void hal_uart_flush(hal_uart_t h) {
    (void)h;
}

void hal_uart_destroy(hal_uart_t h) {
    if (h) {
        h->in_use = 0;
    }
}

#endif /* HAL_DISABLE_UART */
