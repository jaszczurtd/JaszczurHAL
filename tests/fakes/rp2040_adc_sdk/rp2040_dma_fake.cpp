// Simulated RP2040 DMA, DMA_IRQ_0 and ADC FIFO for host tests of DMA-driven
// backends. Data channels paced by DREQ_ADC move one sample per feed; a
// DREQ_FORCE channel runs to completion the moment it is triggered, and a
// word it writes into another channel's al2_write_addr_trig register sets
// that channel's write pointer and starts it, as the chip does.

#include "rp2040_dma_fake.h"

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <string.h>

adc_hw_t jh_fake_adc_hw = {};
dma_hw_t jh_fake_dma_hw = {};

namespace {

struct channel_t {
  bool claimed;
  bool busy;
  dma_channel_config config;
  uint reload;
  uint32_t triggers;
};

struct adc_t {
  bool running;
  uint round_robin;
  float clkdiv;
  bool temperature;
  uint selected;
};

channel_t s_channel[NUM_DMA_CHANNELS] = {};
adc_t s_adc = {};
irq_handler_t s_handler = nullptr;
bool s_irq_enabled = false;
bool s_masked = false;
uint32_t s_handler_calls = 0u;
uint32_t s_fed = 0u;
uint32_t s_dropped = 0u;
uint32_t s_outside = 0u;
uintptr_t s_region_begin = 0u;
size_t s_region_bytes = 0u;
uint32_t s_access_count = 0u;
bool s_schedule_armed = false;
uint32_t s_schedule_index = 0u;
uint32_t s_schedule_samples = 0u;
bool s_feeding = false;

bool in_region(uintptr_t address, size_t bytes) {
  return s_region_bytes != 0u && address >= s_region_begin &&
         address + bytes <= s_region_begin + s_region_bytes;
}

void deliver_irq(void) {
  if (s_handler != nullptr && s_irq_enabled && !s_masked &&
      jh_fake_dma_hw.ints0.value != 0u) {
    ++s_handler_calls;
    s_handler();
  }
}

void feed(uint32_t samples);

// A register access of the code under test: the moment a scheduled feed
// lands between two of its reads.
void on_access(void) {
  if (s_feeding) {
    return;
  }
  if (s_schedule_armed && s_access_count == s_schedule_index) {
    s_schedule_armed = false;
    feed(s_schedule_samples);
  }
  ++s_access_count;
}

void trigger(uint channel);

void complete(uint channel) {
  channel_t &ch = s_channel[channel];
  ch.busy = false;
  jh_fake_dma_hw.ch[channel].ctrl_trig.value &=
      ~(uintptr_t)DMA_CH0_CTRL_TRIG_BUSY_BITS;
  if (!ch.config.irq_quiet &&
      (jh_fake_dma_hw.inte0.value & (1u << channel)) != 0u) {
    jh_fake_dma_hw.ints0.value |= 1u << channel;
  }
  if (ch.config.chain_to != channel) {
    trigger(ch.config.chain_to);
  }
  deliver_irq();
}

// A forced channel moves its words at once. Its source is read as the
// register image of an address, its destination either a trigger alias of
// another channel or plain memory.
void run_forced(uint channel) {
  channel_t &ch = s_channel[channel];
  dma_channel_hw_t &regs = jh_fake_dma_hw.ch[channel];
  while (regs.transfer_count.value > 0u) {
    uintptr_t word = 0u;
    memcpy(&word, (const void *)regs.read_addr.value, sizeof(word));
    bool aliased = false;
    for (uint other = 0u; other < NUM_DMA_CHANNELS; ++other) {
      if (regs.write_addr.value ==
          (uintptr_t)&jh_fake_dma_hw.ch[other].al2_write_addr_trig) {
        jh_fake_dma_hw.ch[other].write_addr.value = word;
        aliased = true;
        trigger(other);
        break;
      }
    }
    if (!aliased) {
      if (in_region(regs.write_addr.value, sizeof(word))) {
        memcpy((void *)regs.write_addr.value, &word, sizeof(word));
      } else {
        ++s_outside;
      }
    }
    if (ch.config.read_increment) {
      regs.read_addr.value += sizeof(word);
    }
    if (ch.config.write_increment) {
      regs.write_addr.value += sizeof(word);
    }
    regs.transfer_count.value -= 1u;
  }
  complete(channel);
}

void trigger(uint channel) {
  channel_t &ch = s_channel[channel];
  if (ch.busy) {
    return;
  }
  ch.busy = true;
  ++ch.triggers;
  jh_fake_dma_hw.ch[channel].ctrl_trig.value |= DMA_CH0_CTRL_TRIG_BUSY_BITS;
  jh_fake_dma_hw.ch[channel].transfer_count.value = ch.reload;
  if (ch.config.dreq == DREQ_FORCE) {
    run_forced(channel);
  }
}

int fifo_reader(void) {
  for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
    if (s_channel[channel].busy && s_channel[channel].config.dreq == DREQ_ADC &&
        jh_fake_dma_hw.ch[channel].read_addr.value ==
            (uintptr_t)&jh_fake_adc_hw.fifo) {
      return (int)channel;
    }
  }
  return -1;
}

void feed(uint32_t samples) {
  const bool nested = s_feeding;
  s_feeding = true;
  for (uint32_t i = 0u; i < samples; ++i) {
    const uint16_t value = (uint16_t)(s_fed & 0xFFFFu);
    ++s_fed;
    const int channel = fifo_reader();
    if (channel < 0) {
      ++s_dropped;
      continue;
    }
    dma_channel_hw_t &regs = jh_fake_dma_hw.ch[(uint)channel];
    if (in_region(regs.write_addr.value, sizeof(value))) {
      memcpy((void *)regs.write_addr.value, &value, sizeof(value));
    } else {
      ++s_outside;
    }
    if (s_channel[(uint)channel].config.write_increment) {
      regs.write_addr.value += sizeof(value);
    }
    regs.transfer_count.value -= 1u;
    if (regs.transfer_count.value == 0u) {
      complete((uint)channel);
    }
  }
  s_feeding = nested;
}

} // namespace

jh_fake_dma_reg::operator uintptr_t() const {
  on_access();
  return value;
}

jh_fake_dma_reg &jh_fake_dma_reg::operator=(uintptr_t next) {
  on_access();
  if (this == &jh_fake_dma_hw.ints0) {
    value &= ~next; /* write-1-to-clear, as on the chip */
  } else {
    value = next;
  }
  return *this;
}

// --- SDK surface -----------------------------------------------------------

dma_channel_config dma_channel_get_default_config(uint channel) {
  dma_channel_config config = {};
  config.channel = channel;
  config.data_size = DMA_SIZE_32;
  config.read_increment = true;
  config.write_increment = false;
  config.dreq = DREQ_FORCE;
  config.chain_to = channel;
  config.irq_quiet = false;
  return config;
}

void dma_channel_configure(uint channel, const dma_channel_config *config,
                           volatile void *write_addr,
                           const volatile void *read_addr, uint transfer_count,
                           bool trigger_now) {
  channel_t &ch = s_channel[channel];
  ch.config = *config;
  ch.reload = transfer_count;
  jh_fake_dma_hw.ch[channel].write_addr.value = (uintptr_t)write_addr;
  jh_fake_dma_hw.ch[channel].read_addr.value = (uintptr_t)read_addr;
  jh_fake_dma_hw.ch[channel].transfer_count.value = transfer_count;
  if (trigger_now) {
    trigger(channel);
  }
}

void dma_channel_set_irq0_enabled(uint channel, bool enabled) {
  if (enabled) {
    jh_fake_dma_hw.inte0.value |= 1u << channel;
  } else {
    jh_fake_dma_hw.inte0.value &= ~(uintptr_t)(1u << channel);
  }
}

int dma_claim_unused_channel(bool required) {
  (void)required;
  for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
    if (!s_channel[channel].claimed) {
      s_channel[channel].claimed = true;
      return (int)channel;
    }
  }
  return -1;
}

void dma_channel_unclaim(uint channel) { s_channel[channel].claimed = false; }

void dma_channel_start(uint channel) { trigger(channel); }

void dma_channel_abort(uint channel) {
  s_channel[channel].busy = false;
  jh_fake_dma_hw.ch[channel].ctrl_trig.value &=
      ~(uintptr_t)DMA_CH0_CTRL_TRIG_BUSY_BITS;
}

bool dma_channel_is_busy(uint channel) {
  on_access();
  return s_channel[channel].busy;
}

bool irq_has_handler(unsigned int irq) {
  return irq == DMA_IRQ_0 && s_handler != nullptr;
}

void irq_set_exclusive_handler(unsigned int irq, irq_handler_t handler) {
  if (irq == DMA_IRQ_0) {
    s_handler = handler;
  }
}

void irq_remove_handler(unsigned int irq, irq_handler_t handler) {
  if (irq == DMA_IRQ_0 && s_handler == handler) {
    s_handler = nullptr;
  }
}

void irq_set_enabled(unsigned int irq, bool enabled) {
  if (irq == DMA_IRQ_0) {
    s_irq_enabled = enabled;
    deliver_irq();
  }
}

uint32_t save_and_disable_interrupts(void) {
  on_access();
  const uint32_t status = s_masked ? 1u : 0u;
  s_masked = true;
  return status;
}

void restore_interrupts(uint32_t status) {
  on_access();
  s_masked = status != 0u;
  deliver_irq();
}

uint32_t clock_get_hz(enum clock_index clock) {
  return clock == clk_adc ? 48000000u : 0u;
}

extern "C" {

void sleep_ms(uint32_t ms) { (void)ms; }
void adc_init(void) {}
void adc_gpio_init(uint gpio) { (void)gpio; }
void adc_select_input(uint input) { s_adc.selected = input; }
uint16_t adc_read(void) { return 0u; }
void adc_set_temp_sensor_enabled(bool enabled) { s_adc.temperature = enabled; }
void adc_run(bool run) { s_adc.running = run; }
void adc_fifo_drain(void) {}
void adc_set_clkdiv(float clkdiv) { s_adc.clkdiv = clkdiv; }
void adc_set_round_robin(uint input_mask) { s_adc.round_robin = input_mask; }
void adc_fifo_setup(bool en, bool dreq_en, uint16_t dreq_thresh,
                    bool err_in_fifo, bool byte_shift) {
  (void)en;
  (void)dreq_en;
  (void)dreq_thresh;
  (void)err_in_fifo;
  (void)byte_shift;
}

} // extern "C"

// --- test control ------------------------------------------------------------

void jh_fake_dma_reset(void) {
  memset(&jh_fake_dma_hw, 0, sizeof(jh_fake_dma_hw));
  memset(&jh_fake_adc_hw, 0, sizeof(jh_fake_adc_hw));
  for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
    s_channel[channel] = channel_t{};
  }
  s_adc = adc_t{};
  s_handler = nullptr;
  s_irq_enabled = false;
  s_masked = false;
  s_handler_calls = 0u;
  s_fed = 0u;
  s_dropped = 0u;
  s_outside = 0u;
  s_region_begin = 0u;
  s_region_bytes = 0u;
  s_access_count = 0u;
  s_schedule_armed = false;
  s_feeding = false;
}

void jh_fake_dma_set_legal_region(const void *begin, size_t bytes) {
  s_region_begin = (uintptr_t)begin;
  s_region_bytes = bytes;
}

uint32_t jh_fake_dma_writes_outside_region(void) { return s_outside; }

void jh_fake_adc_feed(uint32_t samples) { feed(samples); }
uint32_t jh_fake_adc_samples_fed(void) { return s_fed; }
uint32_t jh_fake_adc_samples_dropped(void) { return s_dropped; }

void jh_fake_dma_schedule_feed(uint32_t access_index, uint32_t samples) {
  s_schedule_armed = true;
  s_schedule_index = access_index;
  s_schedule_samples = samples;
}

void jh_fake_dma_reset_access_count(void) { s_access_count = 0u; }
uint32_t jh_fake_dma_access_count(void) { return s_access_count; }

uint32_t jh_fake_dma_claimed_channels(void) {
  uint32_t count = 0u;
  for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
    count += s_channel[channel].claimed ? 1u : 0u;
  }
  return count;
}

uint32_t jh_fake_dma_channel_triggers(unsigned int channel) {
  return channel < NUM_DMA_CHANNELS ? s_channel[channel].triggers : 0u;
}

uint32_t jh_fake_irq_handler_calls(void) { return s_handler_calls; }
bool jh_fake_irq_masked(void) { return s_masked; }
bool jh_fake_irq_enabled(void) { return s_irq_enabled; }

bool jh_fake_adc_running(void) { return s_adc.running; }
unsigned int jh_fake_adc_round_robin(void) { return s_adc.round_robin; }
float jh_fake_adc_clkdiv(void) { return s_adc.clkdiv; }
bool jh_fake_adc_temperature_enabled(void) { return s_adc.temperature; }
