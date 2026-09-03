#include "hal/analog/hal_adc_utils.h"

#include "hal/analog/hal_adc.h"
#include "hal/system/hal_system.h"

#include <math.h>

hal_status_t hal_adc_raw_to_voltage_ex(int raw, float reference_voltage,
                                       uint8_t resolution_bits,
                                       float high_side_resistance,
                                       float low_side_resistance,
                                       float *out_voltage) {
  if (out_voltage == nullptr || resolution_bits == 0u ||
      resolution_bits > 30u || reference_voltage < 0.0f ||
      high_side_resistance < 0.0f || low_side_resistance <= 0.0f) {
    return HAL_EINVAL;
  }
  const uint32_t levels = UINT32_C(1) << resolution_bits;
  const float divider_scale =
      (high_side_resistance + low_side_resistance) / low_side_resistance;
  *out_voltage =
      (float)raw * (reference_voltage / (float)levels) * divider_scale;
  return HAL_OK;
}

int hal_adc_compensate_rp2040_12bit(int sample) {
  if (sample > 3584) {
    return sample + 32;
  }
  if (sample == 3583) {
    return sample + 29;
  }
  if (sample == 3582) {
    return sample + 27;
  }
  if (sample > 2560) {
    return sample + 24;
  }
  if (sample == 2559) {
    return sample + 21;
  }
  if (sample == 2558) {
    return sample + 19;
  }
  if (sample > 1536) {
    return sample + 16;
  }
  if (sample == 1535) {
    return sample + 13;
  }
  if (sample == 1534) {
    return sample + 11;
  }
  if (sample > 512) {
    return sample + 8;
  }
  if (sample == 511) {
    return sample + 5;
  }
  if (sample == 510) {
    return sample + 3;
  }
  return sample;
}

hal_status_t hal_adc_read_average_ex(const hal_adc_average_config_t *config,
                                     float *out_average) {
  if (config == nullptr || out_average == nullptr ||
      config->sample_count == 0u) {
    return HAL_EINVAL;
  }
  if (config->discard_first) {
    (void)hal_adc_read(config->pin);
  }

  float sum = 0.0f;
  for (uint16_t i = 0u; i < config->sample_count; ++i) {
    int sample = hal_adc_read(config->pin);
    if (config->transform != nullptr) {
      sample = config->transform(sample);
    }
    sum += (float)sample;
    if (config->sample_delay_us != 0u) {
      hal_delay_us(config->sample_delay_us);
    }
  }
  *out_average = sum / (float)config->sample_count;
  return HAL_OK;
}

float hal_adc_raw_to_voltage(int raw, float high_side_resistance,
                             float low_side_resistance) {
  float voltage = NAN;
  (void)hal_adc_raw_to_voltage_ex(raw, 3.3f, HAL_ADC_UTIL_DEFAULT_BITS,
                                  high_side_resistance, low_side_resistance,
                                  &voltage);
  return voltage;
}

float hal_adc_read_average(uint8_t pin) {
  const hal_adc_average_config_t config = {
      pin,  (uint16_t)HAL_ADC_UTIL_DEFAULT_SAMPLES, 10u,
      true, hal_adc_compensate_rp2040_12bit,
  };
  float average = 0.0f;
  (void)hal_adc_read_average_ex(&config, &average);
  return average;
}
