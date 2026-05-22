#include "../../hal_config.h"
#ifndef HAL_DISABLE_DS18B20

#include "../../hal_ds18b20.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"

#include "drivers/DallasTemperature/DallasTemperature.h"
#include "drivers/OneWire/OneWire.h"

#include <Arduino.h>
#include <math.h>
#include <new>
#include <string.h>

enum ds18b20_state_t {
    DS18B20_STATE_IDLE = 0,
    DS18B20_STATE_CONVERTING,
};

struct hal_ds18b20_impl_s {
    bool                       in_use;
    uint8_t                    pin;
    bool                       use_rom;
    uint8_t                    rom[8];
    uint8_t                    address[8];
    hal_ds18b20_resolution_t   resolution;
    uint32_t                   conversion_time_us;
    uint64_t                   conversion_deadline_us;
    ds18b20_state_t            state;
    bool                       sample_valid;
    bool                       sample_fresh;
    float                      last_temp_c;
    hal_mutex_t                mutex;

    alignas(OneWire)           uint8_t onewire_mem[sizeof(OneWire)];
    alignas(DallasTemperature) uint8_t dallas_mem[sizeof(DallasTemperature)];
};

static hal_ds18b20_impl_t s_pool[HAL_DS18B20_MAX_INSTANCES];

static void release_pool_slot(hal_ds18b20_impl_t *h) {
    hal_critical_section_enter();
    h->in_use = false;
    hal_critical_section_exit();
}

static inline OneWire *as_onewire(hal_ds18b20_impl_t *h) {
    return reinterpret_cast<OneWire *>(h->onewire_mem);
}

static inline DallasTemperature *as_dallas(hal_ds18b20_impl_t *h) {
    return reinterpret_cast<DallasTemperature *>(h->dallas_mem);
}

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

static uint8_t resolution_to_u8(hal_ds18b20_resolution_t r) {
    return (uint8_t)normalize_resolution(r);
}

static uint32_t conversion_time_us_from_resolution(hal_ds18b20_resolution_t r) {
    switch (normalize_resolution(r)) {
        case HAL_DS18B20_RES_9_BIT:  return 93750u;
        case HAL_DS18B20_RES_10_BIT: return 187500u;
        case HAL_DS18B20_RES_11_BIT: return 375000u;
        default:                     return 750000u;
    }
}

static void refresh_conversion_timing(hal_ds18b20_impl_t *h) {
    const uint8_t sensor_resolution = as_dallas(h)->getResolution(h->address);
    if (sensor_resolution >= 9u && sensor_resolution <= 12u) {
        h->resolution = (hal_ds18b20_resolution_t)sensor_resolution;
    }
    h->conversion_time_us = conversion_time_us_from_resolution(h->resolution);
}

static bool resolve_sensor_address(hal_ds18b20_impl_t *h) {
    DallasTemperature *dt = as_dallas(h);

    if (h->use_rom) {
        if (!dt->validAddress(h->rom) || !dt->validFamily(h->rom) || !dt->isConnected(h->rom)) {
            return false;
        }
        memcpy(h->address, h->rom, sizeof(h->address));
        return true;
    }

    if (dt->getDS18Count() == 0u) {
        return false;
    }

    DeviceAddress discovered = {0};
    if (!dt->getAddress(discovered, 0u) || !dt->validFamily(discovered) || !dt->isConnected(discovered)) {
        return false;
    }

    memcpy(h->address, discovered, sizeof(h->address));

    if (dt->getDS18Count() > 1u) {
        hal_derr("hal_ds18b20_init: multiple sensors on pin %u; using first discovered address", (unsigned)h->pin);
    }

    return true;
}

hal_ds18b20_t hal_ds18b20_init(const hal_ds18b20_config_t *cfg) {
    if (!cfg) {
        return NULL;
    }

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

    HAL_ASSERT(slot >= 0, "hal_ds18b20: pool exhausted - increase HAL_DS18B20_MAX_INSTANCES");
    if (slot < 0) {
        return NULL;
    }

    hal_ds18b20_impl_t *h = &s_pool[slot];
    h->pin = cfg->data_pin;
    h->use_rom = cfg->use_rom;
    memset(h->rom, 0, sizeof(h->rom));
    memset(h->address, 0, sizeof(h->address));
    h->conversion_deadline_us = 0u;
    h->state = DS18B20_STATE_IDLE;
    h->sample_valid = false;
    h->sample_fresh = false;
    h->last_temp_c = NAN;
    h->mutex = NULL;

    memcpy(h->rom, cfg->rom_code, sizeof(h->rom));
    h->resolution = normalize_resolution(cfg->resolution_hint);
    h->conversion_time_us = conversion_time_us_from_resolution(h->resolution);

    h->mutex = hal_mutex_create();
    if (!h->mutex) {
        release_pool_slot(h);
        return NULL;
    }

    bool onewire_ready = false;
    bool dallas_ready = false;

    new (h->onewire_mem) OneWire(h->pin);
    onewire_ready = true;

    new (h->dallas_mem) DallasTemperature(as_onewire(h));
    dallas_ready = true;

    DallasTemperature *dt = as_dallas(h);
    dt->setWaitForConversion(false);
    dt->setCheckForConversion(false);
    dt->begin();

    if (!resolve_sensor_address(h)) {
        hal_derr("hal_ds18b20_init: sensor not found on pin %u", (unsigned)h->pin);
        if (dallas_ready) {
            as_dallas(h)->~DallasTemperature();
        }
        if (onewire_ready) {
            as_onewire(h)->~OneWire();
        }
        hal_mutex_destroy(h->mutex);
        h->mutex = NULL;
        release_pool_slot(h);
        return NULL;
    }

    const uint8_t requested_resolution = resolution_to_u8(h->resolution);
    if (!dt->setResolution(h->address, requested_resolution, false)) {
        hal_derr("hal_ds18b20_init: setResolution(%u-bit) failed on pin %u; using fallback timing",
                 (unsigned)requested_resolution,
                 (unsigned)h->pin);
    }

    refresh_conversion_timing(h);
    return h;
}

void hal_ds18b20_deinit(hal_ds18b20_t h) {
    if (!h) {
        return;
    }

    hal_mutex_lock(h->mutex);
    as_dallas(h)->~DallasTemperature();
    as_onewire(h)->~OneWire();

    hal_mutex_t m = h->mutex;
    h->mutex = NULL;
    hal_mutex_unlock(m);
    hal_mutex_destroy(m);
    release_pool_slot(h);
}

bool hal_ds18b20_request(hal_ds18b20_t h) {
    if (!h) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    if (h->state == DS18B20_STATE_CONVERTING) {
        hal_mutex_unlock(h->mutex);
        return false;
    }

    DallasTemperature *dt = as_dallas(h);
    if (!dt->isConnected(h->address)) {
        hal_mutex_unlock(h->mutex);
        return false;
    }

    const DallasTemperature::request_t req = dt->requestTemperaturesByAddress(h->address);
    if (!req.result) {
        hal_mutex_unlock(h->mutex);
        return false;
    }

    h->conversion_deadline_us = hal_micros64() + h->conversion_time_us;
    h->state = DS18B20_STATE_CONVERTING;
    hal_mutex_unlock(h->mutex);
    return true;
}

void hal_ds18b20_poll(hal_ds18b20_t h) {
    if (!h) {
        return;
    }

    hal_mutex_lock(h->mutex);
    if (h->state != DS18B20_STATE_CONVERTING) {
        hal_mutex_unlock(h->mutex);
        return;
    }

    if (hal_micros64() < h->conversion_deadline_us) {
        hal_mutex_unlock(h->mutex);
        return;
    }

    const float temp_c = as_dallas(h)->getTempC(h->address);
    if (!isnan(temp_c) && temp_c >= -55.0f && temp_c <= 125.0f) {
        h->last_temp_c = temp_c;
        h->sample_valid = true;
        h->sample_fresh = true;
        refresh_conversion_timing(h);
    }

    h->state = DS18B20_STATE_IDLE;
    hal_mutex_unlock(h->mutex);
}

bool hal_ds18b20_is_busy(hal_ds18b20_t h) {
    if (!h) {
        return false;
    }

    hal_mutex_lock(h->mutex);
    const bool busy = (h->state == DS18B20_STATE_CONVERTING);
    hal_mutex_unlock(h->mutex);
    return busy;
}

bool hal_ds18b20_take_latest(hal_ds18b20_t h, float *temp_c, bool *fresh) {
    if (!h || !temp_c) {
        return false;
    }

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

#endif /* HAL_DISABLE_DS18B20 */
