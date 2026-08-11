#ifndef JH_HAL_PWM_FREQ_POOL_H
#define JH_HAL_PWM_FREQ_POOL_H

#include <stddef.h>

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

#endif
