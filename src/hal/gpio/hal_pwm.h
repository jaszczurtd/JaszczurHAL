#pragma once

/**
 * @file hal_pwm.h
 * @brief Hardware abstraction for simple PWM output (analogWrite style).
 *
 * This API is intentionally minimal and does not provide an independent
 * frequency/channel allocation contract. Use hal_pwm_freq.h when the
 * application needs explicit frequency, resolution and channel lifetime.
 */

#include "hal/core/hal_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the simple-PWM duty resolution in bits.
 *
 * Supported range is 1..16 bits. Invalid values trigger HAL_ASSERT in checked
 * builds and are clamped to the nearest supported value.
 *
 * This is a backend-global setting for hal_pwm_write(); call it during init,
 * not concurrently with writes.
 *
 * @param bits Resolution (e.g. 8, 10, 12).
 */
void hal_pwm_set_resolution(uint8_t bits);

/**
 * @brief Return true when the simple-PWM backend supports @p pin.
 */
bool hal_pwm_is_pin_supported(uint8_t pin);

/**
 * @brief Write a PWM duty-cycle value to a pin.
 *
 * Values above the current resolution maximum are clamped. Invalid or
 * unsupported pins trigger HAL_ASSERT in checked builds and are ignored.
 *
 * @param pin   PWM-capable output pin.
 * @param value Duty cycle in [0, 2^bits - 1].
 */
void hal_pwm_write(uint8_t pin, uint32_t value);

#ifdef __cplusplus
}
#endif
