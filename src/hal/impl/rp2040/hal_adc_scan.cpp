#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_ADC_SCAN
#include "hal/analog/jh_adc_scan_backend.h"
#include "hal/analog/jh_adc_scan_ring.h"
#include "hal/system/hal_system.h"
#include "rp2040_adc_shared.h"

#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/sync.h>

namespace {

// Two DMA channels chained into a ring: each fills one half of the buffer,
// triggers the other and raises DMA_IRQ_0, where it is re-armed for its next
// turn. DACless keeps DMA_IRQ_1; both paths share the converter ownership
// flag in rp2040_adc_shared, so they exclude each other instead of colliding.
constexpr uint32_t kMinConversionCycles = 96u;
constexpr uint32_t kMaxConversionCycles = 65536u;
constexpr uint8_t kTemperatureInput = 4u;
constexpr uint8_t kInputs = 5u;

struct scan_state_t {
  hal_adc_scan_config_t config;
  uint8_t count;
  uint8_t input_mask;
  uint8_t input_of_position[kInputs];
  int channel[2];
  uint32_t samples_per_block;
  bool adc_owned;
  bool irq_owned;
  bool started;
  volatile uint32_t sequence;
  volatile uint8_t completed;
  volatile uint32_t completed_us;
  volatile uint32_t marker;
};

scan_state_t s = {};

uint16_t *half(uint8_t index) {
  return s.config.buffer + ((uint32_t)index * s.samples_per_block);
}

void dma_irq0_handler(void) {
  const uint32_t ints = dma_hw->ints0;
  for (uint8_t b = 0u; b < 2u; ++b) {
    if (s.channel[b] < 0 || (ints & (1u << (uint)s.channel[b])) == 0u) {
      continue;
    }
    dma_hw->ints0 = 1u << (uint)s.channel[b];
    // The other channel runs now; this one restarts when that one chains.
    dma_channel_set_write_addr((uint)s.channel[b], half(b), false);
    dma_channel_set_trans_count((uint)s.channel[b], s.samples_per_block, false);
    s.marker =
        s.config.marker != NULL ? s.config.marker(s.config.marker_user) : 0u;
    s.completed_us = hal_micros();
    s.completed = b;
    s.sequence = s.sequence + 1u;
  }
}

bool scan_reader(uint8_t input, uint16_t *raw) {
  for (uint8_t position = 0u; position < s.count; ++position) {
    if (s.input_of_position[position] == input) {
      return jh_adc_scan_latest(position, raw) == HAL_OK;
    }
  }
  return false;
}

bool input_for_pin(uint8_t pin, uint8_t *input) {
  if (pin == HAL_ADC_SCAN_PIN_TEMPERATURE) {
    *input = kTemperatureInput;
    return true;
  }
  if (pin < 26u || pin > 29u) {
    return false;
  }
  *input = (uint8_t)(pin - 26u);
  return true;
}

void configure_channel(int channel, int next, uint16_t *target) {
  dma_channel_config cfg = dma_channel_get_default_config((uint)channel);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, true);
  channel_config_set_dreq(&cfg, DREQ_ADC);
  channel_config_set_chain_to(&cfg, (uint)next);
  channel_config_set_irq_quiet(&cfg, false);
  dma_channel_configure((uint)channel, &cfg, target, &adc_hw->fifo,
                        s.samples_per_block, false);
  dma_channel_set_irq0_enabled((uint)channel, true);
}

} // namespace

hal_status_t jh_adc_scan_start(const hal_adc_scan_config_t *config,
                               uint8_t *positions, uint32_t *frame_period_ns) {
  if (s.started || s.adc_owned || s.irq_owned) {
    return HAL_EBUSY;
  }
  const uint32_t adc_hz = clock_get_hz(clk_adc);
  if (adc_hz == 0u) {
    return HAL_EHW;
  }
  // Round the requested period up to whole converter cycles.
  const uint64_t cycles64 =
      ((uint64_t)config->conversion_period_ns * adc_hz + 999999999ull) /
      1000000000ull;
  if (cycles64 > kMaxConversionCycles) {
    return HAL_EUNSUPPORTED;
  }
  const uint32_t cycles = cycles64 < kMinConversionCycles ? kMinConversionCycles
                                                          : (uint32_t)cycles64;

  // The FIFO delivers the round-robin inputs in ascending order whatever the
  // configured order, so positions are ranks among the selected inputs.
  uint8_t inputs[HAL_ADC_SCAN_MAX_PINS] = {};
  uint8_t mask = 0u;
  for (uint8_t i = 0u; i < config->pin_count; ++i) {
    if (!input_for_pin(config->pins[i], &inputs[i])) {
      return HAL_EINVAL;
    }
    mask |= (uint8_t)(1u << inputs[i]);
  }
  s.config = *config;
  s.count = config->pin_count;
  s.input_mask = mask;
  uint8_t position = 0u;
  for (uint8_t input = 0u; input < kInputs; ++input) {
    if ((mask & (uint8_t)(1u << input)) == 0u) {
      continue;
    }
    s.input_of_position[position] = input;
    for (uint8_t i = 0u; i < config->pin_count; ++i) {
      if (inputs[i] == input) {
        positions[i] = position;
      }
    }
    ++position;
  }
  s.samples_per_block = config->block_frames * (uint32_t)config->pin_count;
  s.channel[0] = -1;
  s.channel[1] = -1;
  s.sequence = 0u;
  s.completed = 0u;
  s.completed_us = 0u;
  s.marker = 0u;

  const hal_status_t owned = rp2040_adc_acquire_dma();
  if (owned != HAL_OK) {
    return owned;
  }
  s.adc_owned = true;

  s.channel[0] = dma_claim_unused_channel(false);
  s.channel[1] = dma_claim_unused_channel(false);
  if (s.channel[0] < 0 || s.channel[1] < 0) {
    return HAL_ENOMEM;
  }
  // The SDK's shared-handler pool hard-asserts when exhausted; an exclusive
  // handler keeps a conflict a recoverable result, the same way DACless
  // treats DMA_IRQ_1.
  if (irq_has_handler(DMA_IRQ_0)) {
    return HAL_EBUSY;
  }
  irq_set_exclusive_handler(DMA_IRQ_0, dma_irq0_handler);
  irq_set_enabled(DMA_IRQ_0, true);
  s.irq_owned = true;

  for (uint8_t input = 0u; input < kTemperatureInput; ++input) {
    if ((mask & (uint8_t)(1u << input)) != 0u) {
      adc_gpio_init(26u + input);
    }
  }
  if ((mask & (uint8_t)(1u << kTemperatureInput)) != 0u) {
    adc_set_temp_sensor_enabled(true);
  }
  adc_run(false);
  adc_fifo_drain();
  adc_set_clkdiv((float)cycles - 1.0f);
  adc_set_round_robin(mask);
  adc_select_input(s.input_of_position[0]);
  adc_fifo_setup(true, true, 1u, false, false);
  adc_fifo_drain();

  dma_hw->ints0 = (1u << (uint)s.channel[0]) | (1u << (uint)s.channel[1]);
  configure_channel(s.channel[0], s.channel[1], half(0u));
  configure_channel(s.channel[1], s.channel[0], half(1u));
  rp2040_adc_set_scan_reader(scan_reader);
  dma_channel_start((uint)s.channel[0]);
  adc_run(true);
  s.started = true;

  const uint64_t frame_cycles = (uint64_t)cycles * (uint64_t)config->pin_count;
  *frame_period_ns = (uint32_t)((frame_cycles * 1000000000ull) / adc_hz);
  return HAL_OK;
}

hal_status_t jh_adc_scan_stop(void) {
  if (s.started) {
    adc_run(false);
    rp2040_adc_set_scan_reader(NULL);
  }
  for (uint8_t b = 0u; b < 2u; ++b) {
    if (s.channel[b] >= 0) {
      dma_channel_set_irq0_enabled((uint)s.channel[b], false);
      dma_channel_abort((uint)s.channel[b]);
      dma_hw->ints0 = 1u << (uint)s.channel[b];
    }
  }
  if (s.irq_owned) {
    irq_set_enabled(DMA_IRQ_0, false);
    irq_remove_handler(DMA_IRQ_0, dma_irq0_handler);
    s.irq_owned = false;
  }
  for (uint8_t b = 0u; b < 2u; ++b) {
    if (s.channel[b] >= 0) {
      dma_channel_unclaim((uint)s.channel[b]);
      s.channel[b] = -1;
    }
  }
  if (s.started) {
    adc_fifo_drain();
    adc_set_round_robin(0u);
    if ((s.input_mask & (uint8_t)(1u << kTemperatureInput)) != 0u) {
      adc_set_temp_sensor_enabled(false);
    }
    s.started = false;
  }
  if (s.adc_owned) {
    rp2040_adc_release_dma();
    s.adc_owned = false;
  }
  return HAL_OK;
}

bool jh_adc_scan_completed(hal_adc_scan_block_t *block) {
  const uint32_t saved = save_and_disable_interrupts();
  const uint32_t sequence = s.sequence;
  const uint8_t completed = s.completed;
  const uint32_t completed_us = s.completed_us;
  const uint32_t marker = s.marker;
  restore_interrupts(saved);
  if (!s.started || sequence == 0u) {
    return false;
  }
  jh_adc_scan_describe(block, half(completed), s.config.block_frames, s.count,
                       sequence, completed_us, marker);
  return true;
}

hal_status_t jh_adc_scan_latest(uint8_t position, uint16_t *raw) {
  if (!s.started || position >= s.count) {
    return HAL_ESTATE;
  }
  // Exactly one channel is busy between block boundaries; its write pointer
  // says how many complete frames the block in progress already holds. Right
  // after the chain the other half is the complete one even before its
  // interrupt has run, so the frame choice never leans on that bookkeeping.
  uint8_t busy = UINT8_MAX;
  uint32_t written = 0u;
  for (uint8_t b = 0u; b < 2u; ++b) {
    if (s.channel[b] < 0 || !dma_channel_is_busy((uint)s.channel[b])) {
      continue;
    }
    const uintptr_t base = (uintptr_t)half(b);
    const uintptr_t write =
        (uintptr_t)dma_hw->ch[(uint)s.channel[b]].write_addr;
    if (write >= base) {
      busy = b;
      written = (uint32_t)((write - base) / sizeof(uint16_t));
      break;
    }
  }
  const uint32_t saved = save_and_disable_interrupts();
  const uint32_t sequence = s.sequence;
  const uint8_t completed = s.completed;
  restore_interrupts(saved);
  uint8_t index = 0u;
  uint32_t frame = 0u;
  if (!jh_adc_scan_latest_frame(busy, written, s.count, s.config.block_frames,
                                sequence, completed, &index, &frame)) {
    return HAL_EAGAIN;
  }
  *raw = half(index)[(frame * s.count) + position];
  return HAL_OK;
}

#endif // HAL_ENABLE_ADC_SCAN
#endif // HAL_TARGET_IS_RP
