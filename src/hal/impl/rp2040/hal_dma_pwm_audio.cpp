#include "../../hal_target.h"
#if HAL_TARGET_IS_RP

#include "../../hal_config.h"
#ifdef HAL_ENABLE_DMA_PWM_AUDIO

#include "../../hal_dma_pwm_audio.h"
#include "../../hal_serial.h"

#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>
#include <string.h>

struct hal_dma_pwm_audio_impl_s {
  bool in_use;
  bool configured;
  bool running;
  bool paused;
  uint8_t pwm_pin;
  uint8_t pwm_slice;
  uint16_t block_size;
  uint16_t idle_value;
  uint16_t *buffer_a;
  uint16_t *buffer_b;
  const uint8_t *adc_pins;
  uint8_t adc_count;
  volatile uint16_t *adc_buffer;
  uint32_t adc_write_addr;
  uint32_t sample_rate_hz;
  int dma_a;
  int dma_b;
  int dma_adc_sample;
  int dma_adc_control;
  hal_dma_pwm_audio_buffer_cb_t cb;
  void *user;
};

static hal_dma_pwm_audio_impl_t s_pool[HAL_DMA_PWM_AUDIO_MAX_CHANNELS];
static bool s_irq_installed = false;

static uint32_t dma_claim_mask(void) {
  uint32_t mask = 0u;

  for (uint32_t i = 0u; i < NUM_DMA_CHANNELS && i < 32u; ++i) {
    if (dma_channel_is_claimed(i)) {
      mask |= 1u << i;
    }
  }

  return mask;
}

static uint8_t ring_bits_for_bytes(uint32_t bytes) {
  if (bytes == 0u) {
    return 0u;
  }
  return (uint8_t)(31u - (uint32_t)__builtin_clz(bytes));
}

static bool adc_pins_match_dacless_scan(const hal_dma_pwm_audio_config_t *cfg) {
  if (cfg->adc_count == 0u) {
    return true;
  }
  if (cfg->adc_pins == nullptr || cfg->adc_buffer == nullptr ||
      cfg->adc_count > 4u) {
    return false;
  }
  for (uint8_t i = 0u; i < cfg->adc_count; ++i) {
    if (cfg->adc_pins[i] != (uint8_t)(26u + i)) {
      return false;
    }
  }
  return true;
}

static void release_claimed_channels(hal_dma_pwm_audio_impl_t *audio) {
  if (audio->dma_a >= 0) {
    dma_channel_unclaim((uint)audio->dma_a);
    audio->dma_a = -1;
  }
  if (audio->dma_b >= 0) {
    dma_channel_unclaim((uint)audio->dma_b);
    audio->dma_b = -1;
  }
  if (audio->dma_adc_sample >= 0) {
    dma_channel_unclaim((uint)audio->dma_adc_sample);
    audio->dma_adc_sample = -1;
  }
  if (audio->dma_adc_control >= 0) {
    dma_channel_unclaim((uint)audio->dma_adc_control);
    audio->dma_adc_control = -1;
  }
}

static void dma_irq1_handler(void) {
  const uint32_t ints = dma_hw->ints1;
  dma_hw->ints1 = ints;

  for (uint8_t i = 0u; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    hal_dma_pwm_audio_impl_t *audio = &s_pool[i];
    if (!audio->in_use || !audio->running) {
      continue;
    }

    if (audio->dma_a >= 0 && (ints & (1u << (uint)audio->dma_a)) != 0u) {
      if (audio->cb != nullptr) {
        audio->cb(audio->user, audio->buffer_a, 0u);
      }
    }
    if (audio->dma_b >= 0 && (ints & (1u << (uint)audio->dma_b)) != 0u) {
      if (audio->cb != nullptr) {
        audio->cb(audio->user, audio->buffer_b, 1u);
      }
    }
  }
}

static bool ensure_irq_installed(void) {
  if (!s_irq_installed) {
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq1_handler);
    irq_set_enabled(DMA_IRQ_1, true);
    s_irq_installed = true;
  }
  return true;
}

static bool claim_channels(hal_dma_pwm_audio_impl_t *audio, bool adc_enabled) {
  audio->dma_a = dma_claim_unused_channel(false);
  audio->dma_b = dma_claim_unused_channel(false);
  if (audio->dma_a < 0 || audio->dma_b < 0) {
    hal_deb("hal_dma_pwm_audio: claim failed pwm a=%d b=%d mask=0x%08lx "
            "channels=%u",
            audio->dma_a, audio->dma_b, (unsigned long)dma_claim_mask(),
            (unsigned int)NUM_DMA_CHANNELS);
    release_claimed_channels(audio);
    return false;
  }

  if (adc_enabled) {
    audio->dma_adc_sample = dma_claim_unused_channel(false);
    audio->dma_adc_control = dma_claim_unused_channel(false);
    if (audio->dma_adc_sample < 0 || audio->dma_adc_control < 0) {
      hal_deb("hal_dma_pwm_audio: claim failed adc sample=%d control=%d "
              "mask=0x%08lx channels=%u",
              audio->dma_adc_sample, audio->dma_adc_control,
              (unsigned long)dma_claim_mask(), (unsigned int)NUM_DMA_CHANNELS);
      release_claimed_channels(audio);
      return false;
    }
  }

  return true;
}

static void configure_pwm_dma(hal_dma_pwm_audio_impl_t *audio,
                              uint32_t period_ticks) {
  gpio_set_function(audio->pwm_pin, GPIO_FUNC_PWM);
  const uint32_t clk_hz = clock_get_hz(clk_sys);
  float clkdiv = (float)((double)clk_hz / ((double)audio->sample_rate_hz *
                                           (double)period_ticks));
  if (clkdiv < 1.0f) {
    clkdiv = 1.0f;
  }
  pwm_set_clkdiv(audio->pwm_slice, clkdiv);
  pwm_set_wrap(audio->pwm_slice, period_ticks - 1u);
  pwm_set_gpio_level(audio->pwm_pin, audio->idle_value);
  pwm_set_irq_enabled(audio->pwm_slice, true);

  const uint8_t ring_bits =
      ring_bits_for_bytes((uint32_t)audio->block_size * sizeof(uint16_t));

  dma_channel_config cfg_a = dma_channel_get_default_config((uint)audio->dma_a);
  channel_config_set_transfer_data_size(&cfg_a, DMA_SIZE_16);
  channel_config_set_read_increment(&cfg_a, true);
  channel_config_set_dreq(&cfg_a, DREQ_PWM_WRAP0 + audio->pwm_slice);
  channel_config_set_ring(&cfg_a, false, ring_bits);
  channel_config_set_chain_to(&cfg_a, (uint)audio->dma_b);
  dma_channel_configure((uint)audio->dma_a, &cfg_a,
                        &pwm_hw->slice[audio->pwm_slice].cc, audio->buffer_a,
                        audio->block_size, false);
  dma_channel_set_irq1_enabled((uint)audio->dma_a, true);

  dma_channel_config cfg_b = dma_channel_get_default_config((uint)audio->dma_b);
  channel_config_set_transfer_data_size(&cfg_b, DMA_SIZE_16);
  channel_config_set_read_increment(&cfg_b, true);
  channel_config_set_dreq(&cfg_b, DREQ_PWM_WRAP0 + audio->pwm_slice);
  channel_config_set_ring(&cfg_b, false, ring_bits);
  channel_config_set_chain_to(&cfg_b, (uint)audio->dma_a);
  dma_channel_configure((uint)audio->dma_b, &cfg_b,
                        &pwm_hw->slice[audio->pwm_slice].cc, audio->buffer_b,
                        audio->block_size, false);
  dma_channel_set_irq1_enabled((uint)audio->dma_b, true);

  const uint32_t actual_hz =
      (uint32_t)((double)clk_hz / ((double)clkdiv * (double)period_ticks) +
                 0.5);
  hal_deb("hal_dma_pwm_audio: pwm clk=%lu rate=%lu actual=%lu period=%lu "
          "clkdiv=%.3f",
          (unsigned long)clk_hz, (unsigned long)audio->sample_rate_hz,
          (unsigned long)actual_hz, (unsigned long)period_ticks,
          (double)clkdiv);
}

static void configure_adc_dma(hal_dma_pwm_audio_impl_t *audio) {
  if (audio->adc_count == 0u) {
    return;
  }

  adc_init();
  for (uint8_t i = 0u; i < audio->adc_count; ++i) {
    adc_gpio_init((uint)audio->adc_pins[i]);
  }
  adc_set_clkdiv(1.0f);
  adc_set_round_robin((1u << audio->adc_count) - 1u);
  adc_select_input(0u);
  adc_fifo_setup(true, true, audio->adc_count, false, false);
  adc_fifo_drain();

  audio->adc_write_addr = (uint32_t)(uintptr_t)audio->adc_buffer;

  dma_channel_config sample_cfg =
      dma_channel_get_default_config((uint)audio->dma_adc_sample);
  channel_config_set_transfer_data_size(&sample_cfg, DMA_SIZE_16);
  channel_config_set_read_increment(&sample_cfg, false);
  channel_config_set_write_increment(&sample_cfg, true);
  channel_config_set_irq_quiet(&sample_cfg, true);
  channel_config_set_dreq(&sample_cfg, DREQ_ADC);
  channel_config_set_chain_to(&sample_cfg, (uint)audio->dma_adc_control);
  dma_channel_configure((uint)audio->dma_adc_sample, &sample_cfg,
                        (void *)audio->adc_buffer, &adc_hw->fifo,
                        audio->adc_count, false);

  dma_channel_config control_cfg =
      dma_channel_get_default_config((uint)audio->dma_adc_control);
  channel_config_set_transfer_data_size(&control_cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&control_cfg, false);
  channel_config_set_write_increment(&control_cfg, false);
  channel_config_set_irq_quiet(&control_cfg, true);
  channel_config_set_dreq(&control_cfg, DREQ_FORCE);
  dma_channel_configure((uint)audio->dma_adc_control, &control_cfg,
                        &dma_hw->ch[audio->dma_adc_sample].al2_write_addr_trig,
                        &audio->adc_write_addr, 1u, false);
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
  if (cfg == nullptr || cfg->buffer_a == nullptr || cfg->buffer_b == nullptr ||
      cfg->block_size == 0u || cfg->period_ticks == 0u ||
      cfg->sample_rate_hz == 0u || !adc_pins_match_dacless_scan(cfg)) {
    return HAL_EINVAL;
  }

  hal_dma_pwm_audio_impl_t *audio = nullptr;
  const uint32_t irq_state = save_and_disable_interrupts();
  for (uint8_t i = 0u; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    if (!s_pool[i].in_use) {
      audio = &s_pool[i];
      memset(audio, 0, sizeof(*audio));
      audio->in_use = true;
      audio->dma_a = -1;
      audio->dma_b = -1;
      audio->dma_adc_sample = -1;
      audio->dma_adc_control = -1;
      break;
    }
  }
  restore_interrupts(irq_state);

  if (audio == nullptr) {
    HAL_ASSERT(false, "hal_dma_pwm_audio: RP2040 pool exhausted");
    return HAL_ENOMEM;
  }

  audio->pwm_pin = cfg->pwm_pin;
  audio->pwm_slice = (uint8_t)pwm_gpio_to_slice_num(cfg->pwm_pin);
  audio->block_size = cfg->block_size;
  audio->idle_value = cfg->idle_value;
  audio->sample_rate_hz = cfg->sample_rate_hz;
  audio->buffer_a = cfg->buffer_a;
  audio->buffer_b = cfg->buffer_b;
  audio->adc_pins = cfg->adc_pins;
  audio->adc_count = cfg->adc_count;
  audio->adc_buffer = cfg->adc_buffer;
  audio->cb = cfg->buffer_done_cb;
  audio->user = cfg->user;

  if (!claim_channels(audio, cfg->adc_count > 0u)) {
    audio->in_use = false;
    return HAL_EIO;
  }

  ensure_irq_installed();
  configure_pwm_dma(audio, cfg->period_ticks);
  configure_adc_dma(audio);
  audio->configured = true;
  *out_audio = audio;
  return HAL_OK;
}

bool hal_dma_pwm_audio_start(hal_dma_pwm_audio_t audio) {
  return hal_status_to_bool(hal_dma_pwm_audio_start_ex(audio));
}

hal_status_t hal_dma_pwm_audio_start_ex(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use || !audio->configured) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }

  if (audio->adc_count > 0u) {
    dma_channel_start((uint)audio->dma_adc_sample);
    dma_channel_start((uint)audio->dma_adc_control);
    adc_run(true);
  }

  audio->running = true;
  audio->paused = false;
  pwm_set_enabled(audio->pwm_slice, true);
  dma_channel_start((uint)audio->dma_a);
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_stop(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }

  pwm_set_enabled(audio->pwm_slice, false);
  if (audio->dma_a >= 0) {
    dma_channel_abort((uint)audio->dma_a);
  }
  if (audio->dma_b >= 0) {
    dma_channel_abort((uint)audio->dma_b);
  }
  if (audio->dma_adc_sample >= 0) {
    dma_channel_abort((uint)audio->dma_adc_sample);
  }
  if (audio->dma_adc_control >= 0) {
    dma_channel_abort((uint)audio->dma_adc_control);
  }
  if (audio->adc_count > 0u) {
    adc_run(false);
  }
  audio->running = false;
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_pause(hal_dma_pwm_audio_t audio,
                                     uint16_t idle_value) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  audio->idle_value = idle_value;
  pwm_set_gpio_level(audio->pwm_pin, idle_value);
  pwm_set_enabled(audio->pwm_slice, false);
  audio->paused = true;
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_resume(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  audio->paused = false;
  audio->running = true;
  pwm_set_enabled(audio->pwm_slice, true);
  return HAL_OK;
}

void hal_dma_pwm_audio_destroy(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr) {
    return;
  }

  hal_dma_pwm_audio_stop(audio);
  dma_channel_set_irq1_enabled((uint)audio->dma_a, false);
  dma_channel_set_irq1_enabled((uint)audio->dma_b, false);
  release_claimed_channels(audio);
  memset(audio, 0, sizeof(*audio));
}

bool hal_dma_pwm_audio_is_running(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use && audio->running && !audio->paused;
}

bool hal_dma_pwm_audio_is_paused(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use && audio->paused;
}

#endif /* HAL_ENABLE_DMA_PWM_AUDIO */
#endif /* HAL_TARGET_IS_RP */
