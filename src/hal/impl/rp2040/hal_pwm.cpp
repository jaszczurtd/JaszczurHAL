#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_pwm.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <pico/platform.h>

static uint8_t s_resolution_bits = 8u;
static hal_mutex_t s_pwm_mutex = NULL;

static constexpr uint32_t kDefaultPwmFrequencyHz = 1000u;
static constexpr uint8_t kPwmSliceCount = 8u;

static bool s_slice_configured[kPwmSliceCount] = {};
static uint32_t s_slice_wrap[kPwmSliceCount] = {};

static void pwm_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_pwm_mutex);
}

static bool pwm_pin_valid(uint8_t pin) { return pin < NUM_BANK0_GPIOS; }

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

static uint32_t max_value_for_resolution(void) {
  return (1u << s_resolution_bits) - 1u;
}

static uint32_t pwm_effective_scale_for_max(uint32_t max_value) {
  uint32_t scale = max_value;
  while (((clock_get_hz(clk_sys) / ((float)scale * kDefaultPwmFrequencyHz)) >
          255.0f) &&
         (scale < 32768u)) {
    scale *= 2u;
  }
  while (((clock_get_hz(clk_sys) / ((float)scale * kDefaultPwmFrequencyHz)) <
          1.0f) &&
         (scale >= 6u)) {
    scale /= 2u;
  }
  return scale;
}

static float pwm_clkdiv_for_scale(uint32_t scale) {
  float clkdiv =
      clock_get_hz(clk_sys) / ((float)kDefaultPwmFrequencyHz * scale);
  if (clkdiv < 1.0f) {
    clkdiv = 1.0f;
  } else if (clkdiv > 255.0f) {
    clkdiv = 255.0f;
  }
  return clkdiv;
}

static void pwm_configure_slice(uint slice, uint32_t wrap) {
  if (slice >= kPwmSliceCount) {
    return;
  }
  if (s_slice_configured[slice] && s_slice_wrap[slice] == wrap) {
    return;
  }

  pwm_config c = pwm_get_default_config();
  pwm_config_set_clkdiv(&c, pwm_clkdiv_for_scale(wrap + 1u));
  pwm_config_set_wrap(&c, (uint16_t)wrap);
  pwm_init(slice, &c, s_slice_configured[slice]);

  s_slice_configured[slice] = true;
  s_slice_wrap[slice] = wrap;
}

void hal_pwm_set_resolution(uint8_t bits) {
  pwm_ensure_mutex();
  hal_mutex_lock(s_pwm_mutex);
  s_resolution_bits = clamp_resolution(bits);
  hal_mutex_unlock(s_pwm_mutex);
}

bool hal_pwm_is_pin_supported(uint8_t pin) { return pwm_pin_valid(pin); }

void hal_pwm_write(uint8_t pin, uint32_t value) {
  if (!pwm_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_pwm_write: unsupported pin");
    return;
  }
  const uint32_t max_value = max_value_for_resolution();
  if (value > max_value) {
    value = max_value;
  }

  pwm_ensure_mutex();
  hal_mutex_lock(s_pwm_mutex);

  const uint slice = pwm_gpio_to_slice_num(pin);
  const uint32_t effective_scale = pwm_effective_scale_for_max(max_value);
  const uint32_t wrap = effective_scale - 1u;
  pwm_configure_slice(slice, wrap);

  uint32_t level = 0u;
  if (max_value > 0u) {
    level = (uint32_t)(((uint64_t)value * effective_scale + (max_value / 2u)) /
                       max_value);
    if (level > effective_scale) {
      level = effective_scale;
    }
  }

  pwm_set_gpio_level(pin, (uint16_t)level);
  gpio_set_function(pin, GPIO_FUNC_PWM);
  pwm_set_enabled(slice, true);

  hal_mutex_unlock(s_pwm_mutex);
}
#endif // HAL_TARGET_IS_RP2040
