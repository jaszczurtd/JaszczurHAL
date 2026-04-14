#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifndef HAL_DISABLE_PWM_FREQ

/**
 * @file hal_pwm_freq.h
 * @brief Frequency-controlled PWM channels (pico SDK hardware PWM).
 *
 * Use this instead of analogWrite() when you need precise control of PWM
 * frequency and resolution. Each channel wraps one pico SDK PWM slice.
 *
 * Thread safety: hal_pwm_freq_write() is protected by an internal mutex.
 * hal_pwm_freq_create() and hal_pwm_freq_destroy() must be called from
 * one core only.
 */

#include <stdint.h>

/**
 * @brief Opaque handle for a frequency-controlled PWM channel.
 *
 * Wraps pico SDK PWM with custom frequency and resolution.
 * Obtain via hal_pwm_freq_create(); release via hal_pwm_freq_destroy().
 */
typedef struct hal_pwm_freq_channel_impl_s hal_pwm_freq_channel_impl_t;
typedef hal_pwm_freq_channel_impl_t *hal_pwm_freq_channel_t;

/**
 * @brief Create a PWM channel with a specific frequency and resolution.
 * @param pin          GPIO pin to output PWM on.
 * @param frequency_hz Desired PWM frequency in Hz.
 * @param resolution   Wrap value (e.g. 2047 for 11-bit).
 * @return Opaque handle, or NULL on failure / pool exhaustion.
 */
hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin,
                                           uint32_t frequency_hz,
                                           uint32_t resolution);

/**
 * @brief Write a duty-cycle value to the channel.
 * @param ch    Handle from hal_pwm_freq_create().
 * @param value Value in [0, resolution].
 */
void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value);

/**
 * @brief Release the PWM channel resources.
 * @param ch Handle to release. Must not be used after this call.
 */
void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch);


#endif /* HAL_DISABLE_PWM_FREQ */
#ifdef __cplusplus
}
#endif
