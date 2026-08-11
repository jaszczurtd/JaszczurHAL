#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/gpio/hal_pwm.h"
#include "hal_pwm_stm32g474.h"

static uint8_t s_resolution = 8u;

static constexpr uint32_t kDefaultPwmFrequencyHz = 1000u;

static uint8_t clamp_resolution(uint8_t bits) {
  if (bits < 1u) {
    HAL_ASSERT(false, "hal_pwm_set_resolution: resolution is below 1 bit");
    return 1u;
  }
  if (bits > 16u) {
    HAL_ASSERT(false, "hal_pwm_set_resolution: resolution is above 16 bits");
    return 16u;
  }
  return bits;
}

static uint32_t period_ticks_for_resolution(uint8_t bits) {
  bits = clamp_resolution(bits);
  return 1u << bits;
}

void hal_pwm_set_resolution(uint8_t bits) {
  s_resolution = clamp_resolution(bits);
}

bool hal_pwm_is_pin_supported(uint8_t pin) {
  return jh_stm32_pwm_pin_supported(pin);
}

void hal_pwm_write(uint8_t pin, uint32_t value) {
  if (!hal_pwm_is_pin_supported(pin)) {
    HAL_ASSERT(false, "hal_pwm_write: unsupported pin");
    return;
  }
  const uint32_t period_ticks = period_ticks_for_resolution(s_resolution);
  jh_stm32_pwm_channel_desc ch = {};
  if (!jh_stm32_pwm_prepare_pin(pin, kDefaultPwmFrequencyHz, period_ticks,
                                &ch)) {
    HAL_ASSERT(false, "hal_pwm_write: PWM pin configuration failed");
    return;
  }

  const uint32_t max_input = period_ticks - 1u;
  const uint32_t compare = (value >= max_input) ? period_ticks : value;
  jh_stm32_pwm_write_compare(&ch, compare);
  jh_stm32_pwm_start_output(&ch);
}

#endif // HAL_TARGET_IS_STM32G474
