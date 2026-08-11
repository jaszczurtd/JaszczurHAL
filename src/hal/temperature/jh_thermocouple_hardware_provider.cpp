#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_THERMOCOUPLE

#include "hal/i2c/hal_i2c.h"
#include "hal/serial/hal_serial.h"
#include "hal/temperature/jh_thermocouple_provider.h"
#ifdef HAL_ENABLE_MAX6675
#include "hal/temperature/max6675/max6675_driver.h"
#endif
#ifdef HAL_ENABLE_MCP9600
#include "hal/temperature/mcp9600/mcp9600_driver.h"
#endif

#include <math.h>
#include <new>

namespace {

#ifdef HAL_ENABLE_MCP9600
hal_mcp9600_t *as_mcp(void *context) {
  return static_cast<hal_mcp9600_t *>(context);
}

hal_status_t mcp_initialize(void *context,
                            const hal_thermocouple_config_t *config) {
  const hal_thermocouple_i2c_cfg_t &bus = config->bus.i2c;
  const hal_status_t status =
      hal_i2c_init_bus(bus.i2c_bus, bus.sda_pin, bus.scl_pin, bus.clock_hz);
  if (!hal_status_is_ok(status)) {
    return status;
  }

  hal_mcp9600_t *device = new (context) hal_mcp9600_t();
  const hal_mcp9600_config_t driver_config = {bus.i2c_bus, bus.i2c_addr};
  if (!hal_mcp9600_init(device, &driver_config)) {
    hal_serial_println("hal_thermocouple_init: MCP9600 not found");
    return HAL_EIO;
  }
  return HAL_OK;
}

void mcp_deinitialize(void *context) { hal_mcp9600_deinit(as_mcp(context)); }

hal_status_t mcp_read(void *context, float *out_c) {
  *out_c = hal_mcp9600_read_thermocouple(as_mcp(context));
  return isnan(*out_c) ? HAL_EIO : HAL_OK;
}

hal_status_t mcp_get_type(void *context, hal_thermocouple_type_t *out_type) {
  *out_type = static_cast<hal_thermocouple_type_t>(
      hal_mcp9600_get_thermocouple_type(as_mcp(context)));
  return HAL_OK;
}

hal_status_t mcp_is_enabled(void *context, bool *out_enabled) {
  *out_enabled = hal_mcp9600_enabled(as_mcp(context));
  return HAL_OK;
}

hal_status_t mcp_read_ambient(void *context, float *out_c) {
  *out_c = hal_mcp9600_read_ambient(as_mcp(context));
  return isnan(*out_c) ? HAL_EIO : HAL_OK;
}

hal_status_t mcp_read_adc_raw(void *context, int32_t *out_raw) {
  *out_raw = hal_mcp9600_read_adc(as_mcp(context));
  return HAL_OK;
}

hal_status_t mcp_set_type(void *context, hal_thermocouple_type_t type) {
  hal_mcp9600_set_thermocouple_type(
      as_mcp(context), static_cast<hal_mcp9600_thermocouple_type_t>(type));
  return HAL_OK;
}

hal_status_t mcp_set_filter(void *context, uint8_t coeff) {
  hal_mcp9600_set_filter_coefficient(as_mcp(context), coeff);
  return HAL_OK;
}

hal_status_t mcp_get_filter(void *context, uint8_t *out_coeff) {
  *out_coeff = hal_mcp9600_get_filter_coefficient(as_mcp(context));
  return HAL_OK;
}

hal_status_t mcp_set_adc_resolution(void *context,
                                    hal_thermocouple_adc_res_t resolution) {
  hal_mcp9600_set_adc_resolution(
      as_mcp(context), static_cast<hal_mcp9600_adc_resolution_t>(resolution));
  return HAL_OK;
}

hal_status_t
mcp_get_adc_resolution(void *context,
                       hal_thermocouple_adc_res_t *out_resolution) {
  *out_resolution = static_cast<hal_thermocouple_adc_res_t>(
      hal_mcp9600_get_adc_resolution(as_mcp(context)));
  return HAL_OK;
}

hal_status_t
mcp_set_ambient_resolution(void *context,
                           hal_thermocouple_ambient_res_t resolution) {
  hal_mcp9600_set_ambient_resolution(
      as_mcp(context),
      static_cast<hal_mcp9600_ambient_resolution_t>(resolution));
  return HAL_OK;
}

hal_status_t mcp_enable(void *context, bool enabled) {
  hal_mcp9600_enable(as_mcp(context), enabled);
  return HAL_OK;
}

hal_status_t mcp_set_alert(void *context, uint8_t alert_num, bool enabled,
                           const hal_thermocouple_alert_cfg_t *config) {
  hal_mcp9600_t *device = as_mcp(context);
  if (enabled) {
    hal_mcp9600_set_alert_temperature(device, alert_num, config->temperature);
  }
  hal_mcp9600_configure_alert(device, alert_num, enabled,
                              enabled ? config->rising : false,
                              enabled ? config->alert_cold_junction : false,
                              enabled ? config->active_high : false,
                              enabled ? config->interrupt_mode : false);
  return HAL_OK;
}

hal_status_t mcp_get_alert_temp(void *context, uint8_t alert_num,
                                float *out_c) {
  *out_c = hal_mcp9600_get_alert_temperature(as_mcp(context), alert_num);
  return isnan(*out_c) ? HAL_EIO : HAL_OK;
}

hal_status_t mcp_get_status(void *context, uint8_t *out_status) {
  *out_status = hal_mcp9600_get_status(as_mcp(context));
  return HAL_OK;
}

const jh_thermocouple_provider_ops_t kMcpOps = {
    mcp_initialize,
    mcp_deinitialize,
    mcp_read,
    mcp_get_type,
    mcp_is_enabled,
    mcp_read_ambient,
    mcp_read_adc_raw,
    mcp_set_type,
    mcp_set_filter,
    mcp_get_filter,
    mcp_set_adc_resolution,
    mcp_get_adc_resolution,
    mcp_set_ambient_resolution,
    mcp_enable,
    mcp_set_alert,
    mcp_get_alert_temp,
    mcp_get_status,
};

const jh_thermocouple_provider_t kMcpProvider = {
    HAL_THERMOCOUPLE_CHIP_MCP9600, "MCP9600", sizeof(hal_mcp9600_t),
    alignof(hal_mcp9600_t),        true,      &kMcpOps};
#endif

#ifdef HAL_ENABLE_MAX6675
hal_max6675_t *as_max(void *context) {
  return static_cast<hal_max6675_t *>(context);
}

hal_status_t max_initialize(void *context,
                            const hal_thermocouple_config_t *config) {
  const hal_thermocouple_spi_cfg_t &bus = config->bus.spi;
  hal_max6675_t *device = new (context) hal_max6675_t();
  const hal_max6675_config_t driver_config = {bus.sclk_pin, bus.cs_pin,
                                              bus.miso_pin};
  if (!hal_max6675_init(device, &driver_config)) {
    hal_serial_println("hal_thermocouple_init: MAX6675 init failed");
    return HAL_EIO;
  }
  return HAL_OK;
}

void max_deinitialize(void *context) { hal_max6675_deinit(as_max(context)); }

hal_status_t max_read(void *context, float *out_c) {
  *out_c = hal_max6675_read_celsius(as_max(context));
  return isnan(*out_c) ? HAL_EIO : HAL_OK;
}

hal_status_t max_get_type(void *, hal_thermocouple_type_t *out_type) {
  *out_type = HAL_THERMOCOUPLE_TYPE_K;
  return HAL_OK;
}

hal_status_t max_is_enabled(void *, bool *out_enabled) {
  *out_enabled = true;
  return HAL_OK;
}

const jh_thermocouple_provider_ops_t kMaxOps = {
    max_initialize, max_deinitialize, max_read, max_get_type, max_is_enabled,
#ifdef HAL_ENABLE_MCP9600
    nullptr,        nullptr,          nullptr,  nullptr,      nullptr,
    nullptr,        nullptr,          nullptr,  nullptr,      nullptr,
    nullptr,        nullptr,
#endif
};

const jh_thermocouple_provider_t kMaxProvider = {
    HAL_THERMOCOUPLE_CHIP_MAX6675, "MAX6675", sizeof(hal_max6675_t),
    alignof(hal_max6675_t),        true,      &kMaxOps};
#endif

} // namespace

const jh_thermocouple_provider_t *
jh_thermocouple_provider_get(hal_thermocouple_chip_t chip) {
  switch (chip) {
#ifdef HAL_ENABLE_MCP9600
  case HAL_THERMOCOUPLE_CHIP_MCP9600:
    return &kMcpProvider;
#endif
#ifdef HAL_ENABLE_MAX6675
  case HAL_THERMOCOUPLE_CHIP_MAX6675:
    return &kMaxProvider;
#endif
  default:
    return nullptr;
  }
}

static_assert(
#ifdef HAL_ENABLE_MCP9600
    sizeof(hal_mcp9600_t) <= JH_THERMOCOUPLE_PROVIDER_CONTEXT_SIZE &&
#endif
#ifdef HAL_ENABLE_MAX6675
        sizeof(hal_max6675_t) <= JH_THERMOCOUPLE_PROVIDER_CONTEXT_SIZE &&
#endif
        true,
    "Increase JH_THERMOCOUPLE_PROVIDER_CONTEXT_SIZE for hardware providers");

#endif /* HAL_ENABLE_THERMOCOUPLE */
#endif /* hardware target */
