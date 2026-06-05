#include "../../../hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474)

#include "../../../hal_config.h"
#ifdef HAL_ENABLE_ONEWIRE

#include "../../../hal_onewire.h"
#include "../../../hal_sync.h"

#include "onewire_driver.h"

#include <new>
#include <string.h>

struct hal_onewire_impl_s {
    bool      in_use;
    uint8_t   pin;
    hal_mutex_t mutex;
    alignas(JHOneWire) uint8_t onewire_mem[sizeof(JHOneWire)];
};

static hal_onewire_impl_t s_pool[HAL_ONEWIRE_MAX_INSTANCES];
static hal_mutex_t s_onewire_bus_mutex = NULL;

static inline JHOneWire *as_onewire(hal_onewire_impl_t *h) {
    return reinterpret_cast<JHOneWire *>(h->onewire_mem);
}

static void release_pool_slot(hal_onewire_impl_t *h) {
    hal_critical_section_enter();
    h->in_use = false;
    hal_critical_section_exit();
}

static void onewire_ensure_bus_mutex(void) {
    if (!s_onewire_bus_mutex) {
        hal_critical_section_enter();
        if (!s_onewire_bus_mutex) {
            s_onewire_bus_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

static void onewire_enter_op(hal_onewire_t h) {
    hal_mutex_lock(h->mutex);
    onewire_ensure_bus_mutex();
    if (s_onewire_bus_mutex) {
        hal_mutex_lock(s_onewire_bus_mutex);
    }
}

static void onewire_exit_op(hal_onewire_t h) {
    if (s_onewire_bus_mutex) {
        hal_mutex_unlock(s_onewire_bus_mutex);
    }
    hal_mutex_unlock(h->mutex);
}

hal_onewire_t hal_onewire_init(uint8_t data_pin) {
    hal_critical_section_enter();
    int slot = -1;
    for (int i = 0; i < HAL_ONEWIRE_MAX_INSTANCES; ++i) {
        if (!s_pool[i].in_use) {
            slot = i;
            s_pool[i].in_use = true;
            break;
        }
    }
    hal_critical_section_exit();

    HAL_ASSERT(slot >= 0, "hal_onewire: pool exhausted - increase HAL_ONEWIRE_MAX_INSTANCES");
    if (slot < 0) {
        return NULL;
    }

    hal_onewire_impl_t *h = &s_pool[slot];
    h->pin = data_pin;
    h->mutex = hal_mutex_create();
    if (!h->mutex) {
        release_pool_slot(h);
        return NULL;
    }

    memset(h->onewire_mem, 0, sizeof(h->onewire_mem));
    new (h->onewire_mem) JHOneWire(data_pin);
    return h;
}

void hal_onewire_deinit(hal_onewire_t h) {
    if (!h) {
        return;
    }

    onewire_enter_op(h);
    as_onewire(h)->~JHOneWire();
    onewire_exit_op(h);

    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
    release_pool_slot(h);
}

bool hal_onewire_reset(hal_onewire_t h) {
    if (!h) {
        return false;
    }

    onewire_enter_op(h);
    const bool present = (as_onewire(h)->reset() != 0u);
    onewire_exit_op(h);
    return present;
}

void hal_onewire_select(hal_onewire_t h, const uint8_t rom[8]) {
    if (!h || !rom) {
        return;
    }

    onewire_enter_op(h);
    as_onewire(h)->select(rom);
    onewire_exit_op(h);
}

void hal_onewire_skip(hal_onewire_t h) {
    if (!h) {
        return;
    }

    onewire_enter_op(h);
    as_onewire(h)->skip();
    onewire_exit_op(h);
}

void hal_onewire_write(hal_onewire_t h, uint8_t value, bool power) {
    if (!h) {
        return;
    }

    onewire_enter_op(h);
    as_onewire(h)->write(value, power ? 1u : 0u);
    onewire_exit_op(h);
}

size_t hal_onewire_write_bytes(hal_onewire_t h, const uint8_t *data, uint16_t len, bool power) {
    if (!h || !data || len == 0u) {
        return 0u;
    }

    onewire_enter_op(h);
    as_onewire(h)->write_bytes(data, len, power);
    onewire_exit_op(h);
    return (size_t)len;
}

uint8_t hal_onewire_read(hal_onewire_t h) {
    if (!h) {
        return 0u;
    }

    onewire_enter_op(h);
    const uint8_t value = as_onewire(h)->read();
    onewire_exit_op(h);
    return value;
}

size_t hal_onewire_read_bytes(hal_onewire_t h, uint8_t *out, uint16_t len) {
    if (!h || !out || len == 0u) {
        return 0u;
    }

    onewire_enter_op(h);
    as_onewire(h)->read_bytes(out, len);
    onewire_exit_op(h);
    return (size_t)len;
}

void hal_onewire_write_bit(hal_onewire_t h, uint8_t bit) {
    if (!h) {
        return;
    }

    onewire_enter_op(h);
    as_onewire(h)->write_bit((uint8_t)(bit ? 1u : 0u));
    onewire_exit_op(h);
}

uint8_t hal_onewire_read_bit(hal_onewire_t h) {
    if (!h) {
        return 0u;
    }

    onewire_enter_op(h);
    const uint8_t value = as_onewire(h)->read_bit();
    onewire_exit_op(h);
    return value;
}

void hal_onewire_depower(hal_onewire_t h) {
    if (!h) {
        return;
    }

    onewire_enter_op(h);
    as_onewire(h)->depower();
    onewire_exit_op(h);
}

void hal_onewire_reset_search(hal_onewire_t h) {
    if (!h) {
        return;
    }

    onewire_enter_op(h);
    as_onewire(h)->reset_search();
    onewire_exit_op(h);
}

void hal_onewire_target_search(hal_onewire_t h, uint8_t family_code) {
    if (!h) {
        return;
    }

    onewire_enter_op(h);
    as_onewire(h)->target_search(family_code);
    onewire_exit_op(h);
}

bool hal_onewire_search(hal_onewire_t h, uint8_t out_rom[8], bool search_mode) {
    if (!h || !out_rom) {
        return false;
    }

    onewire_enter_op(h);
    const bool found = as_onewire(h)->search(out_rom, search_mode);
    onewire_exit_op(h);
    return found;
}

uint8_t hal_onewire_crc8(const uint8_t *data, uint8_t len) {
    if (!data || len == 0u) {
        return 0u;
    }
    return JHOneWire::crc8(data, len);
}

bool hal_onewire_check_crc16(const uint8_t *data,
                             uint16_t len,
                             const uint8_t inverted_crc[2],
                             uint16_t crc) {
    if (!data || !inverted_crc) {
        return false;
    }
    return JHOneWire::check_crc16(data, len, inverted_crc, crc);
}

uint16_t hal_onewire_crc16(const uint8_t *data, uint16_t len, uint16_t crc) {
    if (!data && len != 0u) {
        return crc;
    }
    return JHOneWire::crc16(data, len, crc);
}

#endif /* HAL_ENABLE_ONEWIRE */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
