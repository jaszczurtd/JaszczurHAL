#pragma once

#include "hal/temperature/hal_thermocouple.h"

#ifdef HAL_ENABLE_THERMOCOUPLE

#include "hal/core/hal_status.h"

#include <stddef.h>
#include <stdint.h>

/* Static facade storage keeps provider allocation deterministic on targets. */
#define JH_THERMOCOUPLE_PROVIDER_CONTEXT_SIZE 64u

typedef struct {
  hal_status_t (*initialize)(void *context,
                             const hal_thermocouple_config_t *config);
  void (*deinitialize)(void *context);
  hal_status_t (*read)(void *context, float *out_c);
  hal_status_t (*get_type)(void *context, hal_thermocouple_type_t *out_type);
  hal_status_t (*is_enabled)(void *context, bool *out_enabled);
#ifdef HAL_ENABLE_MCP9600
  hal_status_t (*read_ambient)(void *context, float *out_c);
  hal_status_t (*read_adc_raw)(void *context, int32_t *out_raw);
  hal_status_t (*set_type)(void *context, hal_thermocouple_type_t type);
  hal_status_t (*set_filter)(void *context, uint8_t coeff);
  hal_status_t (*get_filter)(void *context, uint8_t *out_coeff);
  hal_status_t (*set_adc_resolution)(void *context,
                                     hal_thermocouple_adc_res_t resolution);
  hal_status_t (*get_adc_resolution)(
      void *context, hal_thermocouple_adc_res_t *out_resolution);
  hal_status_t (*set_ambient_resolution)(
      void *context, hal_thermocouple_ambient_res_t resolution);
  hal_status_t (*enable)(void *context, bool enabled);
  hal_status_t (*set_alert)(void *context, uint8_t alert_num, bool enabled,
                            const hal_thermocouple_alert_cfg_t *config);
  hal_status_t (*get_alert_temp)(void *context, uint8_t alert_num,
                                 float *out_c);
  hal_status_t (*get_status)(void *context, uint8_t *out_status);
#endif
} jh_thermocouple_provider_ops_t;

typedef struct {
  hal_thermocouple_chip_t chip;
  const char *name;
  size_t context_size;
  size_t context_alignment;
  bool assert_on_pool_exhaustion;
  const jh_thermocouple_provider_ops_t *ops;
} jh_thermocouple_provider_t;

/** Return the provider selected by the active target for a public chip type. */
const jh_thermocouple_provider_t *
jh_thermocouple_provider_get(hal_thermocouple_chip_t chip);

typedef void (*jh_thermocouple_context_visitor_t)(void *provider_context,
                                                  void *visitor_context);

/** Visit a live provider context while the shared handle mutex is held. */
hal_status_t jh_thermocouple_provider_visit_context(
    hal_thermocouple_t handle, jh_thermocouple_context_visitor_t visitor,
    void *visitor_context);

#endif /* HAL_ENABLE_THERMOCOUPLE */
