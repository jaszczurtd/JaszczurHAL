#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_THERMOCOUPLE

#include "hal/temperature/jh_thermocouple_provider.h"

#include <math.h>

namespace {

struct MockThermocoupleContext {
  float temperature;
  float ambient_temperature;
  int32_t adc_raw;
  uint8_t status;
  hal_thermocouple_type_t type;
#ifdef HAL_ENABLE_MCP9600
  uint8_t filter;
  hal_thermocouple_adc_res_t adc_resolution;
  hal_thermocouple_ambient_res_t ambient_resolution;
  bool enabled;
  float alert_temperatures[4];
#endif
};

MockThermocoupleContext *as_mock(void *context) {
  return static_cast<MockThermocoupleContext *>(context);
}

hal_status_t mock_initialize(void *context, const hal_thermocouple_config_t *) {
  MockThermocoupleContext *mock = as_mock(context);
  mock->temperature = 25.0f;
  mock->ambient_temperature = 22.0f;
  mock->type = HAL_THERMOCOUPLE_TYPE_K;
#ifdef HAL_ENABLE_MCP9600
  mock->adc_resolution = HAL_THERMOCOUPLE_ADC_RES_18;
  mock->ambient_resolution = HAL_THERMOCOUPLE_AMBIENT_RES_0_0625;
  mock->enabled = true;
#endif
  return HAL_OK;
}

void mock_deinitialize(void *) {}

hal_status_t mock_read(void *context, float *out_c) {
  *out_c = as_mock(context)->temperature;
  return isnan(*out_c) ? HAL_EIO : HAL_OK;
}

#ifdef HAL_ENABLE_MCP9600
hal_status_t mock_get_type(void *context, hal_thermocouple_type_t *out_type) {
  *out_type = as_mock(context)->type;
  return HAL_OK;
}

hal_status_t mock_is_enabled(void *context, bool *out_enabled) {
  *out_enabled = as_mock(context)->enabled;
  return HAL_OK;
}

hal_status_t mock_read_ambient(void *context, float *out_c) {
  *out_c = as_mock(context)->ambient_temperature;
  return isnan(*out_c) ? HAL_EIO : HAL_OK;
}

hal_status_t mock_read_adc_raw(void *context, int32_t *out_raw) {
  *out_raw = as_mock(context)->adc_raw;
  return HAL_OK;
}

hal_status_t mock_set_type(void *context, hal_thermocouple_type_t type) {
  as_mock(context)->type = type;
  return HAL_OK;
}

hal_status_t mock_set_filter(void *context, uint8_t coeff) {
  as_mock(context)->filter = coeff;
  return HAL_OK;
}

hal_status_t mock_get_filter(void *context, uint8_t *out_coeff) {
  *out_coeff = as_mock(context)->filter;
  return HAL_OK;
}

hal_status_t mock_set_adc_resolution(void *context,
                                     hal_thermocouple_adc_res_t resolution) {
  as_mock(context)->adc_resolution = resolution;
  return HAL_OK;
}

hal_status_t
mock_get_adc_resolution(void *context,
                        hal_thermocouple_adc_res_t *out_resolution) {
  *out_resolution = as_mock(context)->adc_resolution;
  return HAL_OK;
}

hal_status_t
mock_set_ambient_resolution(void *context,
                            hal_thermocouple_ambient_res_t resolution) {
  as_mock(context)->ambient_resolution = resolution;
  return HAL_OK;
}

hal_status_t mock_enable(void *context, bool enabled) {
  as_mock(context)->enabled = enabled;
  return HAL_OK;
}

hal_status_t mock_set_alert(void *context, uint8_t alert_num, bool enabled,
                            const hal_thermocouple_alert_cfg_t *config) {
  if (enabled) {
    as_mock(context)->alert_temperatures[alert_num - 1u] = config->temperature;
  }
  return HAL_OK;
}

hal_status_t mock_get_alert_temp(void *context, uint8_t alert_num,
                                 float *out_c) {
  *out_c = as_mock(context)->alert_temperatures[alert_num - 1u];
  return HAL_OK;
}

hal_status_t mock_get_status(void *context, uint8_t *out_status) {
  *out_status = as_mock(context)->status;
  return HAL_OK;
}
#endif

#ifdef HAL_ENABLE_MCP9600
const jh_thermocouple_provider_ops_t kMockMcpOps = {
    mock_initialize,
    mock_deinitialize,
    mock_read,
    mock_get_type,
    mock_is_enabled,
    mock_read_ambient,
    mock_read_adc_raw,
    mock_set_type,
    mock_set_filter,
    mock_get_filter,
    mock_set_adc_resolution,
    mock_get_adc_resolution,
    mock_set_ambient_resolution,
    mock_enable,
    mock_set_alert,
    mock_get_alert_temp,
    mock_get_status,
};

const jh_thermocouple_provider_t kMockMcpProvider = {
    HAL_THERMOCOUPLE_CHIP_MCP9600,
    "MCP9600",
    sizeof(MockThermocoupleContext),
    alignof(MockThermocoupleContext),
    false,
    &kMockMcpOps};
#endif

#ifdef HAL_ENABLE_MAX6675
hal_status_t mock_max_get_type(void *, hal_thermocouple_type_t *out_type) {
  *out_type = HAL_THERMOCOUPLE_TYPE_K;
  return HAL_OK;
}

hal_status_t mock_max_is_enabled(void *, bool *out_enabled) {
  *out_enabled = true;
  return HAL_OK;
}

const jh_thermocouple_provider_ops_t kMockMaxOps = {
    mock_initialize,
    mock_deinitialize,
    mock_read,
    mock_max_get_type,
    mock_max_is_enabled,
#ifdef HAL_ENABLE_MCP9600
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
#endif
};

const jh_thermocouple_provider_t kMockMaxProvider = {
    HAL_THERMOCOUPLE_CHIP_MAX6675,
    "MAX6675",
    sizeof(MockThermocoupleContext),
    alignof(MockThermocoupleContext),
    false,
    &kMockMaxOps};
#endif

void set_temperature(void *context, void *value) {
  as_mock(context)->temperature = *static_cast<float *>(value);
}

#ifdef HAL_ENABLE_MCP9600
void set_ambient_temperature(void *context, void *value) {
  as_mock(context)->ambient_temperature = *static_cast<float *>(value);
}

void set_adc_raw(void *context, void *value) {
  as_mock(context)->adc_raw = *static_cast<int32_t *>(value);
}

void set_status(void *context, void *value) {
  as_mock(context)->status = *static_cast<uint8_t *>(value);
}
#endif

} // namespace

const jh_thermocouple_provider_t *
jh_thermocouple_provider_get(hal_thermocouple_chip_t chip) {
  switch (chip) {
#ifdef HAL_ENABLE_MCP9600
  case HAL_THERMOCOUPLE_CHIP_MCP9600:
    return &kMockMcpProvider;
#endif
#ifdef HAL_ENABLE_MAX6675
  case HAL_THERMOCOUPLE_CHIP_MAX6675:
    return &kMockMaxProvider;
#endif
  default:
    return nullptr;
  }
}

void hal_mock_thermocouple_set_temp(hal_thermocouple_t handle, float temp) {
  (void)jh_thermocouple_provider_visit_context(handle, set_temperature, &temp);
}

#ifdef HAL_ENABLE_MCP9600
void hal_mock_thermocouple_set_ambient(hal_thermocouple_t handle, float temp) {
  (void)jh_thermocouple_provider_visit_context(handle, set_ambient_temperature,
                                               &temp);
}

void hal_mock_thermocouple_set_adc_raw(hal_thermocouple_t handle, int32_t raw) {
  (void)jh_thermocouple_provider_visit_context(handle, set_adc_raw, &raw);
}

void hal_mock_thermocouple_set_status(hal_thermocouple_t handle,
                                      uint8_t status) {
  (void)jh_thermocouple_provider_visit_context(handle, set_status, &status);
}
#endif

static_assert(sizeof(MockThermocoupleContext) <=
                  JH_THERMOCOUPLE_PROVIDER_CONTEXT_SIZE,
              "Increase JH_THERMOCOUPLE_PROVIDER_CONTEXT_SIZE for mock");

#endif /* HAL_ENABLE_THERMOCOUPLE */
#endif /* HAL_TARGET_IS_MOCK */
