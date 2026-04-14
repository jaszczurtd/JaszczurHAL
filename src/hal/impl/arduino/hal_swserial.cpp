#include "../../hal_config.h"
#ifndef HAL_DISABLE_SWSERIAL

#include "../../hal_swserial.h"
#include <SoftwareSerial.h>
#include <new>

struct hal_swserial_impl_s {
    SoftwareSerial sw;
    uint8_t        rx_pin;
    uint8_t        tx_pin;
    bool           started;
    int            in_use;
    hal_swserial_impl_s(uint8_t rx, uint8_t tx) : sw(rx, tx), rx_pin(rx), tx_pin(tx), started(false), in_use(1) {}
};

static uint8_t s_mem[HAL_SWSERIAL_MAX_INSTANCES][sizeof(hal_swserial_impl_t)]
    __attribute__((aligned(__alignof__(hal_swserial_impl_t))));
static int s_used[HAL_SWSERIAL_MAX_INSTANCES];

hal_swserial_t hal_swserial_create(uint8_t rx_pin, uint8_t tx_pin) {
    for (int i = 0; i < hal_get_config()->swserial_max_instances; i++) {
        if (!s_used[i]) {
            s_used[i] = 1;
            return new(s_mem[i]) hal_swserial_impl_s(rx_pin, tx_pin);
        }
    }
    HAL_ASSERT(0, "hal_swserial: pool exhausted – increase HAL_SWSERIAL_MAX_INSTANCES");
    return NULL;
}

static bool hal_swserial_rebind(hal_swserial_t h, uint8_t rx_pin, uint8_t tx_pin) {
    if (!h || h->started) return false;
    h->~hal_swserial_impl_s();
    new(h) hal_swserial_impl_s(rx_pin, tx_pin);
    return true;
}

bool hal_swserial_set_rx(hal_swserial_t h, uint8_t rx_pin) {
    if (!h) return false;
    if (h->rx_pin == rx_pin) return true;
    return hal_swserial_rebind(h, rx_pin, h->tx_pin);
}

bool hal_swserial_set_tx(hal_swserial_t h, uint8_t tx_pin) {
    if (!h) return false;
    if (h->tx_pin == tx_pin) return true;
    return hal_swserial_rebind(h, h->rx_pin, tx_pin);
}

void hal_swserial_begin(hal_swserial_t h, uint32_t baud, uint16_t config) {
    if (!h) return;
    h->sw.begin(baud, config);
    h->started = true;
}

int hal_swserial_available(hal_swserial_t h) {
    if (!h) return 0;
    return h->sw.available();
}

int hal_swserial_read(hal_swserial_t h) {
    if (!h) return -1;
    return h->sw.read();
}

size_t hal_swserial_write(hal_swserial_t h, const uint8_t *data, size_t len) {
    if (!h || !data || len == 0u) return 0u;
    return h->sw.write(data, len);
}

size_t hal_swserial_println(hal_swserial_t h, const char *s) {
    if (!h) return 0u;
    return h->sw.println(s ? s : "");
}

void hal_swserial_flush(hal_swserial_t h) {
    if (!h) return;
    h->sw.flush();
}

void hal_swserial_destroy(hal_swserial_t h) {
    if (!h) return;
    for (int i = 0; i < hal_get_config()->swserial_max_instances; i++) {
        if ((uint8_t *)h == s_mem[i]) {
            h->~hal_swserial_impl_s();
            s_used[i] = 0;
            return;
        }
    }
}


#endif /* HAL_DISABLE_SWSERIAL */
