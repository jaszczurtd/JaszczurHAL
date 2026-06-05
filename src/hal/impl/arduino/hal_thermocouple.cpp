#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#ifdef HAL_ENABLE_THERMOCOUPLE

#include "../../hal_thermocouple.h"
#include "../../hal_i2c.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#ifdef HAL_ENABLE_MCP9600
#include "../shared/mcp9600/mcp9600_driver.h"
#endif
#ifdef HAL_ENABLE_MAX6675
#include "../shared/max6675/max6675_driver.h"
#endif
#include <new>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Internal instance record ───────────────────────────────────────────── */

struct hal_thermocouple_impl_s {
    hal_thermocouple_chip_t chip;
    bool                    in_use;
    hal_mutex_t             mutex;
    union {
#ifdef HAL_ENABLE_MCP9600
        alignas(hal_mcp9600_t)    uint8_t mcp_mem[sizeof(hal_mcp9600_t)];
#endif
#ifdef HAL_ENABLE_MAX6675
        alignas(hal_max6675_t)     uint8_t max_mem[sizeof(hal_max6675_t)];
#endif
    } storage;
};

static hal_thermocouple_impl_t s_pool[HAL_THERMOCOUPLE_MAX_INSTANCES];

/* ── Private helpers ─────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
static inline hal_mcp9600_t *as_mcp(hal_thermocouple_impl_t *h) {
    return reinterpret_cast<hal_mcp9600_t *>(h->storage.mcp_mem);
}
#endif

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
    char buf[80];
    snprintf(buf, sizeof(buf), "%s: %s is not supporting this functionality",
             fn, chip_name(chip));
    hal_serial_println(buf);
}
#endif

/* ── Init / deinit ───────────────────────────────────────────────────────── */

hal_thermocouple_t hal_thermocouple_init(const hal_thermocouple_config_t *cfg) {
    if (!cfg) return NULL;

    /* Atomically reserve a free pool slot across both cores. */
    hal_critical_section_enter();
    int slot = -1;
    for (int i = 0; i < HAL_THERMOCOUPLE_MAX_INSTANCES; i++) {
        if (!s_pool[i].in_use) { slot = i; s_pool[slot].in_use = true; break; }
    }
    hal_critical_section_exit();

    HAL_ASSERT(slot >= 0, "hal_thermocouple: pool exhausted – increase HAL_THERMOCOUPLE_MAX_INSTANCES");
    if (slot < 0) return NULL;

    hal_thermocouple_impl_t *h = &s_pool[slot];
    h->chip  = cfg->chip;
    h->mutex = hal_mutex_create();

#ifdef HAL_ENABLE_MCP9600
    if (cfg->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        const hal_thermocouple_i2c_cfg_t &ic = cfg->bus.i2c;
        hal_i2c_init_bus(ic.i2c_bus, ic.sda_pin, ic.scl_pin, ic.clock_hz);
        hal_mcp9600_t *mcp = new(h->storage.mcp_mem) hal_mcp9600_t();
        const hal_mcp9600_config_t mcp_cfg = {ic.i2c_bus, ic.i2c_addr};
        if (!hal_mcp9600_init(mcp, &mcp_cfg)) {
            hal_mutex_destroy(h->mutex);
            h->mutex  = NULL;
            h->in_use = false;
            hal_serial_println("hal_thermocouple_init: MCP9600 not found");
            return NULL;
        }
    } else
#endif
#ifdef HAL_ENABLE_MAX6675
    if (cfg->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) {
        const hal_thermocouple_spi_cfg_t &sc = cfg->bus.spi;
        hal_max6675_t *max = new(h->storage.max_mem) hal_max6675_t();
        const hal_max6675_config_t max_cfg = {sc.sclk_pin, sc.cs_pin, sc.miso_pin};
        if (!hal_max6675_init(max, &max_cfg)) {
            hal_mutex_destroy(h->mutex);
            h->mutex  = NULL;
            h->in_use = false;
            hal_serial_println("hal_thermocouple_init: MAX6675 init failed");
            return NULL;
        }
    } else
#endif
    {
        hal_mutex_destroy(h->mutex);
        h->mutex  = NULL;
        h->in_use = false;
        hal_serial_println("hal_thermocouple_init: unknown chip type");
        return NULL;
    }

    return h;
}

void hal_thermocouple_deinit(hal_thermocouple_t h) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
#ifdef HAL_ENABLE_MCP9600
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        hal_mcp9600_deinit(as_mcp(h));
    }
#endif
#ifdef HAL_ENABLE_MAX6675
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) {
        hal_max6675_deinit(as_max(h));
    }
#endif
    h->in_use = false;
    hal_mutex_t m = h->mutex;
    h->mutex = NULL;
    hal_mutex_unlock(m);
    hal_mutex_destroy(m);
}

/* ── Temperature reads ───────────────────────────────────────────────────── */

float hal_thermocouple_read(hal_thermocouple_t h) {
    if (!h) return NAN;
    hal_mutex_lock(h->mutex);
    float v = NAN;
#ifdef HAL_ENABLE_MCP9600
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) v = hal_mcp9600_read_thermocouple(as_mcp(h));
    else
#endif
#ifdef HAL_ENABLE_MAX6675
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) v = hal_max6675_read_celsius(as_max(h));
#endif
    (void)v;
    hal_mutex_unlock(h->mutex);
    return v;
}

#ifdef HAL_ENABLE_MCP9600
float hal_thermocouple_read_ambient(hal_thermocouple_t h) {
    if (!h) return NAN;
    hal_mutex_lock(h->mutex);
    float v = NAN;
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        v = hal_mcp9600_read_ambient(as_mcp(h));
    } else {
        not_supported("hal_thermocouple_read_ambient", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}

int32_t hal_thermocouple_read_adc_raw(hal_thermocouple_t h) {
    if (!h) return 0;
    hal_mutex_lock(h->mutex);
    int32_t v = 0;
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        v = hal_mcp9600_read_adc(as_mcp(h));
    } else {
        not_supported("hal_thermocouple_read_adc_raw", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Wire type ───────────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
void hal_thermocouple_set_type(hal_thermocouple_t h, hal_thermocouple_type_t type) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        hal_mcp9600_set_thermocouple_type(
            as_mcp(h), (hal_mcp9600_thermocouple_type_t)type);
    } else {
        not_supported("hal_thermocouple_set_type", h->chip);
    }
    hal_mutex_unlock(h->mutex);
}
#endif /* HAL_ENABLE_MCP9600 */

hal_thermocouple_type_t hal_thermocouple_get_type(hal_thermocouple_t h) {
    if (!h) return HAL_THERMOCOUPLE_TYPE_K;
    hal_mutex_lock(h->mutex);
    hal_thermocouple_type_t v = HAL_THERMOCOUPLE_TYPE_K;
#ifdef HAL_ENABLE_MCP9600
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600)
        v = (hal_thermocouple_type_t)hal_mcp9600_get_thermocouple_type(as_mcp(h));
#endif
    /* MAX6675 is permanently K-type - return the correct value without error. */
    hal_mutex_unlock(h->mutex);
    return v;
}

/* ── IIR filter ──────────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
void hal_thermocouple_set_filter(hal_thermocouple_t h, uint8_t coeff) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        hal_mcp9600_set_filter_coefficient(as_mcp(h), coeff);
    } else {
        not_supported("hal_thermocouple_set_filter", h->chip);
    }
    hal_mutex_unlock(h->mutex);
}

uint8_t hal_thermocouple_get_filter(hal_thermocouple_t h) {
    if (!h) return 0;
    hal_mutex_lock(h->mutex);
    uint8_t v = 0;
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        v = hal_mcp9600_get_filter_coefficient(as_mcp(h));
    } else {
        not_supported("hal_thermocouple_get_filter", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Hot-junction ADC resolution ─────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
void hal_thermocouple_set_adc_resolution(hal_thermocouple_t h,
                                          hal_thermocouple_adc_res_t res) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        hal_mcp9600_set_adc_resolution(
            as_mcp(h), (hal_mcp9600_adc_resolution_t)res);
    } else {
        not_supported("hal_thermocouple_set_adc_resolution", h->chip);
    }
    hal_mutex_unlock(h->mutex);
}

hal_thermocouple_adc_res_t hal_thermocouple_get_adc_resolution(hal_thermocouple_t h) {
    if (!h) return HAL_THERMOCOUPLE_ADC_RES_12;
    hal_mutex_lock(h->mutex);
    hal_thermocouple_adc_res_t v = HAL_THERMOCOUPLE_ADC_RES_12;
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        v = (hal_thermocouple_adc_res_t)hal_mcp9600_get_adc_resolution(as_mcp(h));
    } else {
        not_supported("hal_thermocouple_get_adc_resolution", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Cold-junction (ambient) resolution ──────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
void hal_thermocouple_set_ambient_resolution(hal_thermocouple_t h,
                                              hal_thermocouple_ambient_res_t res) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        hal_mcp9600_set_ambient_resolution(
            as_mcp(h), (hal_mcp9600_ambient_resolution_t)res);
    } else {
        not_supported("hal_thermocouple_set_ambient_resolution", h->chip);
    }
    hal_mutex_unlock(h->mutex);
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Enable / sleep ──────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
void hal_thermocouple_enable(hal_thermocouple_t h, bool enable) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        hal_mcp9600_enable(as_mcp(h), enable);
    } else {
        not_supported("hal_thermocouple_enable", h->chip);
    }
    hal_mutex_unlock(h->mutex);
}
#endif /* HAL_ENABLE_MCP9600 */

bool hal_thermocouple_is_enabled(hal_thermocouple_t h) {
    if (!h) return false;
    hal_mutex_lock(h->mutex);
    bool v = true;  /* MAX6675 has no sleep mode - always active. */
#ifdef HAL_ENABLE_MCP9600
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) v = hal_mcp9600_enabled(as_mcp(h));
#endif
    hal_mutex_unlock(h->mutex);
    return v;
}

/* ── Alerts ──────────────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
void hal_thermocouple_set_alert(hal_thermocouple_t h, uint8_t alert_num,
                                 bool enabled,
                                 const hal_thermocouple_alert_cfg_t *cfg) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip != HAL_THERMOCOUPLE_CHIP_MCP9600) {
        not_supported("hal_thermocouple_set_alert", h->chip);
        hal_mutex_unlock(h->mutex);
        return;
    }
    if (enabled && cfg) {
        hal_mcp9600_set_alert_temperature(as_mcp(h), alert_num,
                                          cfg->temperature);
    }
    hal_mcp9600_configure_alert(
        as_mcp(h),
        alert_num,
        enabled,
        (cfg && enabled) ? cfg->rising             : false,
        (cfg && enabled) ? cfg->alert_cold_junction : false,
        (cfg && enabled) ? cfg->active_high         : false,
        (cfg && enabled) ? cfg->interrupt_mode      : false
    );
    hal_mutex_unlock(h->mutex);
}

float hal_thermocouple_get_alert_temp(hal_thermocouple_t h, uint8_t alert_num) {
    if (!h) return NAN;
    hal_mutex_lock(h->mutex);
    float v = NAN;
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        v = hal_mcp9600_get_alert_temperature(as_mcp(h), alert_num);
    } else {
        not_supported("hal_thermocouple_get_alert_temp", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Status register ─────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
uint8_t hal_thermocouple_get_status(hal_thermocouple_t h) {
    if (!h) return 0;
    hal_mutex_lock(h->mutex);
    uint8_t v = 0;
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        v = hal_mcp9600_get_status(as_mcp(h));
    } else {
        not_supported("hal_thermocouple_get_status", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* HAL_ENABLE_MCP9600 */


#endif /* HAL_ENABLE_THERMOCOUPLE */
#endif  // HAL_TARGET_IS_RP2040
