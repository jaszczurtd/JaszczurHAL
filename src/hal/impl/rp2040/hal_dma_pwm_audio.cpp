#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_DMA_PWM_AUDIO

#include "hal/audio/hal_dma_pwm_audio.h"
#include "hal/audio/hal_dma_pwm_audio_internal.h"
#include "hal/serial/hal_serial.h"
#include "rp2040_adc_shared.h"

#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>
#include <pico/mutex.h>
#include <stddef.h>
#include <string.h>

struct hal_dma_pwm_audio_impl_s {
  bool in_use;
  bool running;
  bool paused;
  uint8_t pwm_pin;
  uint8_t pwm_slice;
  uint8_t pwm_channel;
  uint16_t block_size;
  uint16_t idle_value;
  uint16_t *buffer_a;
  uint16_t *buffer_b;
  const uint8_t *adc_pins;
  uint8_t adc_count;
  volatile uint16_t *adc_buffer;
  bool adc_claimed;
  uint32_t buffer_a_read_addr;
  uint32_t buffer_b_read_addr;
  uint32_t adc_write_addr;
  uint32_t sample_rate_hz;
  uint32_t period_ticks;
  int dma_a;
  int dma_b;
  int dma_a_control;
  int dma_b_control;
  int dma_adc_sample;
  int dma_adc_control;
  hal_dma_pwm_audio_buffer_cb_t cb;
  void *user;
  bool configured; // Atomically published; keep last for reset_audio_slot().
};

static hal_dma_pwm_audio_impl_t s_pool[HAL_DMA_PWM_AUDIO_MAX_CHANNELS];
auto_init_mutex(s_pool_mutex);
static uint8_t s_irq_users = 0u;
static uint8_t s_irq_owner_core = UINT8_MAX;

static bool audio_state_load(const bool *state) {
  return __atomic_load_n(state, __ATOMIC_ACQUIRE);
}

static void audio_state_store(bool *state, bool value) {
  __atomic_store_n(state, value, __ATOMIC_RELEASE);
}

static void reset_audio_slot(hal_dma_pwm_audio_impl_t *audio) {
  audio_state_store(&audio->configured, false);
  memset(audio, 0, offsetof(hal_dma_pwm_audio_impl_t, configured));
}

static uint32_t dma_claim_mask(void) {
  uint32_t mask = 0u;

  for (uint32_t i = 0u; i < NUM_DMA_CHANNELS && i < 32u; ++i) {
    if (dma_channel_is_claimed(i)) {
      mask |= 1u << i;
    }
  }

  return mask;
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
    if (cfg->adc_pins[i] != (uint8_t)(26u + i) ||
        cfg->adc_pins[i] == cfg->pwm_pin) {
      return false;
    }
  }
  return true;
}

static bool pwm_timing_is_representable(uint32_t sample_rate_hz,
                                        uint32_t period_ticks) {
  const uint64_t target_hz = (uint64_t)sample_rate_hz * (uint64_t)period_ticks;
  const uint64_t clock_hz = (uint64_t)clock_get_hz(clk_sys);
  if (target_hz == 0u || clock_hz >= target_hz * 256u) {
    return false;
  }
  if (target_hz <= clock_hz) {
    return true;
  }
  const uint64_t nearest_unscaled_rate =
      (clock_hz + period_ticks / 2u) / period_ticks;
  return sample_rate_hz == nearest_unscaled_rate;
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
  if (audio->dma_a_control >= 0) {
    dma_channel_unclaim((uint)audio->dma_a_control);
    audio->dma_a_control = -1;
  }
  if (audio->dma_b_control >= 0) {
    dma_channel_unclaim((uint)audio->dma_b_control);
    audio->dma_b_control = -1;
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

static void set_dma_channel_enabled(int channel, bool enabled) {
  if (channel < 0) {
    return;
  }
  dma_channel_config config = dma_get_channel_config((uint)channel);
  channel_config_set_enable(&config, enabled);
  dma_channel_set_config((uint)channel, &config, false);
}

static void set_all_dma_channels_enabled(hal_dma_pwm_audio_impl_t *audio,
                                         bool enabled) {
  set_dma_channel_enabled(audio->dma_a, enabled);
  set_dma_channel_enabled(audio->dma_b, enabled);
  set_dma_channel_enabled(audio->dma_a_control, enabled);
  set_dma_channel_enabled(audio->dma_b_control, enabled);
  set_dma_channel_enabled(audio->dma_adc_sample, enabled);
  set_dma_channel_enabled(audio->dma_adc_control, enabled);
}

static uint32_t dma_channel_mask(int channel) {
  return channel >= 0 ? (1u << (uint)channel) : 0u;
}

static uint32_t audio_dma_mask(const hal_dma_pwm_audio_impl_t *audio) {
  return dma_channel_mask(audio->dma_a) | dma_channel_mask(audio->dma_b) |
         dma_channel_mask(audio->dma_a_control) |
         dma_channel_mask(audio->dma_b_control) |
         dma_channel_mask(audio->dma_adc_sample) |
         dma_channel_mask(audio->dma_adc_control);
}

static void set_audio_irq_enabled(hal_dma_pwm_audio_impl_t *audio,
                                  bool enabled) {
  const int channels[] = {audio->dma_a,          audio->dma_b,
                          audio->dma_a_control,  audio->dma_b_control,
                          audio->dma_adc_sample, audio->dma_adc_control};
  for (size_t index = 0u; index < sizeof(channels) / sizeof(channels[0]);
       ++index) {
    if (channels[index] >= 0) {
      dma_channel_set_irq1_enabled((uint)channels[index], enabled);
    }
  }
}

static void acknowledge_audio_irq(hal_dma_pwm_audio_impl_t *audio) {
  const uint32_t mask = audio_dma_mask(audio);
  if (mask != 0u) {
    dma_hw->ints1 = mask;
  }
}

static bool dma_channel_has_error(int channel) {
  return channel >= 0 && (dma_hw->ch[channel].ctrl_trig &
                          (DMA_CH0_CTRL_TRIG_READ_ERROR_BITS |
                           DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS)) != 0u;
}

static bool audio_dma_has_error(const hal_dma_pwm_audio_impl_t *audio) {
  return dma_channel_has_error(audio->dma_a) ||
         dma_channel_has_error(audio->dma_b) ||
         dma_channel_has_error(audio->dma_a_control) ||
         dma_channel_has_error(audio->dma_b_control) ||
         dma_channel_has_error(audio->dma_adc_sample) ||
         dma_channel_has_error(audio->dma_adc_control);
}

static void clear_dma_channel_error(int channel) {
  if (channel >= 0) {
    // DMA error flags are W1C. Preserve the current configuration and write
    // explicit ones through the non-triggering control alias.
    const uint32_t control = dma_hw->ch[channel].ctrl_trig;
    dma_hw->ch[channel].al1_ctrl = control | DMA_CH0_CTRL_TRIG_READ_ERROR_BITS |
                                   DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
  }
}

static void clear_audio_dma_errors(hal_dma_pwm_audio_impl_t *audio) {
  clear_dma_channel_error(audio->dma_a);
  clear_dma_channel_error(audio->dma_b);
  clear_dma_channel_error(audio->dma_a_control);
  clear_dma_channel_error(audio->dma_b_control);
  clear_dma_channel_error(audio->dma_adc_sample);
  clear_dma_channel_error(audio->dma_adc_control);
}

static void abort_dma_channel(int channel) {
  if (channel >= 0) {
    dma_channel_abort((uint)channel);
  }
}

static void quiesce_dma_channels(hal_dma_pwm_audio_impl_t *audio) {
  set_audio_irq_enabled(audio, false);
  set_all_dma_channels_enabled(audio, false);
  abort_dma_channel(audio->dma_a);
  abort_dma_channel(audio->dma_b);
  abort_dma_channel(audio->dma_a_control);
  abort_dma_channel(audio->dma_b_control);
  abort_dma_channel(audio->dma_adc_sample);
  abort_dma_channel(audio->dma_adc_control);
  acknowledge_audio_irq(audio);
  clear_audio_dma_errors(audio);
}

static void activate_dma_channels(hal_dma_pwm_audio_impl_t *audio) {
  acknowledge_audio_irq(audio);
  clear_audio_dma_errors(audio);
  set_all_dma_channels_enabled(audio, true);
  set_audio_irq_enabled(audio, true);
}

static void dma_irq1_handler(void) {
  uint32_t owned_mask = 0u;
  for (uint8_t i = 0u; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    if (audio_state_load(&s_pool[i].configured) && s_pool[i].in_use) {
      owned_mask |= audio_dma_mask(&s_pool[i]);
    }
  }
  const uint32_t ints = dma_hw->ints1 & owned_mask;
  if (ints == 0u) {
    return;
  }
  dma_hw->ints1 = ints;

  for (uint8_t i = 0u; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    hal_dma_pwm_audio_impl_t *audio = &s_pool[i];
    if (!audio_state_load(&audio->configured) || !audio->in_use ||
        (ints & audio_dma_mask(audio)) == 0u) {
      continue;
    }
    if (audio_dma_has_error(audio)) {
      audio_state_store(&audio->running, false);
      audio_state_store(&audio->paused, false);
      pwm_set_enabled(audio->pwm_slice, false);
      quiesce_dma_channels(audio);
      if (audio->adc_count > 0u) {
        adc_run(false);
      }
      pwm_set_gpio_level(audio->pwm_pin, audio->idle_value);
      clear_audio_dma_errors(audio);
      continue;
    }
    if (!audio->in_use || !audio_state_load(&audio->running) ||
        audio_state_load(&audio->paused)) {
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

static hal_status_t acquire_irq_handler(void) {
  const uint8_t core = (uint8_t)get_core_num();
  mutex_enter_blocking(&s_pool_mutex);
  if (s_irq_users > 0u && s_irq_owner_core != core) {
    mutex_exit(&s_pool_mutex);
    return HAL_EBUSY;
  }
  if (s_irq_users == 0u) {
    // The Pico SDK's shared-handler registration hard-asserts when its global
    // shared-handler pool is exhausted. Reserve DMA_IRQ_1 exclusively so a
    // resource conflict remains a recoverable HAL_EBUSY result.
    if (irq_has_handler(DMA_IRQ_1)) {
      mutex_exit(&s_pool_mutex);
      return HAL_EBUSY;
    }
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq1_handler);
    irq_set_enabled(DMA_IRQ_1, true);
    s_irq_owner_core = core;
  }
  ++s_irq_users;
  mutex_exit(&s_pool_mutex);
  return HAL_OK;
}

static hal_status_t release_irq_handler(void) {
  const uint8_t core = (uint8_t)get_core_num();
  mutex_enter_blocking(&s_pool_mutex);
  if (s_irq_users == 0u) {
    mutex_exit(&s_pool_mutex);
    return HAL_ESTATE;
  }
  if (s_irq_owner_core != core) {
    mutex_exit(&s_pool_mutex);
    return HAL_EBUSY;
  }
  --s_irq_users;
  if (s_irq_users == 0u) {
    irq_remove_handler(DMA_IRQ_1, dma_irq1_handler);
    irq_set_enabled(DMA_IRQ_1, false);
    s_irq_owner_core = UINT8_MAX;
  }
  mutex_exit(&s_pool_mutex);
  return HAL_OK;
}

static hal_status_t irq_owner_status(void) {
  const uint8_t core = (uint8_t)get_core_num();
  mutex_enter_blocking(&s_pool_mutex);
  const hal_status_t status =
      s_irq_users == 0u ? HAL_ESTATE
                        : (s_irq_owner_core == core ? HAL_OK : HAL_EBUSY);
  mutex_exit(&s_pool_mutex);
  return status;
}

static bool claim_channels(hal_dma_pwm_audio_impl_t *audio, bool adc_enabled) {
  audio->dma_a = dma_claim_unused_channel(false);
  audio->dma_b = dma_claim_unused_channel(false);
  audio->dma_a_control = dma_claim_unused_channel(false);
  audio->dma_b_control = dma_claim_unused_channel(false);
  if (audio->dma_a < 0 || audio->dma_b < 0 || audio->dma_a_control < 0 ||
      audio->dma_b_control < 0) {
    hal_deb("hal_dma_pwm_audio: claim failed pwm a=%d b=%d control-a=%d "
            "control-b=%d mask=0x%08lx channels=%u",
            audio->dma_a, audio->dma_b, audio->dma_a_control,
            audio->dma_b_control, (unsigned long)dma_claim_mask(),
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
  volatile uint16_t *const compare =
      ((volatile uint16_t *)&pwm_hw->slice[audio->pwm_slice].cc) +
      audio->pwm_channel;

  dma_channel_config cfg_a = dma_channel_get_default_config((uint)audio->dma_a);
  channel_config_set_transfer_data_size(&cfg_a, DMA_SIZE_16);
  channel_config_set_read_increment(&cfg_a, true);
  channel_config_set_dreq(&cfg_a, DREQ_PWM_WRAP0 + audio->pwm_slice);
  channel_config_set_ring(&cfg_a, false, 0u);
  channel_config_set_chain_to(&cfg_a, (uint)audio->dma_a_control);
  dma_channel_configure((uint)audio->dma_a, &cfg_a, compare, audio->buffer_a,
                        audio->block_size, false);
  dma_channel_set_irq1_enabled((uint)audio->dma_a, false);

  dma_channel_config cfg_b = dma_channel_get_default_config((uint)audio->dma_b);
  channel_config_set_transfer_data_size(&cfg_b, DMA_SIZE_16);
  channel_config_set_read_increment(&cfg_b, true);
  channel_config_set_dreq(&cfg_b, DREQ_PWM_WRAP0 + audio->pwm_slice);
  channel_config_set_ring(&cfg_b, false, 0u);
  channel_config_set_chain_to(&cfg_b, (uint)audio->dma_b_control);
  dma_channel_configure((uint)audio->dma_b, &cfg_b, compare, audio->buffer_b,
                        audio->block_size, false);
  dma_channel_set_irq1_enabled((uint)audio->dma_b, false);

  audio->buffer_a_read_addr = (uint32_t)(uintptr_t)audio->buffer_a;
  audio->buffer_b_read_addr = (uint32_t)(uintptr_t)audio->buffer_b;

  dma_channel_config control_a_cfg =
      dma_channel_get_default_config((uint)audio->dma_a_control);
  channel_config_set_transfer_data_size(&control_a_cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&control_a_cfg, false);
  channel_config_set_write_increment(&control_a_cfg, false);
  channel_config_set_irq_quiet(&control_a_cfg, true);
  channel_config_set_dreq(&control_a_cfg, DREQ_FORCE);
  dma_channel_configure((uint)audio->dma_a_control, &control_a_cfg,
                        &dma_hw->ch[audio->dma_b].al3_read_addr_trig,
                        &audio->buffer_b_read_addr, 1u, false);

  dma_channel_config control_b_cfg =
      dma_channel_get_default_config((uint)audio->dma_b_control);
  channel_config_set_transfer_data_size(&control_b_cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&control_b_cfg, false);
  channel_config_set_write_increment(&control_b_cfg, false);
  channel_config_set_irq_quiet(&control_b_cfg, true);
  channel_config_set_dreq(&control_b_cfg, DREQ_FORCE);
  dma_channel_configure((uint)audio->dma_b_control, &control_b_cfg,
                        &dma_hw->ch[audio->dma_a].al3_read_addr_trig,
                        &audio->buffer_a_read_addr, 1u, false);

  const uint32_t actual_hz =
      (uint32_t)((double)clk_hz / ((double)clkdiv * (double)period_ticks) +
                 0.5);
  hal_deb("hal_dma_pwm_audio: pwm clk=%lu rate=%lu actual=%lu period=%lu "
          "clkdiv=%.3f",
          (unsigned long)clk_hz, (unsigned long)audio->sample_rate_hz,
          (unsigned long)actual_hz, (unsigned long)period_ticks,
          (double)clkdiv);
}

static hal_status_t configure_adc_dma(hal_dma_pwm_audio_impl_t *audio) {
  if (audio->adc_count == 0u) {
    return HAL_OK;
  }
  if (!audio->adc_claimed) {
    const hal_status_t status = rp2040_adc_acquire_dma();
    if (status != HAL_OK) {
      return status;
    }
    audio->adc_claimed = true;
  }

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
  return HAL_OK;
}

static void release_adc_dma(hal_dma_pwm_audio_impl_t *audio) {
  if (audio->adc_count == 0u || !audio->adc_claimed) {
    return;
  }
  adc_run(false);
  adc_fifo_drain();
  adc_set_round_robin(0u);
  rp2040_adc_release_dma();
  audio->adc_claimed = false;
}

static void reset_dma_transfers(hal_dma_pwm_audio_impl_t *audio) {
  const uint32_t pwm_mask =
      (1u << (uint)audio->dma_a) | (1u << (uint)audio->dma_b);
  dma_hw->ints1 = pwm_mask;
  dma_channel_set_read_addr((uint)audio->dma_a, audio->buffer_a, false);
  dma_channel_set_trans_count((uint)audio->dma_a, audio->block_size, false);
  dma_channel_set_read_addr((uint)audio->dma_b, audio->buffer_b, false);
  dma_channel_set_trans_count((uint)audio->dma_b, audio->block_size, false);
  dma_channel_set_read_addr((uint)audio->dma_a_control,
                            &audio->buffer_b_read_addr, false);
  dma_channel_set_trans_count((uint)audio->dma_a_control, 1u, false);
  dma_channel_set_read_addr((uint)audio->dma_b_control,
                            &audio->buffer_a_read_addr, false);
  dma_channel_set_trans_count((uint)audio->dma_b_control, 1u, false);

  if (audio->adc_count == 0u) {
    return;
  }
  adc_fifo_drain();
  dma_channel_set_write_addr((uint)audio->dma_adc_sample,
                             (void *)audio->adc_buffer, false);
  dma_channel_set_trans_count((uint)audio->dma_adc_sample, audio->adc_count,
                              false);
  dma_channel_set_read_addr((uint)audio->dma_adc_control,
                            &audio->adc_write_addr, false);
  dma_channel_set_trans_count((uint)audio->dma_adc_control, 1u, false);
}

hal_status_t hal_dma_pwm_audio_create_ex(const hal_dma_pwm_audio_config_t *cfg,
                                         hal_dma_pwm_audio_t *out_audio) {
  if (out_audio == nullptr) {
    return HAL_EINVAL;
  }
  *out_audio = nullptr;
  if (!jh_hal_dma_pwm_audio_config_is_valid(cfg) ||
      cfg->pwm_pin >= NUM_BANK0_GPIOS ||
      !pwm_timing_is_representable(cfg->sample_rate_hz, cfg->period_ticks) ||
      !adc_pins_match_dacless_scan(cfg)) {
    return HAL_EINVAL;
  }

  hal_dma_pwm_audio_impl_t *audio = nullptr;
  const uint8_t pwm_slice = (uint8_t)pwm_gpio_to_slice_num(cfg->pwm_pin);
  const uint8_t pwm_channel = (uint8_t)pwm_gpio_to_channel(cfg->pwm_pin);
  mutex_enter_blocking(&s_pool_mutex);
  for (uint8_t i = 0u; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    if (!s_pool[i].in_use) {
      continue;
    }
    if (s_pool[i].pwm_slice == pwm_slice ||
        (cfg->adc_count > 0u && s_pool[i].adc_count > 0u)) {
      mutex_exit(&s_pool_mutex);
      return HAL_EBUSY;
    }
  }
  for (uint8_t i = 0u; i < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++i) {
    if (!s_pool[i].in_use) {
      audio = &s_pool[i];
      reset_audio_slot(audio);
      audio->dma_a = -1;
      audio->dma_b = -1;
      audio->dma_a_control = -1;
      audio->dma_b_control = -1;
      audio->dma_adc_sample = -1;
      audio->dma_adc_control = -1;
      audio->pwm_pin = cfg->pwm_pin;
      audio->pwm_slice = pwm_slice;
      audio->pwm_channel = pwm_channel;
      audio->adc_count = cfg->adc_count;
      audio->in_use = true;
      break;
    }
  }
  mutex_exit(&s_pool_mutex);

  if (audio == nullptr) {
    return HAL_ENOMEM;
  }

  audio->pwm_pin = cfg->pwm_pin;
  audio->pwm_slice = pwm_slice;
  audio->pwm_channel = pwm_channel;
  audio->block_size = cfg->block_size;
  audio->idle_value = cfg->idle_value;
  audio->sample_rate_hz = cfg->sample_rate_hz;
  audio->period_ticks = cfg->period_ticks;
  audio->buffer_a = cfg->buffer_a;
  audio->buffer_b = cfg->buffer_b;
  audio->adc_pins = cfg->adc_pins;
  audio->adc_count = cfg->adc_count;
  audio->adc_buffer = cfg->adc_buffer;
  audio->cb = cfg->buffer_done_cb;
  audio->user = cfg->user;

  if (!claim_channels(audio, cfg->adc_count > 0u)) {
    mutex_enter_blocking(&s_pool_mutex);
    reset_audio_slot(audio);
    mutex_exit(&s_pool_mutex);
    return HAL_ENOMEM;
  }

  const hal_status_t irq_status = acquire_irq_handler();
  if (irq_status != HAL_OK) {
    release_claimed_channels(audio);
    mutex_enter_blocking(&s_pool_mutex);
    reset_audio_slot(audio);
    mutex_exit(&s_pool_mutex);
    return irq_status;
  }
  const hal_status_t adc_status = configure_adc_dma(audio);
  if (adc_status != HAL_OK) {
    (void)release_irq_handler();
    release_claimed_channels(audio);
    mutex_enter_blocking(&s_pool_mutex);
    reset_audio_slot(audio);
    mutex_exit(&s_pool_mutex);
    return adc_status;
  }
  configure_pwm_dma(audio, cfg->period_ticks);
  audio_state_store(&audio->configured, true);
  *out_audio = audio;
  return HAL_OK;
}

bool hal_dma_pwm_audio_start(hal_dma_pwm_audio_t audio) {
  return hal_status_to_bool(hal_dma_pwm_audio_start_ex(audio));
}

hal_status_t hal_dma_pwm_audio_start_ex(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use ||
      !audio_state_load(&audio->configured)) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (audio_state_load(&audio->running) || audio_state_load(&audio->paused)) {
    return HAL_ESTATE;
  }
  const hal_status_t owner_status = irq_owner_status();
  if (owner_status != HAL_OK) {
    return owner_status;
  }

  const hal_status_t adc_status = configure_adc_dma(audio);
  if (adc_status != HAL_OK) {
    return adc_status;
  }
  reset_dma_transfers(audio);
  activate_dma_channels(audio);
  if (audio->adc_count > 0u) {
    dma_channel_start((uint)audio->dma_adc_sample);
    adc_run(true);
  }

  audio_state_store(&audio->running, true);
  audio_state_store(&audio->paused, false);
  pwm_set_enabled(audio->pwm_slice, true);
  dma_channel_start((uint)audio->dma_a);
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_stop(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }

  audio_state_store(&audio->running, false);
  audio_state_store(&audio->paused, false);
  pwm_set_enabled(audio->pwm_slice, false);
  quiesce_dma_channels(audio);
  if (audio->adc_count > 0u) {
    adc_run(false);
  }
  release_adc_dma(audio);
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_pause(hal_dma_pwm_audio_t audio,
                                     uint16_t idle_value) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (idle_value >= audio->period_ticks) {
    return HAL_EINVAL;
  }
  if (!audio_state_load(&audio->running) || audio_state_load(&audio->paused)) {
    return HAL_ESTATE;
  }
  const hal_status_t owner_status = irq_owner_status();
  if (owner_status != HAL_OK) {
    return owner_status;
  }
  audio->idle_value = idle_value;
  audio_state_store(&audio->running, false);
  audio_state_store(&audio->paused, true);
  pwm_set_enabled(audio->pwm_slice, false);
  quiesce_dma_channels(audio);
  if (audio->adc_count > 0u) {
    adc_run(false);
  }
  release_adc_dma(audio);
  pwm_set_gpio_level(audio->pwm_pin, idle_value);
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_resume(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (!audio_state_load(&audio->paused) || audio_state_load(&audio->running)) {
    return HAL_ESTATE;
  }
  const hal_status_t owner_status = irq_owner_status();
  if (owner_status != HAL_OK) {
    return owner_status;
  }
  const hal_status_t adc_status = configure_adc_dma(audio);
  if (adc_status != HAL_OK) {
    return adc_status;
  }
  reset_dma_transfers(audio);
  activate_dma_channels(audio);
  if (audio->adc_count > 0u) {
    dma_channel_start((uint)audio->dma_adc_sample);
    adc_run(true);
  }
  audio_state_store(&audio->paused, false);
  audio_state_store(&audio->running, true);
  pwm_set_enabled(audio->pwm_slice, true);
  dma_channel_start((uint)audio->dma_a);
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_destroy_ex(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use ||
      !audio_state_load(&audio->configured)) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  const hal_status_t owner_status = irq_owner_status();
  if (owner_status != HAL_OK) {
    return owner_status;
  }

  const hal_status_t stop_status = hal_dma_pwm_audio_stop(audio);
  if (stop_status != HAL_OK) {
    return stop_status;
  }
  audio_state_store(&audio->configured, false);
  const hal_status_t irq_status = release_irq_handler();
  if (irq_status != HAL_OK) {
    return irq_status;
  }
  release_claimed_channels(audio);
  mutex_enter_blocking(&s_pool_mutex);
  reset_audio_slot(audio);
  mutex_exit(&s_pool_mutex);
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
#endif /* HAL_TARGET_IS_RP */
