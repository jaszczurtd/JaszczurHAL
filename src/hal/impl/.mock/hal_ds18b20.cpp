#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#ifdef HAL_ENABLE_DS18B20

#include "../../hal_ds18b20.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include "hal_mock.h"

#include <math.h>
#include <string.h>

enum ds18b20_state_t {
    DS18B20_STATE_IDLE = 0,
    DS18B20_STATE_CONVERTING,
};

struct hal_ds18b20_impl_s {
    bool            in_use;
    uint8_t         pin;
    bool            use_rom;
    uint8_t         rom[8];
    uint32_t        conversion_time_us;
    uint64_t        conversion_deadline_us;
    ds18b20_state_t state;
    bool            sample_valid;
    bool            sample_fresh;
    float           last_temp_c;
    hal_mutex_t     mutex;

    /* Mock controls */
    bool            mock_presence;
    bool            mock_crc_ok;
    float           mock_next_temp_c;
    uint32_t        mock_request_count;
};

static hal_ds18b20_impl_t s_pool[HAL_DS18B20_MAX_INSTANCES];

static hal_ds18b20_resolution_t normalize_resolution(hal_ds18b20_resolution_t r) {
    switch (r) {
        case HAL_DS18B20_RES_9_BIT:
        case HAL_DS18B20_RES_10_BIT:
        case HAL_DS18B20_RES_11_BIT:
        case HAL_DS18B20_RES_12_BIT:
            return r;
        default:
            return HAL_DS18B20_RES_12_BIT;
    }
}

static uint32_t conversion_time_us_from_resolution(hal_ds18b20_resolution_t r) {
    switch (normalize_resolution(r)) {
        case HAL_DS18B20_RES_9_BIT:  return 93750u;
        case HAL_DS18B20_RES_10_BIT: return 187500u;
        case HAL_DS18B20_RES_11_BIT: return 375000u;
        default:                     return 750000u;
    }
}

hal_ds18b20_t hal_ds18b20_init(const hal_ds18b20_config_t *cfg) {
    if (!cfg) return NULL;

    hal_critical_section_enter();
    int slot = -1;
    for (int i = 0; i < HAL_DS18B20_MAX_INSTANCES; ++i) {
        if (!s_pool[i].in_use) {
            slot = i;
            s_pool[i].in_use = true;
            break;
        }
    }
    hal_critical_section_exit();
    if (slot < 0) return NULL;

    hal_ds18b20_impl_t *h = &s_pool[slot];
    memset(h, 0, sizeof(*h));
    h->in_use = true;
    h->pin = cfg->data_pin;
    h->use_rom = cfg->use_rom;
    memcpy(h->rom, cfg->rom_code, sizeof(h->rom));
    h->conversion_time_us = conversion_time_us_from_resolution(cfg->resolution_hint);
    h->state = DS18B20_STATE_IDLE;
    h->sample_valid = false;
    h->sample_fresh = false;
    h->last_temp_c = NAN;
    h->mutex = hal_mutex_create();

    h->mock_presence = true;
    h->mock_crc_ok = true;
    h->mock_next_temp_c = 25.0f;
    h->mock_request_count = 0;
    return h;
}

void hal_ds18b20_deinit(hal_ds18b20_t h) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->in_use = false;
    hal_mutex_t m = h->mutex;
    h->mutex = NULL;
    hal_mutex_unlock(m);
    hal_mutex_destroy(m);
}

bool hal_ds18b20_request(hal_ds18b20_t h) {
    if (!h) return false;
    hal_mutex_lock(h->mutex);
    if (h->state == DS18B20_STATE_CONVERTING || !h->mock_presence) {
        hal_mutex_unlock(h->mutex);
        return false;
    }
    h->conversion_deadline_us = hal_micros64() + h->conversion_time_us;
    h->state = DS18B20_STATE_CONVERTING;
    h->mock_request_count++;
    hal_mutex_unlock(h->mutex);
    return true;
}

void hal_ds18b20_poll(hal_ds18b20_t h) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->state != DS18B20_STATE_CONVERTING) {
        hal_mutex_unlock(h->mutex);
        return;
    }
    if (hal_micros64() < h->conversion_deadline_us) {
        hal_mutex_unlock(h->mutex);
        return;
    }

    if (h->mock_presence && h->mock_crc_ok) {
        h->last_temp_c = h->mock_next_temp_c;
        h->sample_valid = true;
        h->sample_fresh = true;
    }
    h->state = DS18B20_STATE_IDLE;
    hal_mutex_unlock(h->mutex);
}

bool hal_ds18b20_is_busy(hal_ds18b20_t h) {
    if (!h) return false;
    hal_mutex_lock(h->mutex);
    const bool busy = (h->state == DS18B20_STATE_CONVERTING);
    hal_mutex_unlock(h->mutex);
    return busy;
}

bool hal_ds18b20_take_latest(hal_ds18b20_t h, float *temp_c, bool *fresh) {
    if (!h || !temp_c) return false;
    hal_mutex_lock(h->mutex);
    if (!h->sample_valid) {
        hal_mutex_unlock(h->mutex);
        return false;
    }
    *temp_c = h->last_temp_c;
    if (fresh) {
        *fresh = h->sample_fresh;
    }
    h->sample_fresh = false;
    hal_mutex_unlock(h->mutex);
    return true;
}

/* ── Mock helpers ───────────────────────────────────────────────────────── */

void hal_mock_ds18b20_set_next_temp(hal_ds18b20_t h, float temp_c) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->mock_next_temp_c = temp_c;
    hal_mutex_unlock(h->mutex);
}

void hal_mock_ds18b20_set_presence(hal_ds18b20_t h, bool present) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->mock_presence = present;
    hal_mutex_unlock(h->mutex);
}

void hal_mock_ds18b20_set_crc_ok(hal_ds18b20_t h, bool ok) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->mock_crc_ok = ok;
    hal_mutex_unlock(h->mutex);
}

uint32_t hal_mock_ds18b20_get_request_count(hal_ds18b20_t h) {
    if (!h) return 0u;
    hal_mutex_lock(h->mutex);
    uint32_t v = h->mock_request_count;
    hal_mutex_unlock(h->mutex);
    return v;
}

#endif /* HAL_ENABLE_DS18B20 */
#endif  // HAL_TARGET_IS_MOCK
