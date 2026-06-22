#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_pwm.h"
#include "hal_mock.h"

static uint8_t s_resolution = 8;
static uint32_t s_values[64] = {};

static bool pwm_pin_valid(uint8_t pin) { return pin < 64u; }

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
  return (1u << s_resolution) - 1u;
}

void hal_pwm_set_resolution(uint8_t bits) {
  s_resolution = clamp_resolution(bits);
}

bool hal_pwm_is_pin_supported(uint8_t pin) { return pwm_pin_valid(pin); }

void hal_pwm_write(uint8_t pin, uint32_t value) {
  if (!pwm_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_pwm_write: unsupported pin");
    return;
  }
  const uint32_t max_value = max_value_for_resolution();
  s_values[pin] = (value > max_value) ? max_value : value;
}

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

uint32_t hal_mock_pwm_get_value(uint8_t pin) {
  return (pin < 64) ? s_values[pin] : 0;
}

uint8_t hal_mock_pwm_get_resolution(void) { return s_resolution; }
#endif // HAL_TARGET_IS_MOCK
