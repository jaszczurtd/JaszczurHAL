#pragma once

/** @file Beta-model NTC conversion helpers. */

#include "hal/analog/hal_adc_utils.h"
#include "hal/core/hal_status.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default NTC beta coefficient used by convenience helpers. */
#ifndef HAL_NTC_DEFAULT_BETA
#ifdef HAL_TOOLS_BCOEFFICIENT
#define HAL_NTC_DEFAULT_BETA HAL_TOOLS_BCOEFFICIENT
#else
#define HAL_NTC_DEFAULT_BETA 3600.0f
#endif
#endif

/** Default nominal NTC temperature in degrees Celsius. */
#ifndef HAL_NTC_DEFAULT_NOMINAL_C
#ifdef HAL_TOOLS_TEMPERATURENOMINAL
#define HAL_NTC_DEFAULT_NOMINAL_C HAL_TOOLS_TEMPERATURENOMINAL
#else
#define HAL_NTC_DEFAULT_NOMINAL_C 21.0f
#endif
#endif

/** Parameters of the NTC beta model and its voltage divider. */
typedef struct {
  float nominal_resistance_ohm; /**< Resistance at the nominal temperature. */
  float series_resistance_ohm;  /**< Fixed divider resistance. */
  float beta;                   /**< Thermistor beta coefficient. */
  float nominal_temperature_c;  /**< Nominal temperature in Celsius. */
} hal_ntc_beta_config_t;

/**
 * @brief Convert an averaged divider sample to temperature using a beta model.
 * @param adc_average Averaged ADC value strictly between zero and full scale.
 * @param adc_full_scale ADC full-scale value.
 * @param config NTC and divider parameters.
 * @param out_celsius Receives the calculated temperature in Celsius.
 * @return HAL_OK, or HAL_EINVAL for invalid parameters or a non-finite result.
 */
hal_status_t
hal_ntc_temperature_from_adc_ex(float adc_average, float adc_full_scale,
                                const hal_ntc_beta_config_t *config,
                                float *out_celsius);

/**
 * @brief Read averaged ADC data and convert it to an NTC temperature.
 * @param adc_config ADC sampling configuration.
 * @param adc_full_scale ADC full-scale value.
 * @param ntc_config NTC and divider parameters.
 * @param out_celsius Receives the calculated temperature in Celsius.
 * @return HAL_OK, an ADC read error, or HAL_EINVAL for invalid conversion
 * parameters.
 */
hal_status_t hal_ntc_read_temperature_ex(
    const hal_adc_average_config_t *adc_config, float adc_full_scale,
    const hal_ntc_beta_config_t *ntc_config, float *out_celsius);

/**
 * @brief Run the established thermistor conversion with selectable direction.
 * @param divider_ratio Ratio used to derive resistance from @p resistance.
 * @param nominal_resistance Thermistor resistance at the nominal temperature.
 * @param resistance Divider resistance value used by the calculation.
 * @param characteristic true for the usual NTC equation; false for the
 * preserved inverse characteristic calculation.
 * @return Calculated temperature in Celsius.
 */
float hal_ntc_steinhart(float divider_ratio, float nominal_resistance,
                        int resistance, bool characteristic);

/**
 * @brief Read an NTC using default ADC sampling and beta-model settings.
 * @param pin ADC pin or channel.
 * @param nominal_resistance Thermistor resistance at nominal temperature.
 * @param series_resistance Fixed divider resistance.
 * @return Temperature in Celsius, or NaN when conversion fails.
 */
float hal_ntc_read_temperature(uint8_t pin, int nominal_resistance,
                               int series_resistance);

#ifdef __cplusplus
}
#endif
