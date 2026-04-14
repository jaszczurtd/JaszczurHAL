#include "../../hal_config.h"
#ifndef HAL_DISABLE_THERMOCOUPLE

#include "../../hal_thermocouple.h"
#include "../../hal_i2c.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#ifndef HAL_DISABLE_MCP9600
#include "drivers/Adafruit_MCP9600/Adafruit_MCP9600.h"
#endif
#ifndef HAL_DISABLE_MAX6675
#include "drivers/MAX6675/max6675.h"
#endif
#include <Wire.h>
#include <new>
#include <math.h>
#include <string.h>

/* ── Internal instance record ───────────────────────────────────────────── */

struct hal_thermocouple_impl_s {
    hal_thermocouple_chip_t chip;
    bool                    in_use;
    hal_mutex_t             mutex;
    union {
#ifndef HAL_DISABLE_MCP9600
        alignas(Adafruit_MCP9600) uint8_t mcp_mem[sizeof(Adafruit_MCP9600)];
#endif
#ifndef HAL_DISABLE_MAX6675
        alignas(MAX6675)           uint8_t max_mem[sizeof(MAX6675)];
#endif
    } storage;
};

static hal_thermocouple_impl_t s_pool[HAL_THERMOCOUPLE_MAX_INSTANCES];

/* ── Private helpers ─────────────────────────────────────────────────────── */

#ifndef HAL_DISABLE_MCP9600
static inline Adafruit_MCP9600 *as_mcp(hal_thermocouple_impl_t *h) {
    return reinterpret_cast<Adafruit_MCP9600 *>(h->storage.mcp_mem);
}

static TwoWire *thermocouple_i2c_wire(uint8_t bus) {
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
    return bus == 1 ? &Wire1 : &Wire;
#else
    (void)bus;
    return &Wire;
#endif
}
#endif

#ifndef HAL_DISABLE_MAX6675
static inline MAX6675 *as_max(hal_thermocouple_impl_t *h) {
    return reinterpret_cast<MAX6675 *>(h->storage.max_mem);
}
#endif

#ifndef HAL_DISABLE_MCP9600
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

#ifndef HAL_DISABLE_MCP9600
    if (cfg->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        const hal_thermocouple_i2c_cfg_t &ic = cfg->bus.i2c;
        hal_i2c_init_bus(ic.i2c_bus, ic.sda_pin, ic.scl_pin, ic.clock_hz);
        Adafruit_MCP9600 *mcp = new(h->storage.mcp_mem) Adafruit_MCP9600();
        if (!mcp->begin(ic.i2c_addr, thermocouple_i2c_wire(ic.i2c_bus))) {
            mcp->~Adafruit_MCP9600();
            hal_mutex_destroy(h->mutex);
            h->mutex  = NULL;
            h->in_use = false;
            hal_serial_println("hal_thermocouple_init: MCP9600 not found");
            return NULL;
        }
    } else
#endif
#ifndef HAL_DISABLE_MAX6675
    if (cfg->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) {
        const hal_thermocouple_spi_cfg_t &sc = cfg->bus.spi;
        new(h->storage.max_mem) MAX6675(
            (int8_t)sc.sclk_pin,
            (int8_t)sc.cs_pin,
            (int8_t)sc.miso_pin
        );
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
#ifndef HAL_DISABLE_MCP9600
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        as_mcp(h)->~Adafruit_MCP9600();
    }
#endif
#ifndef HAL_DISABLE_MAX6675
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) {
        as_max(h)->~MAX6675();
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
#ifndef HAL_DISABLE_MCP9600
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) v = as_mcp(h)->readThermocouple();
    else
#endif
#ifndef HAL_DISABLE_MAX6675
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) v = as_max(h)->readCelsius();
#endif
    (void)v;
    hal_mutex_unlock(h->mutex);
    return v;
}

#ifndef HAL_DISABLE_MCP9600
float hal_thermocouple_read_ambient(hal_thermocouple_t h) {
    if (!h) return NAN;
    hal_mutex_lock(h->mutex);
    float v = NAN;
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        v = as_mcp(h)->readAmbient();
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
        v = as_mcp(h)->readADC();
    } else {
        not_supported("hal_thermocouple_read_adc_raw", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* !HAL_DISABLE_MCP9600 */

/* ── Wire type ───────────────────────────────────────────────────────────── */

#ifndef HAL_DISABLE_MCP9600
void hal_thermocouple_set_type(hal_thermocouple_t h, hal_thermocouple_type_t type) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        as_mcp(h)->setThermocoupleType((MCP9600_ThemocoupleType)type);
    } else {
        not_supported("hal_thermocouple_set_type", h->chip);
    }
    hal_mutex_unlock(h->mutex);
}
#endif /* !HAL_DISABLE_MCP9600 */

hal_thermocouple_type_t hal_thermocouple_get_type(hal_thermocouple_t h) {
    if (!h) return HAL_THERMOCOUPLE_TYPE_K;
    hal_mutex_lock(h->mutex);
    hal_thermocouple_type_t v = HAL_THERMOCOUPLE_TYPE_K;
#ifndef HAL_DISABLE_MCP9600
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600)
        v = (hal_thermocouple_type_t)as_mcp(h)->getThermocoupleType();
#endif
    /* MAX6675 is permanently K-type — return the correct value without error. */
    hal_mutex_unlock(h->mutex);
    return v;
}

/* ── IIR filter ──────────────────────────────────────────────────────────── */

#ifndef HAL_DISABLE_MCP9600
void hal_thermocouple_set_filter(hal_thermocouple_t h, uint8_t coeff) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        as_mcp(h)->setFilterCoefficient(coeff);
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
        v = as_mcp(h)->getFilterCoefficient();
    } else {
        not_supported("hal_thermocouple_get_filter", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* !HAL_DISABLE_MCP9600 */

/* ── Hot-junction ADC resolution ─────────────────────────────────────────── */

#ifndef HAL_DISABLE_MCP9600
void hal_thermocouple_set_adc_resolution(hal_thermocouple_t h,
                                          hal_thermocouple_adc_res_t res) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        as_mcp(h)->setADCresolution((MCP9600_ADCResolution)res);
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
        v = (hal_thermocouple_adc_res_t)as_mcp(h)->getADCresolution();
    } else {
        not_supported("hal_thermocouple_get_adc_resolution", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* !HAL_DISABLE_MCP9600 */

/* ── Cold-junction (ambient) resolution ──────────────────────────────────── */

#ifndef HAL_DISABLE_MCP9600
void hal_thermocouple_set_ambient_resolution(hal_thermocouple_t h,
                                              hal_thermocouple_ambient_res_t res) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        as_mcp(h)->setAmbientResolution((Ambient_Resolution)res);
    } else {
        not_supported("hal_thermocouple_set_ambient_resolution", h->chip);
    }
    hal_mutex_unlock(h->mutex);
}
#endif /* !HAL_DISABLE_MCP9600 */

/* ── Enable / sleep ──────────────────────────────────────────────────────── */

#ifndef HAL_DISABLE_MCP9600
void hal_thermocouple_enable(hal_thermocouple_t h, bool enable) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        as_mcp(h)->enable(enable);
    } else {
        not_supported("hal_thermocouple_enable", h->chip);
    }
    hal_mutex_unlock(h->mutex);
}
#endif /* !HAL_DISABLE_MCP9600 */

bool hal_thermocouple_is_enabled(hal_thermocouple_t h) {
    if (!h) return false;
    hal_mutex_lock(h->mutex);
    bool v = true;  /* MAX6675 has no sleep mode — always active. */
#ifndef HAL_DISABLE_MCP9600
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) v = as_mcp(h)->enabled();
#endif
    hal_mutex_unlock(h->mutex);
    return v;
}

/* ── Alerts ──────────────────────────────────────────────────────────────── */

#ifndef HAL_DISABLE_MCP9600
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
        as_mcp(h)->setAlertTemperature(alert_num, cfg->temperature);
    }
    as_mcp(h)->configureAlert(
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
        v = as_mcp(h)->getAlertTemperature(alert_num);
    } else {
        not_supported("hal_thermocouple_get_alert_temp", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* !HAL_DISABLE_MCP9600 */

/* ── Status register ─────────────────────────────────────────────────────── */

#ifndef HAL_DISABLE_MCP9600
uint8_t hal_thermocouple_get_status(hal_thermocouple_t h) {
    if (!h) return 0;
    hal_mutex_lock(h->mutex);
    uint8_t v = 0;
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        v = as_mcp(h)->getStatus();
    } else {
        not_supported("hal_thermocouple_get_status", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* !HAL_DISABLE_MCP9600 */


#endif /* HAL_DISABLE_THERMOCOUPLE */
