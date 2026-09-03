#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_resolution.h"
#include "hal/gpio/hal_pwm.h"
#include "hal/system/hal_sync.h"
#include "jh_esp32_gpio.h"
#include "jh_esp32_ledc.h"

#include <soc/soc_caps.h>

#include <stdint.h>

namespace {

constexpr uint32_t kDefaultFrequencyHz = 1000u;
hal_mutex_t s_mutex;
uint8_t s_resolution_bits = 8u;
jh_esp32_ledc_channel_t *s_channels[SOC_GPIO_PIN_COUNT] = {};

hal_mutex_t pwm_mutex(void) { return jh_hal_mutex_create_once(&s_mutex); }

uint32_t resolution_max(uint8_t bits) {
  return (UINT32_C(1) << bits) - UINT32_C(1);
}

} // namespace

void hal_pwm_set_resolution(uint8_t bits) {
  hal_mutex_t mutex = pwm_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  const uint8_t next = jh_resolution_clamp_1_16(
      bits, "hal_pwm_set_resolution: resolution is below 1 bit",
      "hal_pwm_set_resolution: resolution is above 16 bits");
  bool released = true;
  if (next != s_resolution_bits) {
    for (jh_esp32_ledc_channel_t *&channel : s_channels) {
      if (channel != nullptr) {
        if (jh_esp32_ledc_release(channel)) {
          channel = nullptr;
        } else {
          released = false;
        }
      }
    }
    if (released) {
      s_resolution_bits = next;
    }
  }
  hal_mutex_unlock(mutex);
  HAL_ASSERT(released, "hal_pwm_set_resolution: LEDC teardown failed");
}

bool hal_pwm_is_pin_supported(uint8_t pin) {
  return jh_esp32_gpio_output_pin_valid(pin);
}

void hal_pwm_write(uint8_t pin, uint32_t value) {
  if (!hal_pwm_is_pin_supported(pin)) {
    HAL_ASSERT(false, "hal_pwm_write: unsupported pin");
    return;
  }
  hal_mutex_t mutex = pwm_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  const uint32_t maximum = resolution_max(s_resolution_bits);
  if (value > maximum) {
    value = maximum;
  }
  if (s_channels[pin] == nullptr) {
    s_channels[pin] = jh_esp32_ledc_acquire(pin, kDefaultFrequencyHz, maximum);
  }
  const bool written =
      s_channels[pin] != nullptr && jh_esp32_ledc_write(s_channels[pin], value);
  hal_mutex_unlock(mutex);
  HAL_ASSERT(written, "hal_pwm_write: ESP-IDF LEDC resource/update failed");
}

#endif // HAL_TARGET_IS_ESP32_FAMILY
