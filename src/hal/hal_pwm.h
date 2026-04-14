#pragma once

/**
 * @file hal_pwm.h
 * @brief Hardware abstraction for basic PWM output (analogWrite style).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the global PWM resolution in bits.
 * @param bits Resolution (e.g. 8, 10, 12).
 */
void hal_pwm_set_resolution(uint8_t bits);

/**
 * @brief Write a PWM duty-cycle value to a pin.
 * @param pin   PWM-capable output pin.
 * @param value Duty cycle in [0, 2^bits - 1].
 */
void hal_pwm_write(uint8_t pin, uint32_t value);

#ifdef __cplusplus
}
#endif
