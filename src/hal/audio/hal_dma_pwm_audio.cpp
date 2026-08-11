#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_DMA_PWM_AUDIO

#include "hal/audio/hal_dma_pwm_audio.h"

bool hal_dma_pwm_audio_supported(void) { return true; }

hal_dma_pwm_audio_t
hal_dma_pwm_audio_create(const hal_dma_pwm_audio_config_t *config) {
  hal_dma_pwm_audio_t audio = nullptr;
  (void)hal_dma_pwm_audio_create_ex(config, &audio);
  return audio;
}

#endif
#endif
