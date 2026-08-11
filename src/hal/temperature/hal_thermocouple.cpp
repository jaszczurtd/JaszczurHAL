#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_THERMOCOUPLE

#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/temperature/hal_thermocouple.h"
#include "hal/temperature/jh_thermocouple_provider.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct hal_thermocouple_impl_s {
  const jh_thermocouple_provider_t *provider;
  bool in_use;
  hal_mutex_t mutex;
  alignas(max_align_t) uint8_t
      provider_context[JH_THERMOCOUPLE_PROVIDER_CONTEXT_SIZE];
};

namespace {

hal_thermocouple_impl_t s_pool[HAL_THERMOCOUPLE_MAX_INSTANCES];

bool valid_handle(hal_thermocouple_t handle) {
  return handle != nullptr && handle->in_use && handle->mutex != nullptr &&
         handle->provider != nullptr && handle->provider->ops != nullptr;
}

void release_slot(hal_thermocouple_impl_t *handle) {
  if (handle == nullptr) {
    return;
  }
  if (handle->mutex != nullptr) {
    hal_mutex_destroy(handle->mutex);
    handle->mutex = nullptr;
  }
  handle->provider = nullptr;
  hal_critical_section_enter();
  handle->in_use = false;
  hal_critical_section_exit();
}

bool provider_fits(const jh_thermocouple_provider_t *provider) {
  return provider != nullptr && provider->name != nullptr &&
         provider->ops != nullptr && provider->ops->initialize != nullptr &&
         provider->ops->deinitialize != nullptr &&
         provider->ops->read != nullptr && provider->ops->get_type != nullptr &&
         provider->ops->is_enabled != nullptr &&
         provider->context_size <= JH_THERMOCOUPLE_PROVIDER_CONTEXT_SIZE &&
         provider->context_alignment <= alignof(max_align_t);
}

hal_status_t unsupported(hal_thermocouple_t handle, const char *function) {
  char message[96];
  snprintf(message, sizeof(message),
           "%s: %s is not supporting this functionality", function,
           handle->provider->name);
  hal_serial_println(message);
  return HAL_EUNSUPPORTED;
}

} // namespace

hal_thermocouple_t hal_thermocouple_init(const hal_thermocouple_config_t *cfg) {
  hal_thermocouple_t handle = nullptr;
  (void)hal_thermocouple_init_ex(cfg, &handle);
  return handle;
}

hal_status_t hal_thermocouple_init_ex(const hal_thermocouple_config_t *cfg,
                                      hal_thermocouple_t *out_handle) {
  if (cfg == nullptr || out_handle == nullptr) {
    return HAL_EINVAL;
  }
  *out_handle = nullptr;

  const jh_thermocouple_provider_t *provider =
      jh_thermocouple_provider_get(cfg->chip);
  if (provider == nullptr) {
    hal_serial_println("hal_thermocouple_init: unknown chip type");
    return HAL_EUNSUPPORTED;
  }
  if (!provider_fits(provider)) {
    return HAL_EINTERNAL;
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

  if (slot < 0) {
    if (provider->assert_on_pool_exhaustion) {
      HAL_ASSERT(false, "hal_thermocouple: pool exhausted - increase "
                        "HAL_THERMOCOUPLE_MAX_INSTANCES");
    }
    return HAL_ENOMEM;
  }

  hal_thermocouple_impl_t *handle = &s_pool[slot];
  handle->provider = provider;
  memset(handle->provider_context, 0, sizeof(handle->provider_context));
  handle->mutex = hal_mutex_create();
  if (handle->mutex == nullptr) {
    release_slot(handle);
    return HAL_ENOMEM;
  }

  const hal_status_t status =
      provider->ops->initialize(handle->provider_context, cfg);
  if (!hal_status_is_ok(status)) {
    release_slot(handle);
    return status;
  }

  *out_handle = handle;
  return HAL_OK;
}

void hal_thermocouple_deinit(hal_thermocouple_t handle) {
  if (!valid_handle(handle)) {
    return;
  }

  hal_mutex_t mutex = handle->mutex;
  hal_mutex_lock(mutex);
  handle->provider->ops->deinitialize(handle->provider_context);
  handle->provider = nullptr;
  handle->in_use = false;
  handle->mutex = nullptr;
  hal_mutex_unlock(mutex);
  hal_mutex_destroy(mutex);
}

float hal_thermocouple_read(hal_thermocouple_t handle) {
  float value = NAN;
  (void)hal_thermocouple_read_ex(handle, &value);
  return value;
}

hal_status_t hal_thermocouple_read_ex(hal_thermocouple_t handle, float *out_c) {
  if (out_c == nullptr) {
    return HAL_EINVAL;
  }
  *out_c = NAN;
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->read(handle->provider_context, out_c);
  hal_mutex_unlock(handle->mutex);
  return status;
}

#ifdef HAL_ENABLE_MCP9600
float hal_thermocouple_read_ambient(hal_thermocouple_t handle) {
  float value = NAN;
  (void)hal_thermocouple_read_ambient_ex(handle, &value);
  return value;
}

hal_status_t hal_thermocouple_read_ambient_ex(hal_thermocouple_t handle,
                                              float *out_c) {
  if (out_c == nullptr) {
    return HAL_EINVAL;
  }
  *out_c = NAN;
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->read_ambient == nullptr
          ? unsupported(handle, "hal_thermocouple_read_ambient")
          : handle->provider->ops->read_ambient(handle->provider_context,
                                                out_c);
  hal_mutex_unlock(handle->mutex);
  return status;
}

int32_t hal_thermocouple_read_adc_raw(hal_thermocouple_t handle) {
  int32_t value = 0;
  (void)hal_thermocouple_read_adc_raw_ex(handle, &value);
  return value;
}

hal_status_t hal_thermocouple_read_adc_raw_ex(hal_thermocouple_t handle,
                                              int32_t *out_raw) {
  if (out_raw == nullptr) {
    return HAL_EINVAL;
  }
  *out_raw = 0;
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->read_adc_raw == nullptr
          ? unsupported(handle, "hal_thermocouple_read_adc_raw")
          : handle->provider->ops->read_adc_raw(handle->provider_context,
                                                out_raw);
  hal_mutex_unlock(handle->mutex);
  return status;
}

hal_status_t hal_thermocouple_set_type(hal_thermocouple_t handle,
                                       hal_thermocouple_type_t type) {
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->set_type == nullptr
          ? unsupported(handle, "hal_thermocouple_set_type")
          : handle->provider->ops->set_type(handle->provider_context, type);
  hal_mutex_unlock(handle->mutex);
  return status;
}
#endif

hal_thermocouple_type_t hal_thermocouple_get_type(hal_thermocouple_t handle) {
  hal_thermocouple_type_t value = HAL_THERMOCOUPLE_TYPE_K;
  (void)hal_thermocouple_get_type_ex(handle, &value);
  return value;
}

hal_status_t hal_thermocouple_get_type_ex(hal_thermocouple_t handle,
                                          hal_thermocouple_type_t *out_type) {
  if (out_type == nullptr) {
    return HAL_EINVAL;
  }
  *out_type = HAL_THERMOCOUPLE_TYPE_K;
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->get_type(handle->provider_context, out_type);
  hal_mutex_unlock(handle->mutex);
  return status;
}

#ifdef HAL_ENABLE_MCP9600
hal_status_t hal_thermocouple_set_filter(hal_thermocouple_t handle,
                                         uint8_t coeff) {
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->set_filter == nullptr
          ? unsupported(handle, "hal_thermocouple_set_filter")
          : handle->provider->ops->set_filter(handle->provider_context, coeff);
  hal_mutex_unlock(handle->mutex);
  return status;
}

uint8_t hal_thermocouple_get_filter(hal_thermocouple_t handle) {
  uint8_t value = 0;
  (void)hal_thermocouple_get_filter_ex(handle, &value);
  return value;
}

hal_status_t hal_thermocouple_get_filter_ex(hal_thermocouple_t handle,
                                            uint8_t *out_coeff) {
  if (out_coeff == nullptr) {
    return HAL_EINVAL;
  }
  *out_coeff = 0;
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->get_filter == nullptr
          ? unsupported(handle, "hal_thermocouple_get_filter")
          : handle->provider->ops->get_filter(handle->provider_context,
                                              out_coeff);
  hal_mutex_unlock(handle->mutex);
  return status;
}

hal_status_t
hal_thermocouple_set_adc_resolution(hal_thermocouple_t handle,
                                    hal_thermocouple_adc_res_t resolution) {
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->set_adc_resolution == nullptr
          ? unsupported(handle, "hal_thermocouple_set_adc_resolution")
          : handle->provider->ops->set_adc_resolution(handle->provider_context,
                                                      resolution);
  hal_mutex_unlock(handle->mutex);
  return status;
}

hal_thermocouple_adc_res_t
hal_thermocouple_get_adc_resolution(hal_thermocouple_t handle) {
  hal_thermocouple_adc_res_t value = HAL_THERMOCOUPLE_ADC_RES_12;
  (void)hal_thermocouple_get_adc_resolution_ex(handle, &value);
  return value;
}

hal_status_t hal_thermocouple_get_adc_resolution_ex(
    hal_thermocouple_t handle, hal_thermocouple_adc_res_t *out_resolution) {
  if (out_resolution == nullptr) {
    return HAL_EINVAL;
  }
  *out_resolution = HAL_THERMOCOUPLE_ADC_RES_12;
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->get_adc_resolution == nullptr
          ? unsupported(handle, "hal_thermocouple_get_adc_resolution")
          : handle->provider->ops->get_adc_resolution(handle->provider_context,
                                                      out_resolution);
  hal_mutex_unlock(handle->mutex);
  return status;
}

hal_status_t hal_thermocouple_set_ambient_resolution(
    hal_thermocouple_t handle, hal_thermocouple_ambient_res_t resolution) {
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->set_ambient_resolution == nullptr
          ? unsupported(handle, "hal_thermocouple_set_ambient_resolution")
          : handle->provider->ops->set_ambient_resolution(
                handle->provider_context, resolution);
  hal_mutex_unlock(handle->mutex);
  return status;
}

hal_status_t hal_thermocouple_enable(hal_thermocouple_t handle, bool enabled) {
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->enable == nullptr
          ? unsupported(handle, "hal_thermocouple_enable")
          : handle->provider->ops->enable(handle->provider_context, enabled);
  hal_mutex_unlock(handle->mutex);
  return status;
}
#endif

bool hal_thermocouple_is_enabled(hal_thermocouple_t handle) {
  bool value = false;
  (void)hal_thermocouple_is_enabled_ex(handle, &value);
  return value;
}

hal_status_t hal_thermocouple_is_enabled_ex(hal_thermocouple_t handle,
                                            bool *out_enabled) {
  if (out_enabled == nullptr) {
    return HAL_EINVAL;
  }
  *out_enabled = false;
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->is_enabled(handle->provider_context, out_enabled);
  hal_mutex_unlock(handle->mutex);
  return status;
}

#ifdef HAL_ENABLE_MCP9600
hal_status_t
hal_thermocouple_set_alert(hal_thermocouple_t handle, uint8_t alert_num,
                           bool enabled,
                           const hal_thermocouple_alert_cfg_t *config) {
  if (!valid_handle(handle) || alert_num < 1u || alert_num > 4u ||
      (enabled && config == nullptr)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->set_alert == nullptr
          ? unsupported(handle, "hal_thermocouple_set_alert")
          : handle->provider->ops->set_alert(handle->provider_context,
                                             alert_num, enabled, config);
  hal_mutex_unlock(handle->mutex);
  return status;
}

float hal_thermocouple_get_alert_temp(hal_thermocouple_t handle,
                                      uint8_t alert_num) {
  float value = NAN;
  (void)hal_thermocouple_get_alert_temp_ex(handle, alert_num, &value);
  return value;
}

hal_status_t hal_thermocouple_get_alert_temp_ex(hal_thermocouple_t handle,
                                                uint8_t alert_num,
                                                float *out_c) {
  if (out_c == nullptr || alert_num < 1u || alert_num > 4u) {
    return HAL_EINVAL;
  }
  *out_c = NAN;
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->get_alert_temp == nullptr
          ? unsupported(handle, "hal_thermocouple_get_alert_temp")
          : handle->provider->ops->get_alert_temp(handle->provider_context,
                                                  alert_num, out_c);
  hal_mutex_unlock(handle->mutex);
  return status;
}

uint8_t hal_thermocouple_get_status(hal_thermocouple_t handle) {
  uint8_t value = 0;
  (void)hal_thermocouple_get_status_ex(handle, &value);
  return value;
}

hal_status_t hal_thermocouple_get_status_ex(hal_thermocouple_t handle,
                                            uint8_t *out_status) {
  if (out_status == nullptr) {
    return HAL_EINVAL;
  }
  *out_status = 0;
  if (!valid_handle(handle)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->ops->get_status == nullptr
          ? unsupported(handle, "hal_thermocouple_get_status")
          : handle->provider->ops->get_status(handle->provider_context,
                                              out_status);
  hal_mutex_unlock(handle->mutex);
  return status;
}
#endif

hal_status_t jh_thermocouple_provider_visit_context(
    hal_thermocouple_t handle, jh_thermocouple_context_visitor_t visitor,
    void *visitor_context) {
  if (!valid_handle(handle) || visitor == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(handle->mutex);
  visitor(handle->provider_context, visitor_context);
  hal_mutex_unlock(handle->mutex);
  return HAL_OK;
}

#endif /* HAL_ENABLE_THERMOCOUPLE */
