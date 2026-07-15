#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_thermocouple.h"
#include "hal_mock.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Mock instance record ───────────────────────────────────────────────── */

struct hal_thermocouple_impl_s {
  hal_thermocouple_chip_t chip;
  bool in_use;
  hal_mutex_t mutex;

  /* Injected sensor values */
  float mock_temp;
  float mock_ambient;
  int32_t mock_adc_raw;
  uint8_t mock_status;

  /* Configurable state */
  hal_thermocouple_type_t type;
#ifdef HAL_ENABLE_MCP9600
  uint8_t filter;
  hal_thermocouple_adc_res_t adc_res;
  hal_thermocouple_ambient_res_t ambient_res;
  bool enabled;
  float alert_temps[4]; /* [0] = ch1 ... [3] = ch4 */
#endif
};

static hal_thermocouple_impl_t s_pool[HAL_THERMOCOUPLE_MAX_INSTANCES];

/* ── Private helpers ─────────────────────────────────────────────────────── */

static bool valid_chip(hal_thermocouple_chip_t chip) {
  switch (chip) {
#ifdef HAL_ENABLE_MCP9600
  case HAL_THERMOCOUPLE_CHIP_MCP9600:
    return true;
#endif
#ifdef HAL_ENABLE_MAX6675
  case HAL_THERMOCOUPLE_CHIP_MAX6675:
    return true;
#endif
  default:
    return false;
  }
}

static bool valid_handle(hal_thermocouple_t h) {
  return h != NULL && h->in_use && h->mutex != NULL;
}

#ifdef HAL_ENABLE_MCP9600
static const char *chip_name(hal_thermocouple_chip_t chip) {
  switch (chip) {
  case HAL_THERMOCOUPLE_CHIP_MCP9600:
    return "MCP9600";
  case HAL_THERMOCOUPLE_CHIP_MAX6675:
    return "MAX6675";
  default:
    return "UNKNOWN";
  }
}

static void not_supported(const char *fn, hal_thermocouple_chip_t chip) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%s: %s is not supporting this functionality", fn,
           chip_name(chip));
  hal_serial_println(buf);
}
#endif

/* ── Init / deinit ───────────────────────────────────────────────────────── */

hal_thermocouple_t hal_thermocouple_init(const hal_thermocouple_config_t *cfg) {
  hal_thermocouple_t h = NULL;
  (void)hal_thermocouple_init_ex(cfg, &h);
  return h;
}

hal_status_t hal_thermocouple_init_ex(const hal_thermocouple_config_t *cfg,
                                      hal_thermocouple_t *out_handle) {
  if (!cfg || !out_handle)
    return HAL_EINVAL;
  *out_handle = NULL;
  if (!valid_chip(cfg->chip))
    return HAL_EUNSUPPORTED;

  hal_critical_section_enter();
  int slot = -1;
  for (int i = 0; i < HAL_THERMOCOUPLE_MAX_INSTANCES; i++) {
    if (!s_pool[i].in_use) {
      slot = i;
      s_pool[slot].in_use = true;
      break;
    }
  }
  hal_critical_section_exit();

  if (slot < 0)
    return HAL_ENOMEM;

  hal_thermocouple_impl_t *h = &s_pool[slot];
  memset(h, 0, sizeof(*h));
  h->chip = cfg->chip;
  h->in_use = true;
  h->mutex = hal_mutex_create();
  h->mock_temp = 25.0f;
  h->mock_ambient = 22.0f;
#ifdef HAL_ENABLE_MCP9600
  h->enabled = true;
  h->adc_res = HAL_THERMOCOUPLE_ADC_RES_18;
  h->ambient_res = HAL_THERMOCOUPLE_AMBIENT_RES_0_0625;
#endif
  h->type = HAL_THERMOCOUPLE_TYPE_K;
  *out_handle = h;
  return HAL_OK;
}

void hal_thermocouple_deinit(hal_thermocouple_t h) {
  if (!h)
    return;
  hal_mutex_lock(h->mutex);
  h->in_use = false;
  hal_mutex_t m = h->mutex;
  h->mutex = NULL;
  hal_mutex_unlock(m);
  hal_mutex_destroy(m);
}

/* ── Temperature reads ───────────────────────────────────────────────────── */

float hal_thermocouple_read(hal_thermocouple_t h) {
  float value = NAN;
  (void)hal_thermocouple_read_ex(h, &value);
  return value;
}

hal_status_t hal_thermocouple_read_ex(hal_thermocouple_t h, float *out_c) {
  if (!out_c)
    return HAL_EINVAL;
  *out_c = NAN;
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  *out_c = h->mock_temp;
  hal_mutex_unlock(h->mutex);
  return isnan(*out_c) ? HAL_EIO : HAL_OK;
}

#ifdef HAL_ENABLE_MCP9600
float hal_thermocouple_read_ambient(hal_thermocouple_t h) {
  float value = NAN;
  (void)hal_thermocouple_read_ambient_ex(h, &value);
  return value;
}

hal_status_t hal_thermocouple_read_ambient_ex(hal_thermocouple_t h,
                                              float *out_c) {
  if (!out_c)
    return HAL_EINVAL;
  *out_c = NAN;
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_c = h->mock_ambient;
    status = isnan(*out_c) ? HAL_EIO : HAL_OK;
  } else {
    not_supported("hal_thermocouple_read_ambient", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}

int32_t hal_thermocouple_read_adc_raw(hal_thermocouple_t h) {
  int32_t value = 0;
  (void)hal_thermocouple_read_adc_raw_ex(h, &value);
  return value;
}

hal_status_t hal_thermocouple_read_adc_raw_ex(hal_thermocouple_t h,
                                              int32_t *out_raw) {
  if (!out_raw)
    return HAL_EINVAL;
  *out_raw = 0;
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_raw = h->mock_adc_raw;
  } else {
    not_supported("hal_thermocouple_read_adc_raw", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Wire type ───────────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
hal_status_t hal_thermocouple_set_type(hal_thermocouple_t h,
                                       hal_thermocouple_type_t type) {
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    h->type = type;
  } else {
    not_supported("hal_thermocouple_set_type", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}
#endif /* HAL_ENABLE_MCP9600 */

hal_thermocouple_type_t hal_thermocouple_get_type(hal_thermocouple_t h) {
  hal_thermocouple_type_t value = HAL_THERMOCOUPLE_TYPE_K;
  (void)hal_thermocouple_get_type_ex(h, &value);
  return value;
}

hal_status_t hal_thermocouple_get_type_ex(hal_thermocouple_t h,
                                          hal_thermocouple_type_t *out_type) {
  if (!out_type)
    return HAL_EINVAL;
  *out_type = HAL_THERMOCOUPLE_TYPE_K;
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
#ifdef HAL_ENABLE_MCP9600
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600)
    *out_type = h->type;
#endif
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

/* ── IIR filter ──────────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
hal_status_t hal_thermocouple_set_filter(hal_thermocouple_t h, uint8_t coeff) {
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    h->filter = coeff;
  } else {
    not_supported("hal_thermocouple_set_filter", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}

uint8_t hal_thermocouple_get_filter(hal_thermocouple_t h) {
  uint8_t value = 0;
  (void)hal_thermocouple_get_filter_ex(h, &value);
  return value;
}

hal_status_t hal_thermocouple_get_filter_ex(hal_thermocouple_t h,
                                            uint8_t *out_coeff) {
  if (!out_coeff)
    return HAL_EINVAL;
  *out_coeff = 0;
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_coeff = h->filter;
  } else {
    not_supported("hal_thermocouple_get_filter", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Hot-junction ADC resolution ─────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
hal_status_t
hal_thermocouple_set_adc_resolution(hal_thermocouple_t h,
                                    hal_thermocouple_adc_res_t res) {
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    h->adc_res = res;
  } else {
    not_supported("hal_thermocouple_set_adc_resolution", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}

hal_thermocouple_adc_res_t
hal_thermocouple_get_adc_resolution(hal_thermocouple_t h) {
  hal_thermocouple_adc_res_t value = HAL_THERMOCOUPLE_ADC_RES_12;
  (void)hal_thermocouple_get_adc_resolution_ex(h, &value);
  return value;
}

hal_status_t
hal_thermocouple_get_adc_resolution_ex(hal_thermocouple_t h,
                                       hal_thermocouple_adc_res_t *out_res) {
  if (!out_res)
    return HAL_EINVAL;
  *out_res = HAL_THERMOCOUPLE_ADC_RES_12;
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_res = h->adc_res;
  } else {
    not_supported("hal_thermocouple_get_adc_resolution", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Cold-junction (ambient) resolution ──────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
hal_status_t
hal_thermocouple_set_ambient_resolution(hal_thermocouple_t h,
                                        hal_thermocouple_ambient_res_t res) {
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    h->ambient_res = res;
  } else {
    not_supported("hal_thermocouple_set_ambient_resolution", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Enable / sleep ──────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
hal_status_t hal_thermocouple_enable(hal_thermocouple_t h, bool enable) {
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    h->enabled = enable;
  } else {
    not_supported("hal_thermocouple_enable", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}
#endif /* HAL_ENABLE_MCP9600 */

bool hal_thermocouple_is_enabled(hal_thermocouple_t h) {
  bool value = false;
  (void)hal_thermocouple_is_enabled_ex(h, &value);
  return value;
}

hal_status_t hal_thermocouple_is_enabled_ex(hal_thermocouple_t h,
                                            bool *out_enabled) {
  if (!out_enabled)
    return HAL_EINVAL;
  *out_enabled = false;
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  *out_enabled = true;
#ifdef HAL_ENABLE_MCP9600
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600)
    *out_enabled = h->enabled;
#endif
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

/* ── Alerts ──────────────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
hal_status_t
hal_thermocouple_set_alert(hal_thermocouple_t h, uint8_t alert_num,
                           bool enabled,
                           const hal_thermocouple_alert_cfg_t *cfg) {
  if (!valid_handle(h) || alert_num < 1u || alert_num > 4u ||
      (enabled && cfg == NULL))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  if (h->chip != HAL_THERMOCOUPLE_CHIP_MCP9600) {
    not_supported("hal_thermocouple_set_alert", h->chip);
    hal_mutex_unlock(h->mutex);
    return HAL_EUNSUPPORTED;
  }
  if (alert_num >= 1 && alert_num <= 4 && enabled && cfg)
    h->alert_temps[alert_num - 1] = cfg->temperature;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

float hal_thermocouple_get_alert_temp(hal_thermocouple_t h, uint8_t alert_num) {
  float value = NAN;
  (void)hal_thermocouple_get_alert_temp_ex(h, alert_num, &value);
  return value;
}

hal_status_t hal_thermocouple_get_alert_temp_ex(hal_thermocouple_t h,
                                                uint8_t alert_num,
                                                float *out_c) {
  if (!out_c || alert_num < 1u || alert_num > 4u)
    return HAL_EINVAL;
  *out_c = NAN;
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_c = h->alert_temps[alert_num - 1];
  } else {
    not_supported("hal_thermocouple_get_alert_temp", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Status register ─────────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
uint8_t hal_thermocouple_get_status(hal_thermocouple_t h) {
  uint8_t value = 0;
  (void)hal_thermocouple_get_status_ex(h, &value);
  return value;
}

hal_status_t hal_thermocouple_get_status_ex(hal_thermocouple_t h,
                                            uint8_t *out_status) {
  if (!out_status)
    return HAL_EINVAL;
  *out_status = 0;
  if (!valid_handle(h))
    return HAL_EINVAL;
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_status = h->mock_status;
  } else {
    not_supported("hal_thermocouple_get_status", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}
#endif /* HAL_ENABLE_MCP9600 */

/* ── Mock injection helpers ──────────────────────────────────────────────── */

void hal_mock_thermocouple_set_temp(hal_thermocouple_t h, float temp) {
  if (!h)
    return;
  hal_mutex_lock(h->mutex);
  h->mock_temp = temp;
  hal_mutex_unlock(h->mutex);
}

#ifdef HAL_ENABLE_MCP9600
void hal_mock_thermocouple_set_ambient(hal_thermocouple_t h, float temp) {
  if (!h)
    return;
  hal_mutex_lock(h->mutex);
  h->mock_ambient = temp;
  hal_mutex_unlock(h->mutex);
}

void hal_mock_thermocouple_set_adc_raw(hal_thermocouple_t h, int32_t raw) {
  if (!h)
    return;
  hal_mutex_lock(h->mutex);
  h->mock_adc_raw = raw;
  hal_mutex_unlock(h->mutex);
}

void hal_mock_thermocouple_set_status(hal_thermocouple_t h, uint8_t status) {
  if (!h)
    return;
  hal_mutex_lock(h->mutex);
  h->mock_status = status;
  hal_mutex_unlock(h->mutex);
}
#endif /* HAL_ENABLE_MCP9600 */
#endif // HAL_TARGET_IS_MOCK
