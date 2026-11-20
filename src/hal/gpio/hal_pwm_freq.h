#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_PWM_FREQ

/**
 * @file hal_pwm_freq.h
 * @brief Frequency-controlled PWM channels (pico SDK hardware PWM).
 *
 * Use this instead of analogWrite() when you need precise control of PWM
 * frequency and resolution. Each channel wraps one pico SDK PWM slice.
 *
 * Thread safety: hal_pwm_freq_create(), hal_pwm_freq_write(),
 * hal_pwm_freq_set_frequency(), hal_pwm_freq_get_period_ns(),
 * hal_pwm_freq_stop() and hal_pwm_freq_destroy() are protected by an internal
 * mutex. Callers still own channel handle lifetime and must not use a handle
 * after hal_pwm_freq_destroy().
 */

#include "hal/core/hal_status.h"

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
 *
 * Channels that count on one timer share its frequency: the two outputs of an
 * RP PWM slice, the channels of an STM32 timer. Creation fails when the output
 * already has a channel, and when its timer runs another channel at a
 * different frequency. A refused request leaves the running channel as it
 * was.
 *
 * @param pin          GPIO pin to output PWM on.
 * @param frequency_hz Desired PWM frequency in Hz.
 * @param resolution   Wrap value (e.g. 2047 for 11-bit).
 * @return Opaque handle, or NULL for invalid arguments, an exhausted pool, an
 *         output or a timer that is taken as described above, or a
 *         configuration the target cannot provide.
 */
hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin, uint32_t frequency_hz,
                                           uint32_t resolution);

/**
 * @brief Return the source clock used to derive PWM frequency on a pin.
 * @param pin GPIO pin intended for frequency-controlled PWM output.
 * @return Source clock in Hz, or 0 when the pin/backend cannot provide one.
 */
uint32_t hal_pwm_freq_source_clock_hz(uint8_t pin);

/**
 * @brief Write a duty-cycle value to the channel.
 * @param ch    Handle from hal_pwm_freq_create().
 * @param value Value in [0, resolution].
 */
void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value);

/**
 * @brief Change the frequency of an existing channel.
 *
 * The duty scale stays [0, resolution] and the duty ratio written last is
 * kept. A running channel keeps running: the output never leaves the PWM
 * waveform. A channel that was not started yet, or was stopped, only takes
 * the new configuration.
 *
 * The new frequency applies from the next PWM period when the target can
 * change the period alone. When it has to change the clock divider as well,
 * the period in progress may have an intermediate length and duty ratio; the
 * following period is regular. hal_pwm_freq_get_period_ns() returns the period
 * that was actually programmed.
 *
 * @param ch           Handle from hal_pwm_freq_create().
 * @param frequency_hz New PWM frequency in Hz, greater than zero.
 * @return HAL_OK on success (also when the frequency is unchanged),
 *         HAL_EINVAL for a NULL or released handle, a zero frequency or a
 *         frequency the timer cannot reach with the channel resolution,
 *         HAL_EBUSY when another active channel shares the same timer,
 *         HAL_ENOMEM when synchronization is unavailable, HAL_EIO when the
 *         target driver rejects the change. A failed call leaves the channel
 *         unchanged.
 */
hal_status_t hal_pwm_freq_set_frequency(hal_pwm_freq_channel_t ch,
                                        uint32_t frequency_hz);

/**
 * @brief Return the PWM period that the channel timer is programmed for.
 *
 * The value includes the rounding of the clock divider and of the counter
 * period, so it can differ slightly from 1/frequency_hz.
 *
 * @param ch Handle from hal_pwm_freq_create().
 * @return Period in nanoseconds, or 0 for a NULL or released handle.
 */
uint32_t hal_pwm_freq_get_period_ns(hal_pwm_freq_channel_t ch);

/**
 * @brief Stop PWM output for a channel without releasing its configuration.
 *
 * A later hal_pwm_freq_write() restarts output with the supplied duty value.
 * Other channels on the same timer keep running.
 *
 * @param ch Handle from hal_pwm_freq_create().
 */
void hal_pwm_freq_stop(hal_pwm_freq_channel_t ch);

/**
 * @brief Release the PWM channel resources.
 * @param ch Handle to release. Must not be used after this call.
 */
void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch);

#endif /* HAL_ENABLE_PWM_FREQ */
#ifdef __cplusplus
}
#endif
