#include "hal/core/hal_compiler.h"
#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_DMA_PWM_AUDIO

#include "hal/audio/hal_dma_pwm_audio.h"
#include "hal/audio/hal_dma_pwm_audio_internal.h"
#include "hal_mock.h"

#include <string.h>

#ifndef HAL_DMA_PWM_AUDIO_MAX_CHANNELS
#define HAL_DMA_PWM_AUDIO_MAX_CHANNELS 4
#endif

struct hal_dma_pwm_audio_impl_s {
  int in_use;
  int running;
  int paused;
  uint32_t completions;
  hal_dma_pwm_audio_config_t cfg;
};

static hal_dma_pwm_audio_impl_t s_pool[HAL_DMA_PWM_AUDIO_MAX_CHANNELS];
static uint8_t s_pool_lock = 0u;
static bool s_fail_next_create = false;
static bool s_fail_next_pause = false;
static bool s_fail_next_resume = false;

static void pool_lock(void) {
  while (HAL_ATOMIC_TEST_AND_SET(&s_pool_lock, HAL_ATOMIC_ACQUIRE)) {
  }
}

static void pool_unlock(void) {
  HAL_ATOMIC_CLEAR(&s_pool_lock, HAL_ATOMIC_RELEASE);
}

bool hal_dma_pwm_audio_supported(void) { return true; }

hal_dma_pwm_audio_t
hal_dma_pwm_audio_create(const hal_dma_pwm_audio_config_t *cfg) {
  hal_dma_pwm_audio_t audio = nullptr;
  (void)hal_dma_pwm_audio_create_ex(cfg, &audio);
  return audio;
}

hal_status_t hal_dma_pwm_audio_create_ex(const hal_dma_pwm_audio_config_t *cfg,
                                         hal_dma_pwm_audio_t *out_audio) {
  if (out_audio == nullptr) {
    return HAL_EINVAL;
  }
  *out_audio = nullptr;
  if (s_fail_next_create) {
    s_fail_next_create = false;
    return HAL_EIO;
  }

  if (!jh_hal_dma_pwm_audio_config_is_valid(cfg)) {
    return HAL_EINVAL;
  }
  for (uint8_t i = 0u; i < cfg->adc_count; ++i) {
    if (cfg->adc_pins[i] == cfg->pwm_pin) {
      return HAL_EINVAL;
    }
  }

  pool_lock();
  if (cfg->adc_count > 0u) {
    for (int i = 0; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
      if (s_pool[i].in_use && s_pool[i].cfg.adc_count > 0u) {
        pool_unlock();
        return HAL_EBUSY;
      }
    }
  }

  for (int i = 0; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    if (s_pool[i].in_use) {
      continue;
    }
    memset(&s_pool[i], 0, sizeof(s_pool[i]));
    s_pool[i].in_use = 1;
    s_pool[i].cfg = *cfg;
    *out_audio = &s_pool[i];
    pool_unlock();
    return HAL_OK;
  }

  pool_unlock();
  return HAL_ENOMEM;
}

bool hal_dma_pwm_audio_start(hal_dma_pwm_audio_t audio) {
  return hal_status_to_bool(hal_dma_pwm_audio_start_ex(audio));
}

hal_status_t hal_dma_pwm_audio_start_ex(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (audio->running || audio->paused) {
    return HAL_ESTATE;
  }
  audio->running = 1;
  audio->paused = 0;
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_stop(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr) {
    return HAL_EINVAL;
  }
  if (!audio->in_use) {
    return HAL_ESTATE;
  }
  audio->running = 0;
  audio->paused = 0;
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_pause(hal_dma_pwm_audio_t audio,
                                     uint16_t idle_value) {
  if (audio == nullptr) {
    return HAL_EINVAL;
  }
  if (!audio->in_use) {
    return HAL_ESTATE;
  }
  if (idle_value >= audio->cfg.period_ticks) {
    return HAL_EINVAL;
  }
  if (!audio->running || audio->paused) {
    return HAL_ESTATE;
  }
  if (s_fail_next_pause) {
    s_fail_next_pause = false;
    return HAL_EIO;
  }
  audio->cfg.idle_value = idle_value;
  audio->running = 0;
  audio->paused = 1;
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_resume(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (!audio->paused || audio->running) {
    return HAL_ESTATE;
  }
  if (s_fail_next_resume) {
    s_fail_next_resume = false;
    return HAL_EIO;
  }
  audio->paused = 0;
  audio->running = 1;
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_destroy_ex(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  (void)hal_dma_pwm_audio_stop(audio);
  pool_lock();
  memset(audio, 0, sizeof(*audio));
  pool_unlock();
  return HAL_OK;
}

void hal_dma_pwm_audio_destroy(hal_dma_pwm_audio_t audio) {
  (void)hal_dma_pwm_audio_destroy_ex(audio);
}

bool hal_dma_pwm_audio_is_running(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use && audio->running != 0 &&
         audio->paused == 0;
}

bool hal_dma_pwm_audio_is_paused(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use && audio->paused != 0;
}

void hal_mock_dma_pwm_audio_complete(hal_dma_pwm_audio_t audio,
                                     uint8_t buffer_index) {
  if (audio == nullptr || !audio->in_use || !audio->running || audio->paused) {
    return;
  }

  uint16_t *buffer =
      (buffer_index == 0u) ? audio->cfg.buffer_a : audio->cfg.buffer_b;
  audio->completions++;
  if (audio->cfg.buffer_done_cb != nullptr) {
    audio->cfg.buffer_done_cb(audio->cfg.user, buffer, buffer_index ? 1u : 0u);
  }
}

hal_dma_pwm_audio_t hal_mock_dma_pwm_audio_find_by_pin(uint8_t pwm_pin) {
  hal_dma_pwm_audio_t result = nullptr;
  pool_lock();
  for (int i = 0; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    if (s_pool[i].in_use && s_pool[i].cfg.pwm_pin == pwm_pin) {
      result = &s_pool[i];
      break;
    }
  }
  pool_unlock();
  return result;
}

void hal_mock_dma_pwm_audio_fail_next_create(bool fail) {
  s_fail_next_create = fail;
}

void hal_mock_dma_pwm_audio_fail_next_pause(bool fail) {
  s_fail_next_pause = fail;
}

void hal_mock_dma_pwm_audio_fail_next_resume(bool fail) {
  s_fail_next_resume = fail;
}

uint32_t hal_mock_dma_pwm_audio_completion_count(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use ? audio->completions : 0u;
}

uint8_t hal_mock_dma_pwm_audio_get_pin(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use ? audio->cfg.pwm_pin : 0u;
}

uint16_t hal_mock_dma_pwm_audio_get_idle_value(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use ? audio->cfg.idle_value : 0u;
}

#endif /* HAL_ENABLE_DMA_PWM_AUDIO */
#endif /* HAL_TARGET_IS_MOCK */
