#include "../../hal_can.h"
#include "../../hal_config.h"
#include "../../hal_sync.h"
#include "hal_mock.h"
#include <string.h>

struct CanFrame {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[HAL_CAN_MAX_DATA_LEN];
};

struct hal_can_impl_s {
    CanFrame    rx[MOCK_CAN_BUF_SIZE];
    int         rx_head, rx_tail, rx_count;
    CanFrame    tx[MOCK_CAN_BUF_SIZE];
    int         tx_head, tx_tail, tx_count;
    int         in_use;
    hal_mutex_t mutex;
};

static hal_can_impl_t s_pool[MOCK_CAN_MAX_INST];

static int ring_push(CanFrame *buf, int *tail, int *count, const CanFrame *f) {
    const int cap = hal_get_config()->mock_can_buf_size;
    if (*count >= cap) return -1;
    buf[*tail] = *f;
    *tail = (*tail + 1) % cap;
    (*count)++;
    return 0;
}

static int ring_pop(CanFrame *buf, int *head, int *count, CanFrame *out) {
    const int cap = hal_get_config()->mock_can_buf_size;
    if (*count <= 0) return -1;
    *out = buf[*head];
    *head = (*head + 1) % cap;
    (*count)--;
    return 0;
}

hal_can_t hal_can_create(uint8_t cs_pin) {
    (void)cs_pin;
    hal_critical_section_enter();
    int slot = -1;
    for (int i = 0; i < hal_get_config()->mock_can_max_inst; i++) {
        if (!s_pool[i].in_use) { slot = i; s_pool[slot].in_use = 1; break; }
    }
    hal_critical_section_exit();

    if (slot < 0) {
        HAL_ASSERT(0, "hal_can: pool exhausted - increase MOCK_CAN_MAX_INST");
        return NULL;
    }
    hal_can_impl_t *h = &s_pool[slot];
    memset(h, 0, sizeof(*h));
    h->in_use = 1;
    h->mutex  = hal_mutex_create();
    return h;
}

void hal_can_destroy(hal_can_t h) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->in_use = 0;
    hal_mutex_unlock(h->mutex);
}

bool hal_can_send(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data) {
    if (!h) return false;
    if (len > 0 && data == NULL) return false;
    const uint8_t safe_len = len < HAL_CAN_MAX_DATA_LEN ? len : HAL_CAN_MAX_DATA_LEN;
    CanFrame f;
    f.id  = id;
    f.len = safe_len;
    if (safe_len > 0) {
        memcpy(f.data, data, safe_len);
    }
    hal_mutex_lock(h->mutex);
    bool ok = ring_push(h->tx, &h->tx_tail, &h->tx_count, &f) == 0;
    hal_mutex_unlock(h->mutex);
    return ok;
}

bool hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data) {
    if (!h || !id || !len || !data) return false;
    hal_mutex_lock(h->mutex);
    CanFrame f;
    bool ok = ring_pop(h->rx, &h->rx_head, &h->rx_count, &f) == 0;
    hal_mutex_unlock(h->mutex);
    if (!ok) return false;
    *id  = f.id;
    *len = f.len;
    memcpy(data, f.data, f.len < HAL_CAN_MAX_DATA_LEN ? f.len : HAL_CAN_MAX_DATA_LEN);
    return true;
}

bool hal_can_available(hal_can_t h) {
    if (!h) return false;
    hal_mutex_lock(h->mutex);
    bool v = h->rx_count > 0;
    hal_mutex_unlock(h->mutex);
    return v;
}

bool hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1) {
    (void)h; (void)id0; (void)id1;
    return h != NULL;
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

void hal_mock_can_inject(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data) {
    if (!h || (len > 0 && data == NULL)) return;
    const uint8_t safe_len = len < HAL_CAN_MAX_DATA_LEN ? len : HAL_CAN_MAX_DATA_LEN;
    CanFrame f;
    f.id  = id;
    f.len = safe_len;
    if (safe_len > 0) {
        memcpy(f.data, data, safe_len);
    }
    hal_mutex_lock(h->mutex);
    int ok = ring_push(h->rx, &h->rx_tail, &h->rx_count, &f);
    hal_mutex_unlock(h->mutex);
    (void)ok;  // Assertion above checks the value; output not used otherwise
    HAL_ASSERT(ok == 0, "hal_mock_can_inject: RX ring full - increase MOCK_CAN_BUF_SIZE");
}

bool hal_mock_can_get_sent(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data) {
    if (!h || !id || !len || !data) return false;
    hal_mutex_lock(h->mutex);
    CanFrame f;
    bool ok = ring_pop(h->tx, &h->tx_head, &h->tx_count, &f) == 0;
    hal_mutex_unlock(h->mutex);
    if (!ok) return false;
    *id  = f.id;
    *len = f.len;
    memcpy(data, f.data, f.len < HAL_CAN_MAX_DATA_LEN ? f.len : HAL_CAN_MAX_DATA_LEN);
    return true;
}

void hal_mock_can_reset(hal_can_t h) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->rx_head = h->rx_tail = h->rx_count = 0;
    h->tx_head = h->tx_tail = h->tx_count = 0;
    hal_mutex_unlock(h->mutex);
}
