#pragma once

/**
 * @file hal_adc.h
 * @brief Hardware abstraction for analog-to-digital conversion.
 *
 * Thread-safe and multicore-safe on RP2040, STM32G474, and ESP32-S3 runtime
 * paths. An internal mutex serializes access to the shared ADC
 * hardware/backend state.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the ADC resolution in bits (e.g. 10, 12).
 * @param bits Number of ADC resolution bits.
 */
void hal_adc_set_resolution(uint8_t bits);

/**
 * @brief Return true when the ADC backend supports conversions on @p pin.
 * @param pin GPIO pin intended for analog input.
 * @return true when @p pin is a supported ADC input; false otherwise.
 */
bool hal_adc_is_pin_supported(uint8_t pin);

/**
 * @brief Read the analog value on the given pin.
 * @param pin Analog input pin number.
 * @return Raw ADC value in the range [0, 2^bits - 1], or a negative value when
 *         the input cannot be read now: on RP and STM32G474 a running DMA scan
 *         owns the shared converter, and a pin the scan does not carry has no
 *         sample to offer. A scanned pin returns its newest scanned sample.
 *         Callers that average or convert must check for the negative result;
 *         hal_adc_read_average_ex() reports it as HAL_ESTATE.
 */
int hal_adc_read(uint8_t pin);

#ifdef __cplusplus
}
#endif
