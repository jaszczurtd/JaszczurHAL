#pragma once

/** @file Portable ADC conversion and repeated-sampling helpers. */

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAL_ADC_UTIL_DEFAULT_BITS
#ifdef HAL_TOOLS_ADC_BITS
#define HAL_ADC_UTIL_DEFAULT_BITS HAL_TOOLS_ADC_BITS
#else
#define HAL_ADC_UTIL_DEFAULT_BITS 12u
#endif
#endif

#ifndef HAL_ADC_UTIL_DEFAULT_SAMPLES
#ifdef HAL_TOOLS_NUMSAMPLES
#define HAL_ADC_UTIL_DEFAULT_SAMPLES HAL_TOOLS_NUMSAMPLES
#else
#define HAL_ADC_UTIL_DEFAULT_SAMPLES 4u
#endif
#endif

typedef int (*hal_adc_sample_transform_t)(int sample);

typedef struct {
  uint8_t pin;
  uint16_t sample_count;
  uint32_t sample_delay_us;
  bool discard_first;
  hal_adc_sample_transform_t transform;
} hal_adc_average_config_t;

hal_status_t hal_adc_raw_to_voltage_ex(int raw, float reference_voltage,
                                       uint8_t resolution_bits,
                                       float high_side_resistance,
                                       float low_side_resistance,
                                       float *out_voltage);
int hal_adc_compensate_rp2040_12bit(int sample);
hal_status_t hal_adc_read_average_ex(const hal_adc_average_config_t *config,
                                     float *out_average);

/** Convert a raw sample using the configured default resolution and 3.3 V. */
float hal_adc_raw_to_voltage(int raw, float high_side_resistance,
                             float low_side_resistance);

/** Read a pin using the configured default sampling and compensation. */
float hal_adc_read_average(uint8_t pin);

#ifdef __cplusplus
}
#endif
