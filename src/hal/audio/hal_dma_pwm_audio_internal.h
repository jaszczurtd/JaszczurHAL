#pragma once

#include "hal/audio/hal_dma_pwm_audio.h"

#ifdef HAL_ENABLE_DMA_PWM_AUDIO

#include <stddef.h>

static inline bool
jh_hal_dma_pwm_audio_config_is_valid(const hal_dma_pwm_audio_config_t *cfg) {
  return cfg != NULL && cfg->buffer_a != NULL && cfg->buffer_b != NULL &&
         cfg->block_size > 0u && cfg->block_size <= 32767u &&
         cfg->period_ticks > 0u && cfg->period_ticks <= 65536u &&
         cfg->idle_value < cfg->period_ticks && cfg->sample_rate_hz > 0u &&
         cfg->adc_count <= 4u &&
         (cfg->adc_count == 0u ||
          (cfg->adc_pins != NULL && cfg->adc_buffer != NULL));
}

#endif /* HAL_ENABLE_DMA_PWM_AUDIO */
