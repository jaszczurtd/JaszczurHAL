#include "hal/temperature/hal_ntc.h"

#include <math.h>

hal_status_t
hal_ntc_temperature_from_adc_ex(float adc_average, float adc_full_scale,
                                const hal_ntc_beta_config_t *config,
                                float *out_celsius) {
  if (config == nullptr || out_celsius == nullptr || adc_full_scale <= 1.0f ||
      config->nominal_resistance_ohm <= 0.0f ||
      config->series_resistance_ohm <= 0.0f || config->beta <= 0.0f ||
      adc_average <= 0.0f || adc_average >= adc_full_scale) {
    return HAL_EINVAL;
  }

  const float divider_ratio = adc_full_scale / adc_average - 1.0f;
  const float resistance = config->series_resistance_ohm / divider_ratio;
  const float nominal_kelvin = config->nominal_temperature_c + 273.15f;
  const float inverse_kelvin =
      logf(resistance / config->nominal_resistance_ohm) / config->beta +
      1.0f / nominal_kelvin;
  if (!isfinite(inverse_kelvin) || inverse_kelvin == 0.0f) {
    return HAL_EINVAL;
  }
  *out_celsius = 1.0f / inverse_kelvin - 273.15f;
  return isfinite(*out_celsius) ? HAL_OK : HAL_EINVAL;
}

hal_status_t hal_ntc_read_temperature_ex(
    const hal_adc_average_config_t *adc_config, float adc_full_scale,
    const hal_ntc_beta_config_t *ntc_config, float *out_celsius) {
  float average = 0.0f;
  const hal_status_t status = hal_adc_read_average_ex(adc_config, &average);
  return status == HAL_OK
             ? hal_ntc_temperature_from_adc_ex(average, adc_full_scale,
                                               ntc_config, out_celsius)
             : status;
}

float hal_ntc_steinhart(float divider_ratio, float nominal_resistance,
                        int resistance, bool characteristic) {
  float value = (float)resistance / divider_ratio;
  float result = value / nominal_resistance;
  result = logf(result);
  result /= HAL_NTC_DEFAULT_BETA;
  const float inverse_nominal = 1.0f / (HAL_NTC_DEFAULT_NOMINAL_C + 273.15f);
  if (characteristic) {
    result += inverse_nominal;
    result = 1.0f / result;
    result -= 273.15f;
  } else {
    result -= inverse_nominal;
    result = 1.0f / result;
    result += 273.15f;
    result = -result;
  }
  return result;
}

float hal_ntc_read_temperature(uint8_t pin, int nominal_resistance,
                               int series_resistance) {
  const hal_adc_average_config_t adc_config = {
      pin,  (uint16_t)HAL_ADC_UTIL_DEFAULT_SAMPLES,
      10u, // delay 10us between samples
      true, hal_adc_compensate_rp2040_12bit,
  };
  const hal_ntc_beta_config_t ntc_config = {
      (float)nominal_resistance,
      (float)series_resistance,
      HAL_NTC_DEFAULT_BETA,
      HAL_NTC_DEFAULT_NOMINAL_C,
  };
  float temperature = NAN;
  const float full_scale =
      (float)((UINT32_C(1) << HAL_ADC_UTIL_DEFAULT_BITS) - 1u);
  float average = 0.0f;
  if (hal_adc_read_average_ex(&adc_config, &average) != HAL_OK) {
    return temperature;
  }
  if (average >= full_scale) {
    average = full_scale - 1.0f;
  }
  if (average <= 0.0f) {
    average = 1.0f;
  }
  if (hal_ntc_temperature_from_adc_ex(average, full_scale, &ntc_config,
                                      &temperature) != HAL_OK) {
    return NAN;
  }
  return temperature;
}
