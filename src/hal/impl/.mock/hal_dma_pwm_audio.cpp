#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK

#include "../../hal_config.h"
#ifdef HAL_ENABLE_DMA_PWM_AUDIO

#include "../../hal_dma_pwm_audio.h"
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
static bool s_fail_next_create = false;

bool hal_dma_pwm_audio_supported(void) { return true; }

hal_dma_pwm_audio_t
hal_dma_pwm_audio_create(const hal_dma_pwm_audio_config_t *cfg) {
  if (s_fail_next_create) {
    s_fail_next_create = false;
    return nullptr;
  }

  if (cfg == nullptr || cfg->buffer_a == nullptr || cfg->buffer_b == nullptr ||
      cfg->block_size == 0u || cfg->period_ticks == 0u ||
      cfg->sample_rate_hz == 0u) {
    return nullptr;
  }

  for (int i = 0; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    if (s_pool[i].in_use) {
      continue;
    }
    memset(&s_pool[i], 0, sizeof(s_pool[i]));
    s_pool[i].in_use = 1;
    s_pool[i].cfg = *cfg;
    return &s_pool[i];
  }

  HAL_ASSERT(false, "hal_dma_pwm_audio: mock pool exhausted");
  return nullptr;
}

bool hal_dma_pwm_audio_start(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return false;
  }
  audio->running = 1;
  audio->paused = 0;
  return true;
}

void hal_dma_pwm_audio_stop(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr) {
    return;
  }
  audio->running = 0;
}

void hal_dma_pwm_audio_pause(hal_dma_pwm_audio_t audio, uint16_t idle_value) {
  if (audio == nullptr) {
    return;
  }
  audio->cfg.idle_value = idle_value;
  audio->paused = 1;
}

void hal_dma_pwm_audio_resume(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return;
  }
  audio->paused = 0;
  audio->running = 1;
}

void hal_dma_pwm_audio_destroy(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr) {
    return;
  }
  memset(audio, 0, sizeof(*audio));
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

void hal_mock_dma_pwm_audio_fail_next_create(bool fail) {
  s_fail_next_create = fail;
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
