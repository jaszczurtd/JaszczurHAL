#pragma once

/** @file Beta-model NTC conversion helpers. */

#include "hal/analog/hal_adc_utils.h"
#include "hal/core/hal_status.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAL_NTC_DEFAULT_BETA
#ifdef HAL_TOOLS_BCOEFFICIENT
#define HAL_NTC_DEFAULT_BETA HAL_TOOLS_BCOEFFICIENT
#else
#define HAL_NTC_DEFAULT_BETA 3600.0f
#endif
#endif

#ifndef HAL_NTC_DEFAULT_NOMINAL_C
#ifdef HAL_TOOLS_TEMPERATURENOMINAL
#define HAL_NTC_DEFAULT_NOMINAL_C HAL_TOOLS_TEMPERATURENOMINAL
#else
#define HAL_NTC_DEFAULT_NOMINAL_C 21.0f
#endif
#endif

typedef struct {
  float nominal_resistance_ohm;
  float series_resistance_ohm;
  float beta;
  float nominal_temperature_c;
} hal_ntc_beta_config_t;

hal_status_t
hal_ntc_temperature_from_adc_ex(float adc_average, float adc_full_scale,
                                const hal_ntc_beta_config_t *config,
                                float *out_celsius);
hal_status_t hal_ntc_read_temperature_ex(
    const hal_adc_average_config_t *adc_config, float adc_full_scale,
    const hal_ntc_beta_config_t *ntc_config, float *out_celsius);

/** Preserve the established Steinhart calculation and characteristic modes. */
float hal_ntc_steinhart(float divider_ratio, float nominal_resistance,
                        int resistance, bool characteristic);

/** Read an NTC using the configured default ADC and beta-model settings. */
float hal_ntc_read_temperature(uint8_t pin, int nominal_resistance,
                               int series_resistance);

#ifdef __cplusplus
}
#endif
