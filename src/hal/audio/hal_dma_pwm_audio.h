#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAL_ENABLE_DMA_PWM_AUDIO

#include "hal/core/hal_status.h"

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
  /** PWM period in ticks, in the portable range 1..65536. */
  uint32_t period_ticks;
  uint16_t *buffer_a;
  uint16_t *buffer_b;
  /** Samples in each buffer, in the portable range 1..32767. */
  uint16_t block_size;
  /** Idle compare value; must be smaller than period_ticks. */
  uint16_t idle_value;
  const uint8_t *adc_pins;
  uint8_t adc_count;
  volatile uint16_t *adc_buffer;
  hal_dma_pwm_audio_buffer_cb_t buffer_done_cb;
  void *user;
} hal_dma_pwm_audio_config_t;

bool hal_dma_pwm_audio_supported(void);

/**
 * @brief Reserve and configure a PWM-audio DMA instance.
 * @param cfg Configuration whose buffers remain valid for the instance.
 * @param out_audio Destination handle, set to NULL on failure.
 * @return HAL_OK on success, HAL_EINVAL for invalid or unrepresentable
 *         timing/buffer parameters, HAL_EBUSY for an occupied hardware
 *         resource, HAL_ENOMEM when the instance or DMA pool is exhausted,
 *         or HAL_EIO when hardware configuration fails.
 */
hal_status_t hal_dma_pwm_audio_create_ex(const hal_dma_pwm_audio_config_t *cfg,
                                         hal_dma_pwm_audio_t *out_audio);
hal_dma_pwm_audio_t
hal_dma_pwm_audio_create(const hal_dma_pwm_audio_config_t *cfg);

/**
 * @brief Start a configured, stopped instance.
 * @return HAL_OK on success, HAL_EINVAL for NULL, or HAL_ESTATE if the
 *         instance is invalid, already running, or paused.
 */
hal_status_t hal_dma_pwm_audio_start_ex(hal_dma_pwm_audio_t audio);
bool hal_dma_pwm_audio_start(hal_dma_pwm_audio_t audio);

/** @brief Stop an instance; repeated calls are allowed. */
hal_status_t hal_dma_pwm_audio_stop(hal_dma_pwm_audio_t audio);
/**
 * @brief Quiesce the audio DMA channels and drive the configured PWM idle.
 *
 * A successful pause guarantees that this audio instance has no active DMA
 * transfer. This lets the application enter a target-specific flash safe zone
 * before calling @ref hal_dma_pwm_audio_resume.
 * @return HAL_OK on success, HAL_EINVAL for NULL or an idle value outside the
 *         configured PWM period, or HAL_ESTATE unless the instance is
 *         currently running.
 */
hal_status_t hal_dma_pwm_audio_pause(hal_dma_pwm_audio_t audio,
                                     uint16_t idle_value);
/**
 * @brief Restart a paused audio stream from the beginning of both buffers.
 * @return HAL_OK on success, HAL_EINVAL for NULL, or HAL_ESTATE unless the
 *         instance is currently paused.
 */
hal_status_t hal_dma_pwm_audio_resume(hal_dma_pwm_audio_t audio);

/**
 * @brief Stop and release an instance.
 * @note On RP targets, call create, lifecycle operations and destroy for an
 *       instance from the same core because its DMA IRQ belongs to that core.
 * @return HAL_OK on success, HAL_EINVAL for NULL, HAL_ESTATE for an invalid
 *         instance, or HAL_EBUSY on an RP core other than the owner.
 */
hal_status_t hal_dma_pwm_audio_destroy_ex(hal_dma_pwm_audio_t audio);

/** @brief Compatibility wrapper that ignores the destroy status. */
void hal_dma_pwm_audio_destroy(hal_dma_pwm_audio_t audio);
bool hal_dma_pwm_audio_is_running(hal_dma_pwm_audio_t audio);
bool hal_dma_pwm_audio_is_paused(hal_dma_pwm_audio_t audio);

static inline uint16_t hal_dma_interpolate(uint16_t x, uint16_t y,
                                           uint16_t mu_scaled) {
  const int32_t delta = (int32_t)y - (int32_t)x;
  const int32_t frac = (int32_t)(mu_scaled & 0xFFu);
  int32_t value = (int32_t)x + ((delta * frac) >> 8);
  if (value < 0) {
    return 0u;
  }
  if (value > 65535) {
    return 65535u;
  }
  return (uint16_t)value;
}

#endif /* HAL_ENABLE_DMA_PWM_AUDIO */

#ifdef __cplusplus
}
#endif
