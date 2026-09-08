#ifndef JH_HAL_PWM_FREQ_POOL_H
#define JH_HAL_PWM_FREQ_POOL_H

#include "hal/core/hal_assert.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_status.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/gpio/hal_pwm_freq_internal.h"
#include "hal/system/hal_sync.h"

#include <stddef.h>
#include <string.h>

template <typename Channel, size_t Size>
Channel *jh_hal_pwm_freq_reserve(Channel (&pool)[Size], int configured_size) {
  const size_t limit = configured_size > 0 ? (size_t)configured_size : 0u;
  for (size_t i = 0u; i < Size && i < limit; ++i) {
    if (pool[i].in_use == 0) {
      return &pool[i];
    }
  }
  return nullptr;
}

/* Returns a cleared pool slot with the module mutex held. */
template <typename Channel, size_t Size>
Channel *jh_hal_pwm_freq_begin_locked_create(Channel (&pool)[Size],
                                             int configured_size,
                                             hal_mutex_t *mutex_storage) {
  hal_mutex_t mutex = jh_hal_mutex_try_create_once(mutex_storage);
  if (mutex == nullptr) {
    return nullptr;
  }
  hal_mutex_lock(mutex);
  Channel *channel = jh_hal_pwm_freq_reserve(pool, configured_size);
  if (channel == nullptr) {
    hal_mutex_unlock(mutex);
    return nullptr;
  }
  memset(channel, 0, sizeof(*channel));
  return channel;
}

static inline hal_status_t
jh_hal_pwm_freq_prepare_create(uint32_t frequency_hz, uint32_t resolution,
                               hal_pwm_freq_channel_t *out_channel) {
  if (out_channel == nullptr || frequency_hz == 0u || resolution == 0u) {
    return HAL_EINVAL;
  }
  *out_channel = nullptr;
  return HAL_OK;
}

static inline hal_pwm_freq_channel_t
jh_hal_pwm_freq_create_compat(uint8_t pin, uint32_t frequency_hz,
                              uint32_t resolution) {
  hal_pwm_freq_channel_t channel = nullptr;
  const hal_status_t status =
      jh_hal_pwm_freq_try_create(pin, frequency_hz, resolution, &channel);
  if (status == HAL_ENOMEM) {
    HAL_ASSERT(
        false,
        "hal_pwm_freq: pool exhausted - increase HAL_PWM_FREQ_MAX_CHANNELS");
  }
  return status == HAL_OK ? channel : nullptr;
}

#endif
