#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_pwm.h"
#include <Arduino.h>

static uint8_t s_resolution_bits = 8u;

static bool pwm_pin_valid(uint8_t pin) {
#if defined(NUM_DIGITAL_PINS)
  return pin < NUM_DIGITAL_PINS;
#elif defined(PINS_COUNT)
  return pin < PINS_COUNT;
#else
  (void)pin;
  return true;
#endif
}

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

void hal_pwm_set_resolution(uint8_t bits) {
  s_resolution_bits = clamp_resolution(bits);
  analogWriteResolution(s_resolution_bits);
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
  analogWrite(pin, (int)value);
}
#endif // HAL_TARGET_IS_RP2040
