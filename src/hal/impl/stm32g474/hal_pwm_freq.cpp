#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_config.h"
#ifdef HAL_ENABLE_PWM_FREQ

#include "../../hal_pwm_freq.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"
#include "hal_pwm_stm32g474.h"

#include <string.h>

static hal_mutex_t pwm_mutex = NULL;

static void pwm_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&pwm_mutex);
}

struct hal_pwm_freq_channel_impl_s {
  jh_stm32_pwm_channel_desc pwm;
  uint32_t requested_period_ticks;
  uint8_t left_shift;
  uint8_t right_shift;
  int in_use;
  int started;
};

static hal_pwm_freq_channel_impl_t s_pool[HAL_PWM_FREQ_MAX_CHANNELS];

hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin, uint32_t frequency_hz,
                                           uint32_t resolution) {
  pwm_ensure_mutex();
  hal_mutex_lock(pwm_mutex);

  hal_pwm_freq_channel_impl_t *cfg = NULL;
  for (int i = 0; i < hal_get_config()->pwm_freq_max_channels; i++) {
    if (!s_pool[i].in_use) {
      cfg = &s_pool[i];
      break;
    }
  }
  if (!cfg) {
    hal_mutex_unlock(pwm_mutex);
    HAL_ASSERT(
        cfg != NULL,
        "hal_pwm_freq: pool exhausted - increase HAL_PWM_FREQ_MAX_CHANNELS");
    return NULL;
  }

  memset(cfg, 0, sizeof(*cfg));
  if (!jh_stm32_pwm_prepare_frequency_pin(pin, frequency_hz, resolution,
                                          &cfg->pwm, &cfg->left_shift,
                                          &cfg->right_shift)) {
    hal_mutex_unlock(pwm_mutex);
    return NULL;
  }

  cfg->in_use = 1;
  cfg->requested_period_ticks = resolution;
  cfg->started = 0;
  hal_mutex_unlock(pwm_mutex);
  return cfg;
}

void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value) {
  if (!ch) {
    hal_derr_limited("pwm_freq", "write called with NULL channel");
    return;
  }
  pwm_ensure_mutex();

  hal_pwm_freq_channel_impl_t *cfg = ch;
  if (value < 0) {
    value = 0;
  } else if ((uint32_t)value > cfg->requested_period_ticks) {
    value = (int)cfg->requested_period_ticks;
  }

  uint64_t compare = (uint32_t)value;
  if ((uint32_t)value >= cfg->requested_period_ticks) {
    compare = cfg->pwm.period_ticks;
  } else {
    if (cfg->left_shift > 0u) {
      compare <<= cfg->left_shift;
    }
    if (cfg->right_shift > 0u) {
      compare >>= cfg->right_shift;
    }
  }

  if (compare > cfg->pwm.period_ticks) {
    compare = cfg->pwm.period_ticks;
  }

  hal_mutex_lock(pwm_mutex);
  jh_stm32_pwm_write_compare(&cfg->pwm, (uint32_t)compare);
  if (!cfg->started) {
    jh_stm32_pwm_start_output(&cfg->pwm);
    cfg->started = 1;
  }
  hal_mutex_unlock(pwm_mutex);
}

void hal_pwm_freq_stop(hal_pwm_freq_channel_t ch) {
  if (!ch) {
    return;
  }
  pwm_ensure_mutex();
  hal_mutex_lock(pwm_mutex);
  jh_stm32_pwm_release_output(&ch->pwm);
  ch->started = 0;
  hal_mutex_unlock(pwm_mutex);
}

void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch) {
  if (!ch) {
    return;
  }
  pwm_ensure_mutex();
  hal_mutex_lock(pwm_mutex);
  jh_stm32_pwm_release_output(&ch->pwm);
  ch->started = 0;
  ch->in_use = 0;
  hal_mutex_unlock(pwm_mutex);
}

#endif /* HAL_ENABLE_PWM_FREQ */
#endif // HAL_TARGET_IS_STM32G474
