#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_pwm.h"
#include "hal_pwm_stm32g474.h"

static uint8_t s_resolution = 8u;

static constexpr uint32_t kDefaultPwmFrequencyHz = 1000u;

static uint32_t period_ticks_for_resolution(uint8_t bits) {
  if (bits == 0u) {
    bits = 1u;
  } else if (bits > 16u) {
    bits = 16u;
  }
  return 1u << bits;
}

void hal_pwm_set_resolution(uint8_t bits) { s_resolution = bits; }

void hal_pwm_write(uint8_t pin, uint32_t value) {
  const uint32_t period_ticks = period_ticks_for_resolution(s_resolution);
  jh_stm32_pwm_channel_desc ch = {};
  if (!jh_stm32_pwm_prepare_pin(pin, kDefaultPwmFrequencyHz, period_ticks,
                                &ch)) {
    return;
  }

  const uint32_t max_input = period_ticks - 1u;
  const uint32_t compare = (value >= max_input) ? period_ticks : value;
  jh_stm32_pwm_write_compare(&ch, compare);
  jh_stm32_pwm_start_output(&ch);
}

#endif // HAL_TARGET_IS_STM32G474
