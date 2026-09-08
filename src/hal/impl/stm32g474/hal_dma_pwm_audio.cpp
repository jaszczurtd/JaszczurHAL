#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_DMA_PWM_AUDIO

#include "hal/analog/hal_adc.h"
#include "hal/audio/hal_dma_pwm_audio.h"
#include "hal/audio/hal_dma_pwm_audio_internal.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "hal_pwm_stm32g474.h"
#include "stm32g474_adc_shared.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef JH_STM32G474_HW
#include "port/stm32g474_adc_channels.h"
#include "port/stm32g474_regs.h"
#endif

struct hal_dma_pwm_audio_impl_s {
  bool in_use;
  bool running;
  bool paused;
  jh_stm32_pwm_channel_desc pwm;
  uint16_t *buffer_a;
  uint16_t *buffer_b;
  uint16_t block_size;
  uint16_t idle_value;
  const uint8_t *adc_pins;
  uint8_t adc_count;
  volatile uint16_t *adc_buffer;
  bool adc_dma_configured;
  bool adc_claimed;
  hal_dma_pwm_audio_buffer_cb_t cb;
  void *user;
};

static hal_dma_pwm_audio_impl_t s_pool[HAL_DMA_PWM_AUDIO_MAX_CHANNELS];

static void pool_lock(void) { hal_critical_section_enter(); }

static void pool_unlock(void) { hal_critical_section_exit(); }

static bool audio_state_load(const bool *state) {
  return __atomic_load_n(state, __ATOMIC_ACQUIRE);
}

static void audio_state_store(bool *state, bool value) {
  __atomic_store_n(state, value, __ATOMIC_RELEASE);
}

#ifdef JH_STM32G474_HW
static hal_dma_pwm_audio_impl_t *s_irq_audio = nullptr;
static constexpr uint8_t kPwmDmaChannel = 0u; /* DMA1 Channel1 */
static constexpr uint8_t kAdcDmaChannel = 1u; /* DMA1 Channel2 */
static constexpr uint32_t kAdcDmaCfgrMask =
    ADC_CFGR_DMAEN | ADC_CFGR_DMACFG | ADC_CFGR_OVRMOD | ADC_CFGR_CONT;

static bool adc_pin_channels(const uint8_t *pins, uint8_t count,
                             uint32_t channels[4]) {
  if (count == 0u) {
    return true;
  }
  if (pins == nullptr || count > 4u) {
    return false;
  }
  for (uint8_t i = 0u; i < count; ++i) {
    channels[i] = jh_stm32g474_adc1_channel_for_pin(pins[i]);
    if (channels[i] == 0u) {
      return false;
    }
  }
  return true;
}

static void set_pin_analog(uint8_t pin) {
  const uint32_t port = (uint32_t)(pin >> 4);
  const uint32_t n = (uint32_t)(pin & 0x0Fu);
  if (port > 6u) {
    return;
  }

  RCC_AHB2ENR |= (1u << port);
  (void)RCC_AHB2ENR;
  GPIO_MODER(port) =
      (GPIO_MODER(port) & ~(0x3u << (n * 2u))) | (GPIO_MODE_ANALOG << (n * 2u));
  GPIO_PUPDR(port) &= ~(0x3u << (n * 2u));
}

static void stop_adc_conversion(void) {
  if ((ADC1_CR & ADC_CR_ADSTART) != 0u) {
    ADC1_CR |= ADC_CR_ADSTP;
    uint32_t timeout = ADC_POLL_TIMEOUT;
    while ((ADC1_CR & ADC_CR_ADSTP) != 0u && timeout > 0u) {
      --timeout;
    }
  }
}

static void configure_adc_sequence(const uint8_t *pins, uint8_t count,
                                   const uint32_t channels[4]) {
  uint32_t sqr = (uint32_t)(count - 1u) & ADC_SQR1_L_MASK;
  const uint32_t shifts[4] = {ADC_SQR1_SQ1_POS, ADC_SQR1_SQ2_POS,
                              ADC_SQR1_SQ3_POS, ADC_SQR1_SQ4_POS};

  for (uint8_t i = 0u; i < count; ++i) {
    set_pin_analog(pins[i]);
    const uint32_t ch = channels[i];
    if (ch <= 9u) {
      ADC1_SMPR1 = (ADC1_SMPR1 & ~(0x7u << (ch * 3u))) |
                   (ADC_SMP_247CYCLES << (ch * 3u));
    } else {
      const uint32_t s = ch - 10u;
      ADC1_SMPR2 =
          (ADC1_SMPR2 & ~(0x7u << (s * 3u))) | (ADC_SMP_247CYCLES << (s * 3u));
    }
    sqr |= ch << shifts[i];
  }

  ADC1_SQR1 = sqr;
}

static hal_status_t configure_adc_dma(hal_dma_pwm_audio_impl_t *audio) {
  if (audio->adc_count == 0u) {
    return HAL_OK;
  }
  if (audio->adc_buffer == nullptr) {
    return HAL_EINVAL;
  }

  uint32_t channels[4] = {};
  if (!adc_pin_channels(audio->adc_pins, audio->adc_count, channels)) {
    return HAL_EINVAL;
  }
  if (!audio->adc_claimed) {
    const hal_status_t status = stm32g474_adc_acquire_dma();
    if (status != HAL_OK) {
      return status;
    }
    audio->adc_claimed = true;
  }

  stop_adc_conversion();
  configure_adc_sequence(audio->adc_pins, audio->adc_count, channels);

  ADC1_CFGR =
      (ADC1_CFGR & ~(ADC_CFGR_RES_MASK | kAdcDmaCfgrMask)) | kAdcDmaCfgrMask;

  DMA_CCR(DMA1_BASE, kAdcDmaChannel) &= ~DMA_CCR_EN;
  DMAMUX_CCR(kAdcDmaChannel) = DMA_REQUEST_ADC1 & DMAMUX_CCR_DMAREQ_ID_MASK;
  DMA_IFCR(DMA1_BASE) = DMA_IFCR_CLEAR_ALL(kAdcDmaChannel);
  DMA_CPAR(DMA1_BASE, kAdcDmaChannel) = (uint32_t)(uintptr_t)&ADC1_DR;
  DMA_CMAR(DMA1_BASE, kAdcDmaChannel) = (uint32_t)(uintptr_t)audio->adc_buffer;
  DMA_CNDTR(DMA1_BASE, kAdcDmaChannel) = audio->adc_count;
  DMA_CCR(DMA1_BASE, kAdcDmaChannel) = DMA_CCR_MINC | DMA_CCR_CIRC |
                                       DMA_CCR_PSIZE_16 | DMA_CCR_MSIZE_16 |
                                       DMA_CCR_PL_HIGH;
  DMA_CCR(DMA1_BASE, kAdcDmaChannel) |= DMA_CCR_EN;

  ADC1_ISR = ADC_ISR_EOC;
  audio->adc_dma_configured = true;
  return HAL_OK;
}

static hal_status_t start_adc_dma_conversion(hal_dma_pwm_audio_impl_t *audio) {
  if (audio->adc_count == 0u) {
    return HAL_OK;
  }
  if (!audio->adc_dma_configured) {
    const hal_status_t status = configure_adc_dma(audio);
    if (status != HAL_OK) {
      return status;
    }
  }
  if ((ADC1_CR & ADC_CR_ADSTART) == 0u) {
    ADC1_ISR = ADC_ISR_EOC;
    ADC1_CR |= ADC_CR_ADSTART;
  }
  return HAL_OK;
}

static void quiesce_adc_dma(hal_dma_pwm_audio_impl_t *audio) {
  if (audio == nullptr || !audio->adc_dma_configured) {
    return;
  }
  stop_adc_conversion();
  DMA_CCR(DMA1_BASE, kAdcDmaChannel) &= ~DMA_CCR_EN;
  audio->adc_dma_configured = false;
}

static void release_adc_dma(hal_dma_pwm_audio_impl_t *audio) {
  if (audio == nullptr || !audio->adc_claimed) {
    return;
  }
  quiesce_adc_dma(audio);
  DMAMUX_CCR(kAdcDmaChannel) = 0u;
  DMA_IFCR(DMA1_BASE) = DMA_IFCR_CLEAR_ALL(kAdcDmaChannel);
  ADC1_CFGR &= ~kAdcDmaCfgrMask;
  ADC1_ISR = ADC_ISR_EOC;
  audio->adc_dma_configured = false;
  stm32g474_adc_release_dma();
  audio->adc_claimed = false;
}

static bool configure_pwm_dma(hal_dma_pwm_audio_impl_t *audio) {
  const uint32_t compare_addr = jh_stm32_pwm_compare_address(&audio->pwm);
  const uint32_t request = jh_stm32_pwm_timer_dma_request(&audio->pwm);
  if (compare_addr == 0u || request == 0u ||
      audio->buffer_b != (audio->buffer_a + audio->block_size)) {
    return false;
  }

  jh_stm32_pwm_write_compare(&audio->pwm, audio->idle_value);
  jh_stm32_pwm_set_update_dma_request(&audio->pwm, false);

  DMA_CCR(DMA1_BASE, kPwmDmaChannel) &= ~DMA_CCR_EN;
  DMAMUX_CCR(kPwmDmaChannel) = request & DMAMUX_CCR_DMAREQ_ID_MASK;
  DMA_IFCR(DMA1_BASE) = DMA_IFCR_CLEAR_ALL(kPwmDmaChannel);
  DMA_CPAR(DMA1_BASE, kPwmDmaChannel) = compare_addr;
  DMA_CMAR(DMA1_BASE, kPwmDmaChannel) = (uint32_t)(uintptr_t)audio->buffer_a;
  DMA_CNDTR(DMA1_BASE, kPwmDmaChannel) = (uint32_t)audio->block_size * 2u;
  DMA_CCR(DMA1_BASE, kPwmDmaChannel) =
      DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PSIZE_16 |
      DMA_CCR_MSIZE_16 | DMA_CCR_PL_HIGH | DMA_CCR_HTIE | DMA_CCR_TCIE |
      DMA_CCR_TEIE;

  NVIC_IPR8(DMA1_Channel1_IRQn) = JH_NVIC_PRIO_TIMER;
  NVIC_ICPR(DMA1_Channel1_IRQn / 32u) = 1u << (DMA1_Channel1_IRQn % 32u);
  NVIC_ISER(DMA1_Channel1_IRQn / 32u) = 1u << (DMA1_Channel1_IRQn % 32u);
  return true;
}

static void reset_pwm_dma(hal_dma_pwm_audio_impl_t *audio) {
  DMA_CCR(DMA1_BASE, kPwmDmaChannel) &= ~DMA_CCR_EN;
  DMA_IFCR(DMA1_BASE) = DMA_IFCR_CLEAR_ALL(kPwmDmaChannel);
  DMA_CMAR(DMA1_BASE, kPwmDmaChannel) = (uint32_t)(uintptr_t)audio->buffer_a;
  DMA_CNDTR(DMA1_BASE, kPwmDmaChannel) = (uint32_t)audio->block_size * 2u;
}

static void disable_pwm_dma(void) {
  jh_stm32_pwm_set_update_dma_request(&s_irq_audio->pwm, false);
  DMA_CCR(DMA1_BASE, kPwmDmaChannel) &= ~DMA_CCR_EN;
}

extern "C" void DMA1_Channel1_IRQHandler(void) {
  hal_dma_pwm_audio_impl_t *audio = s_irq_audio;
  const uint32_t status = DMA_ISR(DMA1_BASE);
  const uint32_t clear = status & DMA_IFCR_CLEAR_ALL(kPwmDmaChannel);
  if (clear != 0u) {
    DMA_IFCR(DMA1_BASE) = clear;
  }
  const bool adc_transfer_error =
      audio != nullptr && audio->adc_count > 0u &&
      (status & DMA_FLAG_TEIF(kAdcDmaChannel)) != 0u;
  if (adc_transfer_error) {
    DMA_IFCR(DMA1_BASE) = DMA_IFCR_CLEAR_ALL(kAdcDmaChannel);
  }
  if (audio == nullptr || !audio->in_use ||
      !audio_state_load(&audio->running) || audio_state_load(&audio->paused)) {
    return;
  }

  if ((status & DMA_FLAG_TEIF(kPwmDmaChannel)) != 0u || adc_transfer_error) {
    disable_pwm_dma();
    quiesce_adc_dma(audio);
    jh_stm32_pwm_release_output(&audio->pwm);
    audio_state_store(&audio->running, false);
    return;
  }
  if ((status & DMA_FLAG_HTIF(kPwmDmaChannel)) != 0u && audio->cb != nullptr) {
    audio->cb(audio->user, audio->buffer_a, 0u);
  }
  if ((status & DMA_FLAG_TCIF(kPwmDmaChannel)) != 0u && audio->cb != nullptr) {
    audio->cb(audio->user, audio->buffer_b, 1u);
  }
}
#endif /* JH_STM32G474_HW */

static void release_pool_slot(hal_dma_pwm_audio_impl_t *audio) {
  pool_lock();
#ifdef JH_STM32G474_HW
  if (s_irq_audio == audio) {
    s_irq_audio = nullptr;
  }
#endif
  memset(audio, 0, sizeof(*audio));
  pool_unlock();
}

hal_status_t hal_dma_pwm_audio_create_ex(const hal_dma_pwm_audio_config_t *cfg,
                                         hal_dma_pwm_audio_t *out_audio) {
  if (out_audio == nullptr) {
    return HAL_EINVAL;
  }
  *out_audio = nullptr;
  if (!jh_hal_dma_pwm_audio_config_is_valid(cfg)) {
    return HAL_EINVAL;
  }
  for (uint8_t i = 0u; i < cfg->adc_count; ++i) {
    if (cfg->adc_pins[i] == cfg->pwm_pin ||
        !hal_adc_is_pin_supported(cfg->adc_pins[i])) {
      return HAL_EINVAL;
    }
  }

  hal_dma_pwm_audio_impl_t *audio = nullptr;
  pool_lock();
#ifdef JH_STM32G474_HW
  if (s_irq_audio != nullptr) {
    pool_unlock();
    return HAL_EBUSY;
  }
#endif
  for (uint8_t i = 0u; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    if (!s_pool[i].in_use) {
      audio = &s_pool[i];
      memset(audio, 0, sizeof(*audio));
      audio->in_use = true;
      break;
    }
  }
#ifdef JH_STM32G474_HW
  if (audio != nullptr) {
    s_irq_audio = audio;
  }
#endif
  pool_unlock();

  if (audio == nullptr) {
    return HAL_ENOMEM;
  }

  if (!jh_stm32_pwm_prepare_pin(cfg->pwm_pin, cfg->sample_rate_hz,
                                cfg->period_ticks, &audio->pwm)) {
    release_pool_slot(audio);
    return HAL_EINVAL;
  }

  audio->buffer_a = cfg->buffer_a;
  audio->buffer_b = cfg->buffer_b;
  audio->block_size = cfg->block_size;
  audio->idle_value = cfg->idle_value;
  audio->adc_pins = cfg->adc_pins;
  audio->adc_count = cfg->adc_count;
  audio->adc_buffer = cfg->adc_buffer;
  audio->cb = cfg->buffer_done_cb;
  audio->user = cfg->user;

#ifdef JH_STM32G474_HW
  RCC_AHB1ENR |= RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMAMUX1EN;
  (void)RCC_AHB1ENR;

  const hal_status_t adc_status = configure_adc_dma(audio);
  if (adc_status != HAL_OK) {
    jh_stm32_pwm_release_output(&audio->pwm);
    release_pool_slot(audio);
    return adc_status;
  }
  if (!configure_pwm_dma(audio)) {
    release_adc_dma(audio);
    jh_stm32_pwm_release_output(&audio->pwm);
    release_pool_slot(audio);
    return HAL_EIO;
  }
#endif

  *out_audio = audio;
  return HAL_OK;
}

bool hal_dma_pwm_audio_start(hal_dma_pwm_audio_t audio) {
  return hal_status_to_bool(hal_dma_pwm_audio_start_ex(audio));
}

hal_status_t hal_dma_pwm_audio_start_ex(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (audio_state_load(&audio->running) || audio_state_load(&audio->paused)) {
    return HAL_ESTATE;
  }

#ifdef JH_STM32G474_HW
  const hal_status_t adc_status = start_adc_dma_conversion(audio);
  if (adc_status != HAL_OK) {
    return adc_status;
  }
  reset_pwm_dma(audio);
  audio_state_store(&audio->running, true);
  audio_state_store(&audio->paused, false);
  DMA_CCR(DMA1_BASE, kPwmDmaChannel) |= DMA_CCR_EN;
  jh_stm32_pwm_set_update_dma_request(&audio->pwm, true);
  jh_stm32_pwm_start_output(&audio->pwm);
#else
  audio_state_store(&audio->running, true);
  audio_state_store(&audio->paused, false);
#endif
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_stop(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }

  audio_state_store(&audio->running, false);
  audio_state_store(&audio->paused, false);
#ifdef JH_STM32G474_HW
  jh_stm32_pwm_set_update_dma_request(&audio->pwm, false);
  DMA_CCR(DMA1_BASE, kPwmDmaChannel) &= ~DMA_CCR_EN;
  release_adc_dma(audio);
  jh_stm32_pwm_release_output(&audio->pwm);
#endif

  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_pause(hal_dma_pwm_audio_t audio,
                                     uint16_t idle_value) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (idle_value >= audio->pwm.period_ticks) {
    return HAL_EINVAL;
  }
  if (!audio_state_load(&audio->running) || audio_state_load(&audio->paused)) {
    return HAL_ESTATE;
  }
  audio->idle_value = idle_value;
  audio_state_store(&audio->running, false);
  audio_state_store(&audio->paused, true);

#ifdef JH_STM32G474_HW
  jh_stm32_pwm_set_update_dma_request(&audio->pwm, false);
  DMA_CCR(DMA1_BASE, kPwmDmaChannel) &= ~DMA_CCR_EN;
  release_adc_dma(audio);
  jh_stm32_pwm_write_compare(&audio->pwm, idle_value);
  jh_stm32_pwm_release_output(&audio->pwm);
#endif

  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_resume(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (!audio_state_load(&audio->paused) || audio_state_load(&audio->running)) {
    return HAL_ESTATE;
  }

#ifdef JH_STM32G474_HW
  const hal_status_t adc_status = start_adc_dma_conversion(audio);
  if (adc_status != HAL_OK) {
    return adc_status;
  }
  reset_pwm_dma(audio);
  audio_state_store(&audio->paused, false);
  audio_state_store(&audio->running, true);
  DMA_CCR(DMA1_BASE, kPwmDmaChannel) |= DMA_CCR_EN;
  jh_stm32_pwm_set_update_dma_request(&audio->pwm, true);
  jh_stm32_pwm_start_output(&audio->pwm);
#else
  audio_state_store(&audio->paused, false);
  audio_state_store(&audio->running, true);
#endif
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_destroy_ex(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }

  (void)hal_dma_pwm_audio_stop(audio);
  release_pool_slot(audio);
  return HAL_OK;
}

void hal_dma_pwm_audio_destroy(hal_dma_pwm_audio_t audio) {
  (void)hal_dma_pwm_audio_destroy_ex(audio);
}

bool hal_dma_pwm_audio_is_running(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use &&
         audio_state_load(&audio->running) && !audio_state_load(&audio->paused);
}

bool hal_dma_pwm_audio_is_paused(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use && audio_state_load(&audio->paused);
}

#endif /* HAL_ENABLE_DMA_PWM_AUDIO */
#endif /* HAL_TARGET_IS_STM32G474 */
