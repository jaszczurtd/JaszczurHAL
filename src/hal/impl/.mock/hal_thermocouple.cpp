#include "../../hal_thermocouple.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "hal_mock.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Mock instance record ───────────────────────────────────────────────── */

struct hal_thermocouple_impl_s {
    hal_thermocouple_chip_t        chip;
    bool                           in_use;
    hal_mutex_t                    mutex;

    /* Injected sensor values */
    float                          mock_temp;
    float                          mock_ambient;
    int32_t                        mock_adc_raw;
    uint8_t                        mock_status;

    /* Configurable state */
    hal_thermocouple_type_t        type;
#ifndef HAL_DISABLE_MCP9600
    uint8_t                        filter;
    hal_thermocouple_adc_res_t     adc_res;
    hal_thermocouple_ambient_res_t ambient_res;
    bool                           enabled;
    float                          alert_temps[4];  /* [0] = ch1 … [3] = ch4 */
#endif
};

static hal_thermocouple_impl_t s_pool[HAL_THERMOCOUPLE_MAX_INSTANCES];

/* ── Private helpers ─────────────────────────────────────────────────────── */

#ifndef HAL_DISABLE_MCP9600
static const char *chip_name(hal_thermocouple_chip_t chip) {
    switch (chip) {
        case HAL_THERMOCOUPLE_CHIP_MCP9600: return "MCP9600";
        case HAL_THERMOCOUPLE_CHIP_MAX6675: return "MAX6675";
        default:                            return "UNKNOWN";
    }
}

static void not_supported(const char *fn, hal_thermocouple_chip_t chip) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: %s is not supporting this functionality",
             fn, chip_name(chip));
    hal_serial_println(buf);
}
#endif

/* ── Init / deinit ───────────────────────────────────────────────────────── */

hal_thermocouple_t hal_thermocouple_init(const hal_thermocouple_config_t *cfg) {
    if (!cfg) return NULL;

    hal_critical_section_enter();
    int slot = -1;
    for (int i = 0; i < HAL_THERMOCOUPLE_MAX_INSTANCES; i++) {
        if (!s_pool[i].in_use) { slot = i; s_pool[slot].in_use = true; break; }
    }
    hal_critical_section_exit();

    if (slot < 0) return NULL;

    hal_thermocouple_impl_t *h = &s_pool[slot];
    memset(h, 0, sizeof(*h));
    h->chip         = cfg->chip;
    h->in_use       = true;
    h->mutex        = hal_mutex_create();
    h->mock_temp    = 25.0f;
    h->mock_ambient = 22.0f;
#ifndef HAL_DISABLE_MCP9600
    h->enabled      = true;
    h->adc_res      = HAL_THERMOCOUPLE_ADC_RES_18;
    h->ambient_res  = HAL_THERMOCOUPLE_AMBIENT_RES_0_0625;
#endif
    h->type         = HAL_THERMOCOUPLE_TYPE_K;
    return h;
}

void hal_thermocouple_deinit(hal_thermocouple_t h) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
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
    float v = h->mock_temp;
    hal_mutex_unlock(h->mutex);
    return v;
}

#ifndef HAL_DISABLE_MCP9600
float hal_thermocouple_read_ambient(hal_thermocouple_t h) {
    if (!h) return NAN;
    hal_mutex_lock(h->mutex);
    float v = NAN;
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        v = h->mock_ambient;
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
        v = h->mock_adc_raw;
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
        h->type = type;
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
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) v = h->type;
#endif
    hal_mutex_unlock(h->mutex);
    return v;
}

/* ── IIR filter ──────────────────────────────────────────────────────────── */

#ifndef HAL_DISABLE_MCP9600
void hal_thermocouple_set_filter(hal_thermocouple_t h, uint8_t coeff) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        h->filter = coeff;
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
        v = h->filter;
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
        h->adc_res = res;
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
        v = h->adc_res;
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
        h->ambient_res = res;
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
        h->enabled = enable;
    } else {
        not_supported("hal_thermocouple_enable", h->chip);
    }
    hal_mutex_unlock(h->mutex);
}
#endif /* !HAL_DISABLE_MCP9600 */

bool hal_thermocouple_is_enabled(hal_thermocouple_t h) {
    if (!h) return false;
    hal_mutex_lock(h->mutex);
    bool v = true;
#ifndef HAL_DISABLE_MCP9600
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) v = h->enabled;
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
    if (alert_num >= 1 && alert_num <= 4 && enabled && cfg)
        h->alert_temps[alert_num - 1] = cfg->temperature;
    hal_mutex_unlock(h->mutex);
}

float hal_thermocouple_get_alert_temp(hal_thermocouple_t h, uint8_t alert_num) {
    if (!h) return NAN;
    hal_mutex_lock(h->mutex);
    float v = NAN;
    if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
        if (alert_num >= 1 && alert_num <= 4)
            v = h->alert_temps[alert_num - 1];
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
        v = h->mock_status;
    } else {
        not_supported("hal_thermocouple_get_status", h->chip);
    }
    hal_mutex_unlock(h->mutex);
    return v;
}
#endif /* !HAL_DISABLE_MCP9600 */

/* ── Mock injection helpers ──────────────────────────────────────────────── */

void hal_mock_thermocouple_set_temp(hal_thermocouple_t h, float temp) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->mock_temp = temp;
    hal_mutex_unlock(h->mutex);
}

#ifndef HAL_DISABLE_MCP9600
void hal_mock_thermocouple_set_ambient(hal_thermocouple_t h, float temp) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->mock_ambient = temp;
    hal_mutex_unlock(h->mutex);
}

void hal_mock_thermocouple_set_adc_raw(hal_thermocouple_t h, int32_t raw) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->mock_adc_raw = raw;
    hal_mutex_unlock(h->mutex);
}

void hal_mock_thermocouple_set_status(hal_thermocouple_t h, uint8_t status) {
    if (!h) return;
    hal_mutex_lock(h->mutex);
    h->mock_status = status;
    hal_mutex_unlock(h->mutex);
}
#endif /* !HAL_DISABLE_MCP9600 */
