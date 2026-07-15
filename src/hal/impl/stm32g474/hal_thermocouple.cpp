#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_config.h"
#ifdef HAL_ENABLE_THERMOCOUPLE

#include "../../hal_i2c.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_thermocouple.h"
#ifdef HAL_ENABLE_MCP9600
#include "../shared/drivers/mcp9600/mcp9600_driver.h"
#endif
#ifdef HAL_ENABLE_MAX6675
#include "../shared/drivers/max6675/max6675_driver.h"
#endif

#include <math.h>
#include <new>
#include <stdio.h>

struct hal_thermocouple_impl_s {
  hal_thermocouple_chip_t chip;
  bool in_use;
  hal_mutex_t mutex;
  union {
    uint8_t dummy;
#ifdef HAL_ENABLE_MCP9600
    alignas(hal_mcp9600_t) uint8_t mcp_mem[sizeof(hal_mcp9600_t)];
#endif
#ifdef HAL_ENABLE_MAX6675
    alignas(hal_max6675_t) uint8_t max_mem[sizeof(hal_max6675_t)];
#endif
  } storage;
};

static hal_thermocouple_impl_t s_pool[HAL_THERMOCOUPLE_MAX_INSTANCES];

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
  case HAL_THERMOCOUPLE_CHIP_MCP9600:
    return "MCP9600";
  case HAL_THERMOCOUPLE_CHIP_MAX6675:
    return "MAX6675";
  default:
    return "UNKNOWN";
  }
}

static void not_supported(const char *fn, hal_thermocouple_chip_t chip) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s: %s is not supporting this functionality", fn,
           chip_name(chip));
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

static bool valid_handle(hal_thermocouple_t h) {
  return h != nullptr && h->in_use && h->mutex != nullptr;
}

hal_thermocouple_t hal_thermocouple_init(const hal_thermocouple_config_t *cfg) {
  hal_thermocouple_t h = nullptr;
  (void)hal_thermocouple_init_ex(cfg, &h);
  return h;
}

hal_status_t hal_thermocouple_init_ex(const hal_thermocouple_config_t *cfg,
                                      hal_thermocouple_t *out_handle) {
  if (cfg == nullptr || out_handle == nullptr) {
    return HAL_EINVAL;
  }
  *out_handle = nullptr;

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

  HAL_ASSERT(slot >= 0, "hal_thermocouple: pool exhausted - increase "
                        "HAL_THERMOCOUPLE_MAX_INSTANCES");
  if (slot < 0) {
    return HAL_ENOMEM;
  }

  hal_thermocouple_impl_t *h = &s_pool[slot];
  h->chip = cfg->chip;
  h->mutex = hal_mutex_create();
  if (h->mutex == nullptr) {
    release_slot(h);
    return HAL_ENOMEM;
  }

#ifdef HAL_ENABLE_MAX6675
  if (cfg->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) {
    const hal_thermocouple_spi_cfg_t &sc = cfg->bus.spi;
    hal_max6675_t *max = new (h->storage.max_mem) hal_max6675_t();
    const hal_max6675_config_t max_cfg = {sc.sclk_pin, sc.cs_pin, sc.miso_pin};
    if (!hal_max6675_init(max, &max_cfg)) {
      release_slot(h);
      hal_serial_println("hal_thermocouple_init: MAX6675 init failed");
      return HAL_EIO;
    }
    *out_handle = h;
    return HAL_OK;
  }
#endif

#ifdef HAL_ENABLE_MCP9600
  if (cfg->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    const hal_thermocouple_i2c_cfg_t &ic = cfg->bus.i2c;
    hal_status_t status =
        hal_i2c_init_bus(ic.i2c_bus, ic.sda_pin, ic.scl_pin, ic.clock_hz);
    if (!hal_status_is_ok(status)) {
      release_slot(h);
      return status;
    }
    hal_mcp9600_t *mcp = new (h->storage.mcp_mem) hal_mcp9600_t();
    const hal_mcp9600_config_t mcp_cfg = {ic.i2c_bus, ic.i2c_addr};
    if (!hal_mcp9600_init(mcp, &mcp_cfg)) {
      release_slot(h);
      hal_serial_println("hal_thermocouple_init: MCP9600 not found");
      return HAL_EIO;
    }
    *out_handle = h;
    return HAL_OK;
  }
#endif

  release_slot(h);
  hal_serial_println("hal_thermocouple_init: unknown chip type");
  return HAL_EUNSUPPORTED;
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
#ifdef HAL_ENABLE_MCP9600
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    hal_mcp9600_deinit(as_mcp(h));
  }
#endif
  h->in_use = false;
  hal_mutex_t m = h->mutex;
  h->mutex = nullptr;
  hal_mutex_unlock(m);
  hal_mutex_destroy(m);
}

float hal_thermocouple_read(hal_thermocouple_t h) {
  float value = NAN;
  (void)hal_thermocouple_read_ex(h, &value);
  return value;
}

hal_status_t hal_thermocouple_read_ex(hal_thermocouple_t h, float *out_c) {
  if (out_c == nullptr) {
    return HAL_EINVAL;
  }
  *out_c = NAN;
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
#ifdef HAL_ENABLE_MCP9600
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_c = hal_mcp9600_read_thermocouple(as_mcp(h));
  } else
#endif
#ifdef HAL_ENABLE_MAX6675
      if (h->chip == HAL_THERMOCOUPLE_CHIP_MAX6675) {
    *out_c = hal_max6675_read_celsius(as_max(h));
  }
#endif
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
  if (out_c == nullptr) {
    return HAL_EINVAL;
  }
  *out_c = NAN;
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_c = hal_mcp9600_read_ambient(as_mcp(h));
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
  if (out_raw == nullptr) {
    return HAL_EINVAL;
  }
  *out_raw = 0;
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_raw = hal_mcp9600_read_adc(as_mcp(h));
  } else {
    not_supported("hal_thermocouple_read_adc_raw", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}

hal_status_t hal_thermocouple_set_type(hal_thermocouple_t h,
                                       hal_thermocouple_type_t type) {
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    hal_mcp9600_set_thermocouple_type(as_mcp(h),
                                      (hal_mcp9600_thermocouple_type_t)type);
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
  if (out_type == nullptr) {
    return HAL_EINVAL;
  }
  *out_type = HAL_THERMOCOUPLE_TYPE_K;
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
#ifdef HAL_ENABLE_MCP9600
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_type =
        (hal_thermocouple_type_t)hal_mcp9600_get_thermocouple_type(as_mcp(h));
  }
#endif
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

#ifdef HAL_ENABLE_MCP9600
hal_status_t hal_thermocouple_set_filter(hal_thermocouple_t h, uint8_t coeff) {
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    hal_mcp9600_set_filter_coefficient(as_mcp(h), coeff);
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
  if (out_coeff == nullptr) {
    return HAL_EINVAL;
  }
  *out_coeff = 0;
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_coeff = hal_mcp9600_get_filter_coefficient(as_mcp(h));
  } else {
    not_supported("hal_thermocouple_get_filter", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}

hal_status_t
hal_thermocouple_set_adc_resolution(hal_thermocouple_t h,
                                    hal_thermocouple_adc_res_t res) {
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    hal_mcp9600_set_adc_resolution(as_mcp(h),
                                   (hal_mcp9600_adc_resolution_t)res);
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
  if (out_res == nullptr) {
    return HAL_EINVAL;
  }
  *out_res = HAL_THERMOCOUPLE_ADC_RES_12;
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_res =
        (hal_thermocouple_adc_res_t)hal_mcp9600_get_adc_resolution(as_mcp(h));
  } else {
    not_supported("hal_thermocouple_get_adc_resolution", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}

hal_status_t
hal_thermocouple_set_ambient_resolution(hal_thermocouple_t h,
                                        hal_thermocouple_ambient_res_t res) {
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    hal_mcp9600_set_ambient_resolution(as_mcp(h),
                                       (hal_mcp9600_ambient_resolution_t)res);
  } else {
    not_supported("hal_thermocouple_set_ambient_resolution", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}

hal_status_t hal_thermocouple_enable(hal_thermocouple_t h, bool enable) {
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    hal_mcp9600_enable(as_mcp(h), enable);
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
  if (out_enabled == nullptr) {
    return HAL_EINVAL;
  }
  *out_enabled = false;
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  *out_enabled = h->chip == HAL_THERMOCOUPLE_CHIP_MAX6675;
#ifdef HAL_ENABLE_MCP9600
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_enabled = hal_mcp9600_enabled(as_mcp(h));
  }
#endif
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

#ifdef HAL_ENABLE_MCP9600
hal_status_t
hal_thermocouple_set_alert(hal_thermocouple_t h, uint8_t alert_num,
                           bool enabled,
                           const hal_thermocouple_alert_cfg_t *cfg) {
  if (!valid_handle(h) || alert_num < 1u || alert_num > 4u ||
      (enabled && cfg == nullptr)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  if (h->chip != HAL_THERMOCOUPLE_CHIP_MCP9600) {
    not_supported("hal_thermocouple_set_alert", h->chip);
    hal_mutex_unlock(h->mutex);
    return HAL_EUNSUPPORTED;
  }
  if (enabled && cfg) {
    hal_mcp9600_set_alert_temperature(as_mcp(h), alert_num, cfg->temperature);
  }
  hal_mcp9600_configure_alert(
      as_mcp(h), alert_num, enabled, (cfg && enabled) ? cfg->rising : false,
      (cfg && enabled) ? cfg->alert_cold_junction : false,
      (cfg && enabled) ? cfg->active_high : false,
      (cfg && enabled) ? cfg->interrupt_mode : false);
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
  if (out_c == nullptr || alert_num < 1u || alert_num > 4u) {
    return HAL_EINVAL;
  }
  *out_c = NAN;
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_c = hal_mcp9600_get_alert_temperature(as_mcp(h), alert_num);
    status = isnan(*out_c) ? HAL_EIO : HAL_OK;
  } else {
    not_supported("hal_thermocouple_get_alert_temp", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}

uint8_t hal_thermocouple_get_status(hal_thermocouple_t h) {
  uint8_t value = 0;
  (void)hal_thermocouple_get_status_ex(h, &value);
  return value;
}

hal_status_t hal_thermocouple_get_status_ex(hal_thermocouple_t h,
                                            uint8_t *out_status) {
  if (out_status == nullptr) {
    return HAL_EINVAL;
  }
  *out_status = 0;
  if (!valid_handle(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t status = HAL_OK;
  if (h->chip == HAL_THERMOCOUPLE_CHIP_MCP9600) {
    *out_status = hal_mcp9600_get_status(as_mcp(h));
  } else {
    not_supported("hal_thermocouple_get_status", h->chip);
    status = HAL_EUNSUPPORTED;
  }
  hal_mutex_unlock(h->mutex);
  return status;
}
#endif /* HAL_ENABLE_MCP9600 */

#endif /* HAL_ENABLE_THERMOCOUPLE */
#endif /* HAL_TARGET_IS_STM32G474 */
