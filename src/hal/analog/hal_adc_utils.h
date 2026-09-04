#pragma once

/** @file Portable ADC conversion and repeated-sampling helpers. */

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default ADC resolution used by convenience conversion helpers. */
#ifndef HAL_ADC_UTIL_DEFAULT_BITS
#ifdef HAL_TOOLS_ADC_BITS
#define HAL_ADC_UTIL_DEFAULT_BITS HAL_TOOLS_ADC_BITS
#else
#define HAL_ADC_UTIL_DEFAULT_BITS 12u
#endif
#endif

/** Default number of samples used by hal_adc_read_average(). */
#ifndef HAL_ADC_UTIL_DEFAULT_SAMPLES
#ifdef HAL_TOOLS_NUMSAMPLES
#define HAL_ADC_UTIL_DEFAULT_SAMPLES HAL_TOOLS_NUMSAMPLES
#else
#define HAL_ADC_UTIL_DEFAULT_SAMPLES 4u
#endif
#endif

/** Optional transform applied to every raw ADC sample. */
typedef int (*hal_adc_sample_transform_t)(int sample);

/** Configuration for a repeated ADC read. */
typedef struct {
  uint8_t pin;                          /**< ADC pin or channel. */
  uint16_t sample_count;                /**< Number of samples to average. */
  uint32_t sample_delay_us;             /**< Delay after each sample. */
  bool discard_first;                   /**< Perform one throw-away read. */
  hal_adc_sample_transform_t transform; /**< Optional per-sample transform. */
} hal_adc_average_config_t;

/**
 * @brief Convert a raw ADC sample to the voltage before a resistor divider.
 * @param raw Raw ADC sample.
 * @param reference_voltage ADC reference voltage.
 * @param resolution_bits ADC resolution in the range 1..30.
 * @param high_side_resistance Divider resistance between source and ADC input.
 * @param low_side_resistance Divider resistance between ADC input and ground.
 * @param out_voltage Receives the calculated source voltage.
 * @return HAL_OK, or HAL_EINVAL for invalid parameters.
 */
hal_status_t hal_adc_raw_to_voltage_ex(int raw, float reference_voltage,
                                       uint8_t resolution_bits,
                                       float high_side_resistance,
                                       float low_side_resistance,
                                       float *out_voltage);

/**
 * @brief Apply the established RP2040 12-bit ADC DNL compensation table.
 * @param sample Raw ADC sample.
 * @return Compensated sample.
 */
int hal_adc_compensate_rp2040_12bit(int sample);

/**
 * @brief Read and average ADC samples according to a configuration.
 * @param config Sampling configuration.
 * @param out_average Receives the transformed sample average.
 * @return HAL_OK, or HAL_EINVAL for invalid input or zero samples.
 */
hal_status_t hal_adc_read_average_ex(const hal_adc_average_config_t *config,
                                     float *out_average);

/**
 * @brief Convert a raw sample using the default resolution and 3.3 V.
 * @param raw Raw ADC sample.
 * @param high_side_resistance Divider high-side resistance.
 * @param low_side_resistance Divider low-side resistance.
 * @return Calculated voltage, or NaN for an invalid divider.
 */
float hal_adc_raw_to_voltage(int raw, float high_side_resistance,
                             float low_side_resistance);

/**
 * @brief Read a pin using default sampling and RP2040 compensation settings.
 * @param pin ADC pin or channel.
 * @return Averaged sample, or 0.0f when the read configuration is invalid.
 */
float hal_adc_read_average(uint8_t pin);

#ifdef __cplusplus
}
#endif
