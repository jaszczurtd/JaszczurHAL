#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_PWM_FREQ

#include "hal/core/hal_mutex_once.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/gpio/hal_pwm_freq_internal.h"
#include "hal/gpio/hal_pwm_freq_pool.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal_pwm_stm32g474.h"

#include <string.h>

static hal_mutex_t pwm_mutex = NULL;

static void pwm_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&pwm_mutex);
}

struct hal_pwm_freq_channel_impl_s {
  jh_stm32_pwm_channel_desc pwm;
  uint32_t requested_period_ticks;
  uint32_t frequency_hz;
  uint32_t last_value; // last logical duty written
  uint8_t left_shift;
  uint8_t right_shift;
  int in_use;
  int started;
};

static hal_pwm_freq_channel_impl_t s_pool[HAL_PWM_FREQ_MAX_CHANNELS];

hal_status_t jh_hal_pwm_freq_try_create(uint8_t pin, uint32_t frequency_hz,
                                        uint32_t resolution,
                                        hal_pwm_freq_channel_t *out_channel) {
  const hal_status_t args_status =
      jh_hal_pwm_freq_prepare_create(frequency_hz, resolution, out_channel);
  if (args_status != HAL_OK) {
    return args_status;
  }
  hal_pwm_freq_channel_impl_t *cfg = jh_hal_pwm_freq_begin_locked_create(
      s_pool, hal_get_config()->pwm_freq_max_channels, &pwm_mutex);
  if (!cfg) {
    return HAL_ENOMEM;
  }

  const hal_status_t pin_status = jh_stm32_pwm_prepare_frequency_pin(
      pin, frequency_hz, resolution, &cfg->pwm, &cfg->left_shift,
      &cfg->right_shift);
  if (pin_status != HAL_OK) {
    hal_mutex_unlock(pwm_mutex);
    return pin_status;
  }

  cfg->in_use = 1;
  cfg->requested_period_ticks = resolution;
  cfg->frequency_hz = frequency_hz;
  cfg->started = 0;
  hal_mutex_unlock(pwm_mutex);
  *out_channel = cfg;
  return HAL_OK;
}

hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin, uint32_t frequency_hz,
                                           uint32_t resolution) {
  return jh_hal_pwm_freq_create_compat(pin, frequency_hz, resolution);
}

uint32_t hal_pwm_freq_source_clock_hz(uint8_t pin) {
  return jh_stm32_pwm_source_clock_hz(pin);
}

void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value) {
  if (!ch) {
    hal_derr_limited("pwm_freq", "write called with NULL channel");
    return;
  }
  pwm_ensure_mutex();

  hal_pwm_freq_channel_impl_t *cfg = ch;
  // The counter period and the shifts change with the frequency, so the
  // compare value is derived under the lock a frequency change holds.
  hal_mutex_lock(pwm_mutex);
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

  cfg->last_value = (uint32_t)value;
  jh_stm32_pwm_write_compare(&cfg->pwm, (uint32_t)compare);
  if (!cfg->started) {
    jh_stm32_pwm_start_output(&cfg->pwm);
    cfg->started = 1;
  }
  hal_mutex_unlock(pwm_mutex);
}

hal_status_t hal_pwm_freq_set_frequency(hal_pwm_freq_channel_t ch,
                                        uint32_t frequency_hz) {
  if (!ch || frequency_hz == 0u) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = jh_hal_mutex_try_create_once(&pwm_mutex);
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  hal_status_t status = HAL_EINVAL;
  if (ch->in_use != 0) {
    status = HAL_OK;
    if (ch->frequency_hz != frequency_hz) {
      status = jh_stm32_pwm_set_frequency(
          &ch->pwm, frequency_hz, ch->requested_period_ticks, ch->last_value,
          &ch->left_shift, &ch->right_shift);
      if (status == HAL_OK) {
        ch->frequency_hz = frequency_hz;
      }
    }
  }
  hal_mutex_unlock(mutex);
  return status;
}

uint32_t hal_pwm_freq_get_period_ns(hal_pwm_freq_channel_t ch) {
  if (!ch) {
    return 0u;
  }
  hal_mutex_t mutex = jh_hal_mutex_try_create_once(&pwm_mutex);
  if (mutex == NULL) {
    return 0u;
  }
  hal_mutex_lock(mutex);
  const uint32_t period_ns =
      ch->in_use != 0 ? jh_stm32_pwm_period_ns(&ch->pwm) : 0u;
  hal_mutex_unlock(mutex);
  return period_ns;
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
  jh_stm32_pwm_forget_channel(&ch->pwm);
  ch->started = 0;
  ch->in_use = 0;
  hal_mutex_unlock(pwm_mutex);
}

#endif /* HAL_ENABLE_PWM_FREQ */
#endif // HAL_TARGET_IS_STM32G474
