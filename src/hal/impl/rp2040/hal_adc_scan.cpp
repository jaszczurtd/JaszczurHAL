#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_ADC_SCAN
#include "hal/analog/jh_adc_scan_backend.h"
#include "hal/analog/jh_adc_scan_ring.h"
#include "hal/system/hal_system.h"
#include "jh_rp_adc_scan_clock.h"
#include "rp2040_adc_shared.h"

#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/sync.h>

namespace {

// Two DMA channels chained into a ring: each fills one half of the buffer and
// then chains to a control channel that writes the other half's address into
// the other data channel's write-address trigger alias, so the ring runs on
// the DMA alone. DMA_IRQ_0 only publishes the finished block; a core that
// keeps interrupts masked for longer than a block (a flash transaction holds
// them for tens of milliseconds) loses blocks, at most two of which the
// pending interrupt bits still count, but the ring never runs past its
// buffer. DACless keeps DMA_IRQ_1; both paths share the converter
// ownership flag in rp2040_adc_shared, so they exclude each other instead of
// colliding.
constexpr uint32_t kMinConversionCycles = JH_RP_ADC_CONVERSION_CYCLES;
constexpr uint32_t kMaxConversionCycles = 65536u;
constexpr uint8_t kTemperatureInput = 4u;
constexpr uint8_t kInputs = 5u;

struct scan_state_t {
  hal_adc_scan_config_t config;
  uint8_t count;
  uint8_t input_mask;
  uint8_t input_of_position[kInputs];
  int channel[2];
  int control[2];
  uintptr_t reload_target[2]; /* half(b) as the register image a control
                                 channel copies into the trigger alias */
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
  uint32_t finished = 0u;
  uint8_t newest = s.completed;
  for (uint8_t b = 0u; b < 2u; ++b) {
    if (s.channel[b] < 0 || (ints & (1u << (uint)s.channel[b])) == 0u) {
      continue;
    }
    dma_hw->ints0 = 1u << (uint)s.channel[b];
    ++finished;
    newest = b;
  }
  if (finished == 0u) {
    return;
  }
  // Both halves finished while this core kept interrupts masked (a flash
  // transaction, for example): the ring went on by itself, so the newest
  // complete half is the one whose data channel is not filling right now.
  if (finished == 2u) {
    newest = dma_channel_is_busy((uint)s.channel[0]) ? 1u : 0u;
  }
  s.marker =
      s.config.marker != NULL ? s.config.marker(s.config.marker_user) : 0u;
  s.completed_us = hal_micros();
  s.completed = newest;
  s.sequence = s.sequence + finished;
}

// Looks at the ring until the snapshot is settled: the gap between two
// blocks lasts one control transfer, and a stall long enough to tear the
// snapshot ends before the next look.
constexpr uint8_t kReaderAttempts = 4u;

// Exactly one data channel runs inside a block and none during the control
// transfer between blocks. Two channels seen busy, or one seen idle and then
// busy, is a snapshot torn by a stall between two reads (a flash lockout on
// this core, for instance): the ring moved on by a block or more meanwhile.
bool snapshot_settled(const jh_adc_scan_channel_t channels[2]) {
  if (channels[0].busy_before == channels[1].busy_before) {
    return false;
  }
  for (uint8_t b = 0u; b < 2u; ++b) {
    if (!channels[b].busy_before && channels[b].busy_after) {
      return false;
    }
  }
  return true;
}

void sample_channels(jh_adc_scan_channel_t channels[2]) {
  for (uint8_t b = 0u; b < 2u; ++b) {
    channels[b] = jh_adc_scan_channel_t{};
    if (s.channel[b] < 0) {
      continue;
    }
    const uint channel = (uint)s.channel[b];
    channels[b].busy_before = dma_channel_is_busy(channel);
    const uintptr_t base = (uintptr_t)half(b);
    const uintptr_t write = (uintptr_t)dma_hw->ch[channel].write_addr;
    channels[b].busy_after = dma_channel_is_busy(channel);
    channels[b].written =
        write >= base ? (uint32_t)((write - base) / sizeof(uint16_t)) : 0u;
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

void configure_channel(int channel, int next_control, uint16_t *target) {
  dma_channel_config cfg = dma_channel_get_default_config((uint)channel);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, true);
  channel_config_set_dreq(&cfg, DREQ_ADC);
  channel_config_set_chain_to(&cfg, (uint)next_control);
  channel_config_set_irq_quiet(&cfg, false);
  dma_channel_configure((uint)channel, &cfg, target, &adc_hw->fifo,
                        s.samples_per_block, false);
  dma_channel_set_irq0_enabled((uint)channel, true);
}

// One word from reload_target[b] into data channel b's write-address trigger
// alias: the write pointer returns to half(b) and the block starts.
void configure_control(int control, int data_channel, uintptr_t *source) {
  dma_channel_config cfg = dma_channel_get_default_config((uint)control);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, false);
  channel_config_set_irq_quiet(&cfg, true);
  channel_config_set_dreq(&cfg, DREQ_FORCE);
  dma_channel_configure((uint)control, &cfg,
                        &dma_hw->ch[data_channel].al2_write_addr_trig, source,
                        1u, false);
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
  s.control[0] = -1;
  s.control[1] = -1;
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
  s.control[0] = dma_claim_unused_channel(false);
  s.control[1] = dma_claim_unused_channel(false);
  if (s.channel[0] < 0 || s.channel[1] < 0 || s.control[0] < 0 ||
      s.control[1] < 0) {
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
  adc_set_clkdiv(jh_rp_adc_scan_clkdiv(cycles));
  adc_set_round_robin(mask);
  adc_select_input(s.input_of_position[0]);
  adc_fifo_setup(true, true, 1u, false, false);
  adc_fifo_drain();

  dma_hw->ints0 = (1u << (uint)s.channel[0]) | (1u << (uint)s.channel[1]);
  s.reload_target[0] = (uintptr_t)half(0u);
  s.reload_target[1] = (uintptr_t)half(1u);
  // Data 0 finishes -> control 1 starts data 1 at half(1) -> control 0
  // starts data 0 at half(0) -> ...
  configure_channel(s.channel[0], s.control[1], half(0u));
  configure_channel(s.channel[1], s.control[0], half(1u));
  configure_control(s.control[0], s.channel[0], &s.reload_target[0]);
  configure_control(s.control[1], s.channel[1], &s.reload_target[1]);
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
    if (s.control[b] >= 0) {
      dma_channel_abort((uint)s.control[b]);
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
    if (s.control[b] >= 0) {
      dma_channel_unclaim((uint)s.control[b]);
      s.control[b] = -1;
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
  // Inside a block exactly one data channel is busy and its write pointer
  // says how many complete frames the block already holds. The busy flag is
  // looked at again after the pointer: a channel that finished between the
  // two looks has filled its half whatever the pointer said, and taking it
  // for a fresh trigger would hand out the other half, which the ring may
  // have restarted by then (bench 2026-09-16, one read in three hundred
  // thousand a block old). Between two blocks, for the few cycles the control
  // channel needs, neither data channel is busy; a finished channel keeps its
  // pointer at the end of its half until its own control channel restarts it
  // a block later, so both halves then look full and cannot be ordered by
  // their pointers. Looking again settles it, because the next block starts
  // within those cycles. Right after the chain the other half is the complete
  // one even before its interrupt has run, so the frame choice never leans on
  // that bookkeeping while the ring runs. A snapshot torn by a stall between
  // two reads is taken again; after the attempts the last one serves.
  jh_adc_scan_channel_t channels[2] = {};
  for (uint8_t attempt = 0u; attempt < kReaderAttempts; ++attempt) {
    sample_channels(channels);
    if (snapshot_settled(channels)) {
      break;
    }
  }
  const uint32_t saved = save_and_disable_interrupts();
  const uint32_t sequence = s.sequence;
  const uint8_t completed = s.completed;
  restore_interrupts(saved);
  uint8_t index = 0u;
  uint32_t frame = 0u;
  if (!jh_adc_scan_pick_frame(channels, s.count, s.config.block_frames,
                              sequence, completed, &index, &frame)) {
    return HAL_EAGAIN;
  }
  *raw = half(index)[(frame * s.count) + position];
  return HAL_OK;
}

#endif // HAL_ENABLE_ADC_SCAN
#endif // HAL_TARGET_IS_RP
