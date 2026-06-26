#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAL_ENABLE_DMA_PWM_AUDIO

#include <stdbool.h>
#include <stdint.h>

/**
 * @file hal_dma_pwm_audio.h
 * @brief DMA helper API for timer-paced PWM audio buffers.
 *
 * The first public surface intentionally covers only the DACless use case:
 * a timer/PWM output fed from two 16-bit sample buffers, with an optional
 * ADC result buffer refreshed by the backend where the target can do it in
 * hardware.
 */

typedef struct hal_dma_pwm_audio_impl_s hal_dma_pwm_audio_impl_t;
typedef hal_dma_pwm_audio_impl_t *hal_dma_pwm_audio_t;

typedef void (*hal_dma_pwm_audio_buffer_cb_t)(void *user, uint16_t *buffer,
                                              uint8_t buffer_index);

typedef struct {
  uint8_t pwm_pin;
  uint32_t sample_rate_hz;
  uint32_t period_ticks;
  uint16_t *buffer_a;
  uint16_t *buffer_b;
  uint16_t block_size;
  uint16_t idle_value;
  const uint8_t *adc_pins;
  uint8_t adc_count;
  volatile uint16_t *adc_buffer;
  hal_dma_pwm_audio_buffer_cb_t buffer_done_cb;
  void *user;
} hal_dma_pwm_audio_config_t;

bool hal_dma_pwm_audio_supported(void);
hal_dma_pwm_audio_t
hal_dma_pwm_audio_create(const hal_dma_pwm_audio_config_t *cfg);
bool hal_dma_pwm_audio_start(hal_dma_pwm_audio_t audio);
void hal_dma_pwm_audio_stop(hal_dma_pwm_audio_t audio);
void hal_dma_pwm_audio_pause(hal_dma_pwm_audio_t audio, uint16_t idle_value);
void hal_dma_pwm_audio_resume(hal_dma_pwm_audio_t audio);
void hal_dma_pwm_audio_destroy(hal_dma_pwm_audio_t audio);
bool hal_dma_pwm_audio_is_running(hal_dma_pwm_audio_t audio);
bool hal_dma_pwm_audio_is_paused(hal_dma_pwm_audio_t audio);

uint16_t hal_dma_interpolate0(uint16_t x, uint16_t y, uint16_t mu_scaled);
uint16_t hal_dma_interpolate1(uint16_t x, uint16_t y, uint16_t mu_scaled);

#endif /* HAL_ENABLE_DMA_PWM_AUDIO */

#ifdef __cplusplus
}
#endif
