#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifndef HAL_DISABLE_EXTERNAL_ADC

/**
 * @file hal_external_adc.h
 * @brief Hardware abstraction for the ADS1115 external ADC (I2C).
 *
 * Wraps the bundled ADS1X15 driver so that project code is decoupled from it
 * and can be replaced by a mock implementation for unit testing.
 *
 * Thread-safety: every read is protected by the I2C HAL mutex via
 * hal_i2c_lock() / hal_i2c_unlock(), so callers do not need to take any
 * additional locks.
 */

#include <stdint.h>

/**
 * @brief Initialise the ADS1115 and associate it with an I2C address.
 * @param address   7-bit I2C address of the ADS1115.
 * @param adc_range LSB size in millivolts (e.g. 0.1875 for ±6.144 V full-scale).
 *                  Stored internally and used by hal_ext_adc_read_scaled().
 */
void hal_ext_adc_init(uint8_t address, float adc_range);

/**
 * @brief Initialise ADS1115 on selected I2C controller.
 * @param i2c_bus   I2C controller index (0 = Wire, 1 = Wire1).
 * @param address   7-bit I2C address of the ADS1115.
 * @param adc_range LSB size in millivolts.
 */
void hal_ext_adc_init_bus(uint8_t i2c_bus, uint8_t address, float adc_range);

/**
 * @brief Read a raw 16-bit value from the given single-ended channel.
 *
 * Gain is set to 0 (±6.144 V full-scale) before each conversion.
 * The call blocks until the result is available.
 *
 * @param channel ADS1115 input channel (0–3).
 * @return Raw signed 16-bit ADC result.
 * @note Returns 0 on invalid channel or ADS1X15 communication/timeout errors.
 */
int16_t hal_ext_adc_read(uint8_t channel);

/**
 * @brief Read a channel and apply the stored adc_range scale factor.
 *
 * Returns (raw * adc_range) / 1000.0f - the scaled floating-point value
 * without any further conversion. Use this when the caller still needs to
 * apply project-specific corrections (e.g. voltage divider, Steinhart-Hart).
 *
 * @param channel ADS1115 input channel (0–3).
 * @return Scaled value.
 */
float hal_ext_adc_read_scaled(uint8_t channel);


#endif /* HAL_DISABLE_EXTERNAL_ADC */
#ifdef __cplusplus
}
#endif
