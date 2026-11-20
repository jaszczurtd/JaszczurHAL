#pragma once

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct jh_esp32_ledc_channel_s;
typedef struct jh_esp32_ledc_channel_s jh_esp32_ledc_channel_t;

jh_esp32_ledc_channel_t *
jh_esp32_ledc_acquire(uint8_t pin, uint32_t frequency_hz, uint32_t logical_max);
bool jh_esp32_ledc_write(jh_esp32_ledc_channel_t *channel,
                         uint32_t logical_value);

/**
 * @brief Update the duty cycle of a configured channel from an ISR.
 * @param channel Channel already configured by jh_esp32_ledc_write().
 * @param logical_value Duty in the channel's logical range, clamped to its
 *        maximum.
 * @return true when the ESP-IDF duty update succeeded.
 *
 * Takes no mutex and never reconfigures the channel, so it stays usable from
 * an interrupt. The build must place the LEDC control functions in IRAM
 * (CONFIG_LEDC_CTRL_FUNC_IN_IRAM); the sample-paced audio backend selects it.
 * Unlike jh_esp32_ledc_write(), a full-scale value keeps the waveform running
 * at the highest duty the timer resolution expresses instead of switching the
 * output to a static level.
 */
bool jh_esp32_ledc_write_from_isr(jh_esp32_ledc_channel_t *channel,
                                  uint32_t logical_value);
void jh_esp32_ledc_stop(jh_esp32_ledc_channel_t *channel);
bool jh_esp32_ledc_release(jh_esp32_ledc_channel_t *channel);
/**
 * @brief Retune the LEDC timer of a channel.
 * @param channel Channel from jh_esp32_ledc_acquire().
 * @param frequency_hz New frequency in hertz.
 * @return HAL_OK, HAL_EINVAL for an invalid channel or a frequency the timer
 *         cannot reach with the channel duty resolution, HAL_EBUSY when other
 *         channels share the timer, HAL_ENOMEM without a mutex, HAL_EIO when
 *         ESP-IDF rejects the change. The duty value is kept by the driver.
 */
hal_status_t jh_esp32_ledc_set_frequency(jh_esp32_ledc_channel_t *channel,
                                         uint32_t frequency_hz);
/** Period of the channel timer in nanoseconds, 0 for an invalid channel. */
uint32_t jh_esp32_ledc_period_ns(jh_esp32_ledc_channel_t *channel);
uint32_t jh_esp32_ledc_source_clock_hz(void);

#ifdef __cplusplus
}
#endif
