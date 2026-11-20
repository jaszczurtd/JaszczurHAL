#pragma once

#include "hal/core/hal_layout_check.h"

#include "hal/core/hal_status.h"
#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jh_stm32_pwm_channel_desc {
  uint8_t pin;
  uint8_t timer;
  uint8_t channel;
  uint8_t valid;
  uint8_t holds_timing; /* 1 for a frequency channel, see below */
  uint32_t period_ticks;
} jh_stm32_pwm_channel_desc;

/**
 * @brief Configure the timer of a pin and prepare its channel.
 *
 * A timer whose channel is held by a frequency channel keeps its timing: the
 * call fails when it would change the frequency or the period of such a timer.
 * A prepared channel blocks jh_stm32_pwm_set_frequency() on the other channels
 * of its timer until jh_stm32_pwm_forget_channel(), so an owner that can
 * release its pin forgets the channel then.
 *
 * @return true when the channel is prepared.
 */
bool jh_stm32_pwm_prepare_pin(uint8_t pin, uint32_t frequency_hz,
                              uint32_t period_ticks,
                              jh_stm32_pwm_channel_desc *out);

bool jh_stm32_pwm_pin_supported(uint8_t pin);

uint32_t jh_stm32_pwm_source_clock_hz(uint8_t pin);

/**
 * @brief Prepare a pin for a frequency channel and hold its timer channel.
 *
 * The timer channel stays held until jh_stm32_pwm_forget_channel(). Channels
 * of one timer share its prescaler and period, so a second frequency channel
 * joins a timer only with the same frequency and counter period.
 *
 * @param requested_period_ticks Logical resolution of the channel.
 * @param left_shift Receives the duty scaling shift.
 * @param right_shift Receives the duty scaling shift.
 * @return HAL_OK, HAL_EBUSY when a frequency channel already holds this timer
 *         channel or holds the timer at another frequency or period, HAL_EIO
 *         for an unsupported pin, invalid arguments or an unreachable
 *         frequency. A failed call changes nothing.
 */
hal_status_t jh_stm32_pwm_prepare_frequency_pin(
    uint8_t pin, uint32_t frequency_hz, uint32_t requested_period_ticks,
    jh_stm32_pwm_channel_desc *out, uint8_t *left_shift, uint8_t *right_shift);

/**
 * @brief Retune the timer of a prepared frequency channel.
 *
 * The prescaler, the period and the compare value are preloaded and take
 * effect together at the next timer update, so no period is irregular.
 *
 * @param ch Channel prepared by jh_stm32_pwm_prepare_frequency_pin(); its
 *        period_ticks is updated on success.
 * @param frequency_hz New frequency in hertz.
 * @param requested_period_ticks Logical resolution of the channel.
 * @param logical_compare Duty written last, in [0, requested_period_ticks].
 * @param left_shift Receives the new duty scaling shift.
 * @param right_shift Receives the new duty scaling shift.
 * @return HAL_OK, HAL_EINVAL for invalid arguments or an unreachable
 *         frequency, HAL_EBUSY when another prepared channel uses the timer.
 */
hal_status_t jh_stm32_pwm_set_frequency(jh_stm32_pwm_channel_desc *ch,
                                        uint32_t frequency_hz,
                                        uint32_t requested_period_ticks,
                                        uint32_t logical_compare,
                                        uint8_t *left_shift,
                                        uint8_t *right_shift);
/** Period of the channel timer in nanoseconds, 0 for an invalid channel. */
uint32_t jh_stm32_pwm_period_ns(const jh_stm32_pwm_channel_desc *ch);
/**
 * @brief Forget a prepared channel.
 *
 * The timer channel no longer blocks a frequency change of its timer. A
 * frequency channel also gives up the timing of the timer; any other channel
 * leaves the hold of a frequency channel in place.
 */
void jh_stm32_pwm_forget_channel(jh_stm32_pwm_channel_desc *ch);
void jh_stm32_pwm_write_compare(const jh_stm32_pwm_channel_desc *ch,
                                uint32_t compare);

void jh_stm32_pwm_start_output(const jh_stm32_pwm_channel_desc *ch);

void jh_stm32_pwm_release_output(const jh_stm32_pwm_channel_desc *ch);

uint32_t jh_stm32_pwm_compare_address(const jh_stm32_pwm_channel_desc *ch);

uint32_t jh_stm32_pwm_timer_dma_request(const jh_stm32_pwm_channel_desc *ch);

void jh_stm32_pwm_set_update_dma_request(const jh_stm32_pwm_channel_desc *ch,
                                         bool enabled);

#ifdef __cplusplus
}
#endif

#endif // HAL_TARGET_IS_STM32G474
