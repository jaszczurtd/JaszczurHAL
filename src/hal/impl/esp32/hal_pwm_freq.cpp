#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_PWM_FREQ

#include "hal/core/hal_mutex_once.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/gpio/hal_pwm_freq_pool.h"
#include "hal/system/hal_sync.h"
#include "jh_esp32_gpio.h"
#include "jh_esp32_ledc.h"

#include <string.h>

struct hal_pwm_freq_channel_impl_s {
  jh_esp32_ledc_channel_t *ledc;
  uint32_t resolution;
  int in_use;
};

namespace {

hal_mutex_t s_mutex;
hal_pwm_freq_channel_impl_t s_pool[HAL_PWM_FREQ_MAX_CHANNELS] = {};

hal_mutex_t pwm_mutex(void) { return jh_hal_mutex_create_once(&s_mutex); }

} // namespace

hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin, uint32_t frequency_hz,
                                           uint32_t resolution) {
  if (!jh_esp32_gpio_output_pin_valid(pin) || frequency_hz == 0u ||
      resolution == 0u) {
    HAL_ASSERT(false, "hal_pwm_freq_create: invalid pin/frequency/resolution");
    return nullptr;
  }
  hal_mutex_t mutex = pwm_mutex();
  if (mutex == nullptr) {
    return nullptr;
  }
  hal_mutex_lock(mutex);
  hal_pwm_freq_channel_impl_t *channel =
      jh_hal_pwm_freq_reserve(s_pool, hal_get_config()->pwm_freq_max_channels);
  if (channel == nullptr) {
    hal_mutex_unlock(mutex);
    HAL_ASSERT(false, "hal_pwm_freq_create: logical channel pool exhausted");
    return nullptr;
  }
  memset(channel, 0, sizeof(*channel));
  channel->ledc = jh_esp32_ledc_acquire(pin, frequency_hz, resolution);
  if (channel->ledc == nullptr) {
    hal_mutex_unlock(mutex);
    return nullptr;
  }
  channel->resolution = resolution;
  channel->in_use = 1;
  hal_mutex_unlock(mutex);
  return channel;
}

uint32_t hal_pwm_freq_source_clock_hz(uint8_t pin) {
  return jh_esp32_gpio_output_pin_valid(pin) ? jh_esp32_ledc_source_clock_hz()
                                             : 0u;
}

void hal_pwm_freq_write(hal_pwm_freq_channel_t channel, int value) {
  if (channel == nullptr) {
    return;
  }
  hal_mutex_t mutex = pwm_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (channel->in_use == 0) {
    hal_mutex_unlock(mutex);
    return;
  }
  if (value < 0) {
    value = 0;
  } else if ((uint32_t)value > channel->resolution) {
    value = (int)channel->resolution;
  }
  const bool written = jh_esp32_ledc_write(channel->ledc, (uint32_t)value);
  hal_mutex_unlock(mutex);
  HAL_ASSERT(written, "hal_pwm_freq_write: ESP-IDF LEDC update failed");
}

void hal_pwm_freq_stop(hal_pwm_freq_channel_t channel) {
  if (channel == nullptr) {
    return;
  }
  hal_mutex_t mutex = pwm_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (channel->in_use != 0) {
    jh_esp32_ledc_stop(channel->ledc);
  }
  hal_mutex_unlock(mutex);
}

void hal_pwm_freq_destroy(hal_pwm_freq_channel_t channel) {
  if (channel == nullptr) {
    return;
  }
  hal_mutex_t mutex = pwm_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  bool destroyed = true;
  if (channel->in_use != 0) {
    destroyed = jh_esp32_ledc_release(channel->ledc);
    if (destroyed) {
      memset(channel, 0, sizeof(*channel));
    }
  }
  hal_mutex_unlock(mutex);
  HAL_ASSERT(destroyed, "hal_pwm_freq_destroy: LEDC teardown failed");
}

#endif // HAL_ENABLE_PWM_FREQ
#endif // HAL_TARGET_IS_ESP32_FAMILY
