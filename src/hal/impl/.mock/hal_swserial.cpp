#include "../../hal_swserial.h"
#include "../../hal_config.h"
#include "hal_mock.h"
#include <string.h>

#define HAL_SWSERIAL_BUF_SIZE 512

struct hal_swserial_impl_s {
    uint8_t buf[HAL_SWSERIAL_BUF_SIZE];
    char    last_write[HAL_SWSERIAL_BUF_SIZE];
    int     head;
    int     tail;
    uint8_t rx_pin;
    uint8_t tx_pin;
    bool    started;
    int     in_use;
};

static hal_swserial_impl_t s_pool[HAL_SWSERIAL_MAX_INSTANCES];

hal_swserial_t hal_swserial_create(uint8_t rx_pin, uint8_t tx_pin) {
    for (int i = 0; i < hal_get_config()->swserial_max_instances; i++) {
        if (!s_pool[i].in_use) {
            memset(&s_pool[i], 0, sizeof(s_pool[i]));
            s_pool[i].rx_pin = rx_pin;
            s_pool[i].tx_pin = tx_pin;
            s_pool[i].in_use = 1;
            return &s_pool[i];
        }
    }
    HAL_ASSERT(0, "hal_swserial: pool exhausted - increase HAL_SWSERIAL_MAX_INSTANCES");
    return NULL;
}

bool hal_swserial_set_rx(hal_swserial_t h, uint8_t rx_pin) {
    if (!h) return false;
    if (h->rx_pin == rx_pin) return true;
    if (h->started) return false;
    h->rx_pin = rx_pin;
    return true;
}

bool hal_swserial_set_tx(hal_swserial_t h, uint8_t tx_pin) {
    if (!h) return false;
    if (h->tx_pin == tx_pin) return true;
    if (h->started) return false;
    h->tx_pin = tx_pin;
    return true;
}

void hal_swserial_begin(hal_swserial_t h, uint32_t baud, uint16_t config) {
    (void)baud; (void)config;
    if (h) h->started = true;
}

int hal_swserial_available(hal_swserial_t h) {
    return (h->tail - h->head + HAL_SWSERIAL_BUF_SIZE) % HAL_SWSERIAL_BUF_SIZE;
}

int hal_swserial_read(hal_swserial_t h) {
    if (hal_swserial_available(h) == 0) return -1;
    int val = h->buf[h->head];
    h->head = (h->head + 1) % HAL_SWSERIAL_BUF_SIZE;
    return val;
}

size_t hal_swserial_write(hal_swserial_t h, const uint8_t *data, size_t len) {
    if (!h || !data || len == 0u) return 0u;
    size_t copy_len = len;
    if (copy_len >= sizeof(h->last_write)) {
        copy_len = sizeof(h->last_write) - 1u;
    }

    memcpy(h->last_write, data, copy_len);
    h->last_write[copy_len] = '\0';
    return len;
}

size_t hal_swserial_println(hal_swserial_t h, const char *s) {
    const char *text = s ? s : "";
    return hal_swserial_write(h, (const uint8_t *)text, strlen(text));
}

void hal_swserial_flush(hal_swserial_t h) {
    (void)h;
}

void hal_swserial_destroy(hal_swserial_t h) {
    if (h) h->in_use = 0;
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

void hal_mock_swserial_push(hal_swserial_t h, const uint8_t *data, int len) {
    for (int i = 0; i < len; i++) {
        int next = (h->tail + 1) % HAL_SWSERIAL_BUF_SIZE;
        if (next != h->head) {
            h->buf[h->tail] = data[i];
            h->tail = next;
        }
    }
}

void hal_mock_swserial_reset(hal_swserial_t h) {
    if (!h) return;
    h->head = 0;
    h->tail = 0;
    h->last_write[0] = '\0';
}

const char *hal_mock_swserial_last_write(hal_swserial_t h) {
    return h ? h->last_write : "";
}
