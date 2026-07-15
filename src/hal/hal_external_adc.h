#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_EXTERNAL_ADC

/**
 * @file hal_external_adc.h
 * @brief Hardware abstraction for the ADS1115 external ADC (I2C).
 *
 * Wraps the shared Arduino-free ADS1X15/ADS1115 driver so that project code is
 * decoupled from chip-level register details and can be replaced by a mock
 * implementation for unit testing.
 *
 * Thread-safety: RP2040/STM32 reads are serialized by an internal ADC mutex,
 * while individual bus transactions are protected by the I2C HAL. Callers do
 * not need to take additional locks.
 */

#include "hal_status.h"

#include <stdint.h>

/**
 * @brief Initialise the ADS1115 and associate it with an I2C address.
 * @param address   7-bit I2C address of the ADS1115.
 * @param adc_range LSB size in millivolts (e.g. 0.1875 for ±6.144 V
 * full-scale). Stored internally and used by hal_ext_adc_read_scaled().
 */
hal_status_t hal_ext_adc_init(uint8_t address, float adc_range);

/**
 * @brief Initialise ADS1115 on selected I2C controller.
 * @param i2c_bus   I2C controller index (0 = default, 1 = second controller).
 * @param address   7-bit I2C address of the ADS1115.
 * @param adc_range LSB size in millivolts.
 */
hal_status_t hal_ext_adc_init_bus(uint8_t i2c_bus, uint8_t address,
                                  float adc_range);

/**
 * @brief Status-returning raw read variant.
 *
 * @param channel ADS1115 input channel (0-3).
 * @param out     Receives the raw signed 16-bit ADC result.
 * @return HAL_OK; HAL_EINVAL for an invalid channel/output pointer;
 *         HAL_EUNINIT before successful init; HAL_ETIMEOUT/HAL_EBUS/HAL_EIO
 *         for ADS1115/backend read failures.
 */
hal_status_t hal_ext_adc_read_ex(uint8_t channel, int16_t *out);

/**
 * @brief Read a raw 16-bit value from the given single-ended channel.
 *
 * Gain is set to 0 (±6.144 V full-scale) before each conversion.
 * The call blocks until the result is available.
 *
 * @param channel ADS1115 input channel (0-3).
 * @return Raw signed 16-bit ADC result.
 * @note Returns 0 on invalid channel or ADS1X15 communication/timeout errors.
 */
int16_t hal_ext_adc_read(uint8_t channel);

/**
 * @brief Status-returning scaled read variant.
 *
 * @param channel ADS1115 input channel (0-3).
 * @param out     Receives the scaled value.
 * @return HAL_OK or the same error statuses as hal_ext_adc_read_ex().
 */
hal_status_t hal_ext_adc_read_scaled_ex(uint8_t channel, float *out);

/**
 * @brief Read a channel and apply the stored adc_range scale factor.
 *
 * Returns (raw * adc_range) / 1000.0f - the scaled floating-point value
 * without any further conversion. Use this when the caller still needs to
 * apply project-specific corrections (e.g. voltage divider, Steinhart-Hart).
 *
 * @param channel ADS1115 input channel (0-3).
 * @return Scaled value.
 */
float hal_ext_adc_read_scaled(uint8_t channel);

#endif /* HAL_ENABLE_EXTERNAL_ADC */
#ifdef __cplusplus
}
#endif
