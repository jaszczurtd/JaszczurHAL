#pragma once

/**
 * @file hal_adc.h
 * @brief Hardware abstraction for analog-to-digital conversion.
 *
 * Thread-safe and multicore-safe on RP2040/STM32G474 runtime paths. An
 * internal mutex serializes access to the shared ADC hardware/backend state.
 */

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
 * @brief Read the analog value on the given pin.
 * @param pin Analog input pin number.
 * @return Raw ADC value in the range [0, 2^bits - 1].
 */
int hal_adc_read(uint8_t pin);

#ifdef __cplusplus
}
#endif
