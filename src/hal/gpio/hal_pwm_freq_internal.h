#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_PWM_FREQ

#include "hal/core/hal_status.h"
#include "hal/gpio/hal_pwm_freq.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a frequency-controlled PWM channel without asserting.
 * @param pin GPIO pin used for PWM output.
 * @param frequency_hz Requested output frequency in hertz.
 * @param resolution Requested PWM wrap value.
 * @param out_channel Receives the channel on success and NULL on failure.
 * @return HAL_OK on success, HAL_EINVAL for invalid arguments, HAL_ENOMEM
 *         when synchronization or pool storage is unavailable, or HAL_EIO
 *         when the target cannot configure the requested output.
 *
 * This internal entry point lets status-returning facades preserve allocation
 * failures. Public compatibility code continues to use hal_pwm_freq_create().
 */
hal_status_t jh_hal_pwm_freq_try_create(uint8_t pin, uint32_t frequency_hz,
                                        uint32_t resolution,
                                        hal_pwm_freq_channel_t *out_channel);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HAL_ENABLE_PWM_FREQ */
