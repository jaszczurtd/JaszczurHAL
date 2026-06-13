#pragma once

#include "../../hal_target.h"

#if HAL_TARGET_IS_STM32G474

#include <stdint.h>

struct jh_stm32_pwm_channel_desc {
  uint8_t pin;
  uint8_t timer;
  uint8_t channel;
  uint8_t valid;
  uint32_t period_ticks;
};

bool jh_stm32_pwm_prepare_pin(uint8_t pin, uint32_t frequency_hz,
                              uint32_t period_ticks,
                              jh_stm32_pwm_channel_desc *out);

bool jh_stm32_pwm_prepare_frequency_pin(uint8_t pin, uint32_t frequency_hz,
                                        uint32_t requested_period_ticks,
                                        jh_stm32_pwm_channel_desc *out,
                                        uint8_t *left_shift,
                                        uint8_t *right_shift);

void jh_stm32_pwm_write_compare(const jh_stm32_pwm_channel_desc *ch,
                                uint32_t compare);

void jh_stm32_pwm_start_output(const jh_stm32_pwm_channel_desc *ch);

void jh_stm32_pwm_release_output(const jh_stm32_pwm_channel_desc *ch);

#endif // HAL_TARGET_IS_STM32G474
