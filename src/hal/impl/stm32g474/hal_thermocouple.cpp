#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_config.h"
#ifdef HAL_ENABLE_THERMOCOUPLE

#include "../../hal_thermocouple.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#ifdef HAL_ENABLE_MAX6675
#include "../shared/max6675_driver.h"
#endif

#include <math.h>
#include <new>
#include <stdio.h>

struct hal_thermocouple_impl_s {
    hal_thermocouple_chip_t chip;
    bool                    in_use;
    hal_mutex_t             mutex;
    union {
        uint8_t dummy;
#ifdef HAL_ENABLE_MAX6675
        alignas(hal_max6675_t) uint8_t max_mem[sizeof(hal_max6675_t)];
#endif
    } storage;
};

static hal_thermocouple_impl_t s_pool[HAL_THERMOCOUPLE_MAX_INSTANCES];

#ifdef HAL_ENABLE_MAX6675
static inline hal_max6675_t *as_max(hal_thermocouple_impl_t *h) {
    return reinterpret_cast<hal_max6675_t *>(h->storage.max_mem);
}
#endif

#ifdef HAL_ENABLE_MCP9600
static const char *chip_name(hal_thermocouple_chip_t chip) {
    switch (chip) {
        case HAL_THERMOCOUPLE_CHIP_MCP9600: return "MCP9600";
        case HAL_THERMOCOUPLE_CHIP_MAX6675: return "MAX6675";
        default:                            return "UNKNOWN";
    }
}

static void not_supported(const char *fn, hal_thermocouple_chip_t chip) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s: %s is not supporting this functionality",
             fn, chip_name(chip));
    hal_serial_println(buf);
}
#endif

static void release_slot(hal_thermocouple_impl_t *h) {
    if (h == nullptr) {
        return;
    }
    if (h->mutex != nullptr) {
        hal_mutex_destroy(h->mutex);
        h->mutex = nullptr;
    }
    h->in_use = false;
}

hal_thermocouple_t hal_thermocouple_init(const hal_thermocouple_config_t *cfg) {
    if (cfg == nullptr) {
        return nullptr;
    }

    hal_critical_section_enter();
    int slot = -1;
    for (int i = 0; i < HAL_THERMOCOUPLE_MAX_INSTANCES; ++i) {
        if (!s_pool[i].in_use) {
            slot = i;
            s_pool[i].in_use = true;
            break;
        }
    }
    hal_critical_section_exit();

    HAL_ASSERT(slot >= 0, "hal_thermocouple: pool exhausted - increase HAL_THERMOCOUPLE_MAX_INSTANCES");
    if (slot < 0) {
        return nullptr;
    }

    hal_thermocouple_impl_t *h = &s_pool[slot];
    h->chip = cfg->chip;
    h->mutex = hal_mutex_create();

#ifdef HAL_ENABLE_MAX6675
    if (cfg->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) {
        const hal_thermocouple_spi_cfg_t &sc = cfg->bus.spi;
        hal_max6675_t *max = new(h->storage.max_mem) hal_max6675_t();
        const hal_max6675_config_t max_cfg = {sc.sclk_pin, sc.cs_pin, sc.miso_pin};
        if (!hal_max6675_init(max, &max_cfg)) {
            release_slot(h);
            hal_serial_println("hal_thermocouple_init: MAX6675 init failed");
            return nullptr;
        }
        return h;
    }
#endif

#ifdef HAL_ENABLE_MCP9600
    if (cfg->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        release_slot(h);
        hal_serial_println("hal_thermocouple_init: MCP9600 is not supported on STM32G474 yet");
        return nullptr;
    }
#endif

    release_slot(h);
    hal_serial_println("hal_thermocouple_init: unknown chip type");
    return nullptr;
}

void hal_thermocouple_deinit(hal_thermocouple_t h) {
    if (h == nullptr) {
        return;
    }
    hal_mutex_lock(h->mutex);
#ifdef HAL_ENABLE_MAX6675
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) {
        hal_max6675_deinit(as_max(h));
    }
#endif
    h->in_use = false;
    hal_mutex_t m = h->mutex;
    h->mutex = nullptr;
    hal_mutex_unlock(m);
    hal_mutex_destroy(m);
}

float hal_thermocouple_read(hal_thermocouple_t h) {
    if (h == nullptr) {
        return NAN;
    }
    hal_mutex_lock(h->mutex);
    float v = NAN;
#ifdef HAL_ENABLE_MAX6675
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) {
        v = hal_max6675_read_celsius(as_max(h));
    }
#endif
    hal_mutex_unlock(h->mutex);
    return v;
}

#ifdef HAL_ENABLE_MCP9600
float hal_thermocouple_read_ambient(hal_thermocouple_t h) {
    if (h == nullptr) {
        return NAN;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_read_ambient", h->chip);
    hal_mutex_unlock(h->mutex);
    return NAN;
}

int32_t hal_thermocouple_read_adc_raw(hal_thermocouple_t h) {
    if (h == nullptr) {
        return 0;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_read_adc_raw", h->chip);
    hal_mutex_unlock(h->mutex);
    return 0;
}

void hal_thermocouple_set_type(hal_thermocouple_t h, hal_thermocouple_type_t type) {
    (void)type;
    if (h == nullptr) {
        return;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_set_type", h->chip);
    hal_mutex_unlock(h->mutex);
}
#endif /* HAL_ENABLE_MCP9600 */

hal_thermocouple_type_t hal_thermocouple_get_type(hal_thermocouple_t h) {
    (void)h;
    return HAL_THERMOCOUPLE_TYPE_K;
}

#ifdef HAL_ENABLE_MCP9600
void hal_thermocouple_set_filter(hal_thermocouple_t h, uint8_t coeff) {
    (void)coeff;
    if (h == nullptr) {
        return;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_set_filter", h->chip);
    hal_mutex_unlock(h->mutex);
}

uint8_t hal_thermocouple_get_filter(hal_thermocouple_t h) {
    if (h == nullptr) {
        return 0;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_get_filter", h->chip);
    hal_mutex_unlock(h->mutex);
    return 0;
}

void hal_thermocouple_set_adc_resolution(hal_thermocouple_t h,
                                          hal_thermocouple_adc_res_t res) {
    (void)res;
    if (h == nullptr) {
        return;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_set_adc_resolution", h->chip);
    hal_mutex_unlock(h->mutex);
}

hal_thermocouple_adc_res_t hal_thermocouple_get_adc_resolution(hal_thermocouple_t h) {
    if (h == nullptr) {
        return HAL_THERMOCOUPLE_ADC_RES_12;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_get_adc_resolution", h->chip);
    hal_mutex_unlock(h->mutex);
    return HAL_THERMOCOUPLE_ADC_RES_12;
}

void hal_thermocouple_set_ambient_resolution(hal_thermocouple_t h,
                                              hal_thermocouple_ambient_res_t res) {
    (void)res;
    if (h == nullptr) {
        return;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_set_ambient_resolution", h->chip);
    hal_mutex_unlock(h->mutex);
}

void hal_thermocouple_enable(hal_thermocouple_t h, bool enable) {
    (void)enable;
    if (h == nullptr) {
        return;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_enable", h->chip);
    hal_mutex_unlock(h->mutex);
}
#endif /* HAL_ENABLE_MCP9600 */

bool hal_thermocouple_is_enabled(hal_thermocouple_t h) {
    if (h == nullptr) {
        return false;
    }
    hal_mutex_lock(h->mutex);
    const bool v = h->chip == HAL_THERMOCOUPLE_CHIP_MAX6675;
    hal_mutex_unlock(h->mutex);
    return v;
}

#ifdef HAL_ENABLE_MCP9600
void hal_thermocouple_set_alert(hal_thermocouple_t h, uint8_t alert_num,
                                 bool enabled,
                                 const hal_thermocouple_alert_cfg_t *cfg) {
    (void)alert_num;
    (void)enabled;
    (void)cfg;
    if (h == nullptr) {
        return;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_set_alert", h->chip);
    hal_mutex_unlock(h->mutex);
}

float hal_thermocouple_get_alert_temp(hal_thermocouple_t h, uint8_t alert_num) {
    (void)alert_num;
    if (h == nullptr) {
        return NAN;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_get_alert_temp", h->chip);
    hal_mutex_unlock(h->mutex);
    return NAN;
}

uint8_t hal_thermocouple_get_status(hal_thermocouple_t h) {
    if (h == nullptr) {
        return 0;
    }
    hal_mutex_lock(h->mutex);
    not_supported("hal_thermocouple_get_status", h->chip);
    hal_mutex_unlock(h->mutex);
    return 0;
}
#endif /* HAL_ENABLE_MCP9600 */

#endif /* HAL_ENABLE_THERMOCOUPLE */
#endif /* HAL_TARGET_IS_STM32G474 */
