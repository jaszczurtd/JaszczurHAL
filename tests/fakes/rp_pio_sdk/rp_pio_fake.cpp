// Simulated PIO blocks, PIO-paced DMA, GPIO and clocks for host tests of the
// RP software UART backend. See hardware/dma.h for the TRANS_COUNT model.

#include "rp_pio_fake.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include <string.h>

#define FAKE_DMA_CHANNELS 12u
#define FAKE_PIO_SLOTS 32u
#define FAKE_FIFO_DEPTH 8u
#define FAKE_DREQ_PIO_RX 4u

namespace {

struct sm_t {
  bool claimed;
  bool enabled;
  pio_sm_config config;
  uint y;
  uint32_t fifo[FAKE_FIFO_DEPTH];
  uint fifo_count;
};

struct channel_t {
  bool claimed;
  bool busy;
  dma_channel_config config;
  uintptr_t write_addr;
  uintptr_t read_addr;
};

pio_hw_t s_pio_hw[NUM_PIOS] = {};
sm_t s_sm[NUM_PIOS][NUM_PIO_STATE_MACHINES] = {};
uint s_program_used[NUM_PIOS] = {};
channel_t s_channel[FAKE_DMA_CHANNELS] = {};
dma_channel_hw_t s_dma_hw[FAKE_DMA_CHANNELS] = {};
uint32_t s_dropped = 0u;

uint pio_index_of(PIO pio) { return (uint)(pio - s_pio_hw); }

uint32_t count_of(uint32_t reg) {
#if PICO_RP2040
  return reg;
#else
  return reg & DMA_CH0_TRANS_COUNT_COUNT_BITS;
#endif
}

bool endless(uint32_t reg) {
#if PICO_RP2040
  (void)reg;
  return false;
#else
  return (reg & DMA_CH0_TRANS_COUNT_MODE_BITS) == DMA_CH0_TRANS_COUNT_MODE_BITS;
#endif
}

uintptr_t next_write(const channel_t &ch, uintptr_t addr, uint64_t words) {
  if (!ch.config.write_increment) {
    return addr;
  }
  const uint64_t bytes = words * sizeof(uint32_t);
  if (ch.config.ring_write && ch.config.ring_bits != 0u) {
    const uintptr_t mask = ((uintptr_t)1u << ch.config.ring_bits) - 1u;
    return (addr & ~mask) | ((addr + (uintptr_t)bytes) & mask);
  }
  return addr + (uintptr_t)bytes;
}

// Moves @p words words of @p word, the newest ring-size of them written;
// returns how many the run allowed.
uint32_t transfer(uint channel, uint32_t words, uint32_t word) {
  channel_t &ch = s_channel[channel];
  if (!ch.busy) {
    return 0u;
  }
  const uint32_t reg = s_dma_hw[channel].transfer_count;
  const bool forever = endless(reg);
  uint32_t moved = words;
  if (!forever && moved > count_of(reg)) {
    moved = count_of(reg);
  }
  uint32_t written = moved;
  if (ch.config.ring_write && ch.config.ring_bits != 0u) {
    const uint32_t ring_words = (1u << ch.config.ring_bits) / sizeof(uint32_t);
    if (written > ring_words) {
      written = ring_words;
    }
  }
  ch.write_addr = next_write(ch, ch.write_addr, moved - written);
  for (uint32_t i = 0u; i < written; ++i) {
    memcpy((void *)ch.write_addr, &word, sizeof(word));
    ch.write_addr = next_write(ch, ch.write_addr, 1u);
  }
  if (!forever) {
    const uint32_t left = count_of(reg) - moved;
    s_dma_hw[channel].transfer_count = (reg - count_of(reg)) | left;
    if (left == 0u) {
      ch.busy = false;
    }
  }
  return moved;
}

int reader_of(uint pio_index, uint sm) {
  const uintptr_t fifo = (uintptr_t)&s_pio_hw[pio_index].rxf[sm];
  for (uint channel = 0u; channel < FAKE_DMA_CHANNELS; ++channel) {
    if (s_channel[channel].busy && s_channel[channel].read_addr == fifo) {
      return (int)channel;
    }
  }
  return -1;
}

void drain(uint pio_index, uint sm) {
  sm_t &state = s_sm[pio_index][sm];
  while (state.fifo_count > 0u) {
    const int channel = reader_of(pio_index, sm);
    if (channel < 0 || transfer((uint)channel, 1u, state.fifo[0]) == 0u) {
      return;
    }
    memmove(&state.fifo[0], &state.fifo[1],
            (state.fifo_count - 1u) * sizeof(uint32_t));
    --state.fifo_count;
  }
}

bool receiver(uint8_t pin, uint *out_pio, uint *out_sm) {
  for (uint p = 0u; p < NUM_PIOS; ++p) {
    for (uint sm = 0u; sm < NUM_PIO_STATE_MACHINES; ++sm) {
      const sm_t &state = s_sm[p][sm];
      if (state.enabled && state.config.in_base == pin &&
          state.config.jmp_pin == pin) {
        *out_pio = p;
        *out_sm = sm;
        return true;
      }
    }
  }
  return false;
}

// The word jh_swserial_rx pushes for one valid frame: Y + 1 samples shifted
// right into the ISR, so the first data bit lands at bit 32 - samples.
uint32_t rx_word(const sm_t &state, uint8_t byte) {
  const uint samples = state.y + 1u;
  const uint32_t bits = (uint32_t)byte & ((1u << samples) - 1u);
  return bits << (32u - samples);
}

} // namespace

void jh_fake_pio_reset(void) {
  memset(s_pio_hw, 0, sizeof(s_pio_hw));
  memset(s_sm, 0, sizeof(s_sm));
  memset(s_program_used, 0, sizeof(s_program_used));
  memset(s_channel, 0, sizeof(s_channel));
  memset(s_dma_hw, 0, sizeof(s_dma_hw));
  s_dropped = 0u;
}

bool jh_fake_uart_receive(uint8_t pin, uint8_t byte) {
  uint p = 0u;
  uint sm = 0u;
  if (!receiver(pin, &p, &sm)) {
    return false;
  }
  sm_t &state = s_sm[p][sm];
  if (state.fifo_count == FAKE_FIFO_DEPTH) {
    ++s_dropped;
    return true;
  }
  state.fifo[state.fifo_count++] = rx_word(state, byte);
  drain(p, sm);
  return true;
}

uint32_t jh_fake_uart_dropped(void) { return s_dropped; }

uint32_t jh_fake_dma_rx_remaining(uint8_t pin) {
  uint p = 0u;
  uint sm = 0u;
  if (!receiver(pin, &p, &sm)) {
    return 0u;
  }
  const int channel = reader_of(p, sm);
  return channel < 0 ? 0u : count_of(s_dma_hw[channel].transfer_count);
}

uint32_t jh_fake_dma_rx_elapse(uint8_t pin, uint32_t transfers,
                               uint8_t filler) {
  uint p = 0u;
  uint sm = 0u;
  if (!receiver(pin, &p, &sm)) {
    return 0u;
  }
  const int channel = reader_of(p, sm);
  if (channel < 0) {
    return 0u;
  }
  return transfer((uint)channel, transfers, rx_word(s_sm[p][sm], filler));
}

// --- SDK surface -----------------------------------------------------------

uint32_t clock_get_hz(enum clock_index clock) {
  (void)clock;
#if PICO_RP2040
  return 125000000u;
#else
  return 150000000u;
#endif
}

void gpio_pull_up(uint gpio) { (void)gpio; }
void gpio_set_dir(uint gpio, bool out) {
  (void)gpio;
  (void)out;
}
void gpio_set_function(uint gpio, enum gpio_function fn) {
  (void)gpio;
  (void)fn;
}
void gpio_put(uint gpio, bool value) {
  (void)gpio;
  (void)value;
}

dma_channel_hw_t *dma_channel_hw_addr(uint channel) {
  return &s_dma_hw[channel];
}

dma_channel_config dma_channel_get_default_config(uint channel) {
  (void)channel;
  dma_channel_config config = {};
  config.data_size = DMA_SIZE_32;
  config.read_increment = true;
  return config;
}

void dma_channel_configure(uint channel, const dma_channel_config *config,
                           volatile void *write_addr,
                           const volatile void *read_addr, uint transfer_count,
                           bool trigger) {
  channel_t &ch = s_channel[channel];
  ch.config = *config;
  ch.write_addr = (uintptr_t)write_addr;
  ch.read_addr = (uintptr_t)read_addr;
  s_dma_hw[channel].transfer_count = transfer_count;
  if (trigger) {
    ch.busy = endless(transfer_count) || count_of(transfer_count) != 0u;
    for (uint p = 0u; p < NUM_PIOS; ++p) {
      for (uint sm = 0u; sm < NUM_PIO_STATE_MACHINES; ++sm) {
        if (ch.read_addr == (uintptr_t)&s_pio_hw[p].rxf[sm]) {
          drain(p, sm);
        }
      }
    }
  }
}

int dma_claim_unused_channel(bool required) {
  (void)required;
  for (uint channel = 0u; channel < FAKE_DMA_CHANNELS; ++channel) {
    if (!s_channel[channel].claimed) {
      s_channel[channel].claimed = true;
      return (int)channel;
    }
  }
  return -1;
}

void dma_channel_unclaim(uint channel) { s_channel[channel].claimed = false; }

void dma_channel_abort(uint channel) { s_channel[channel].busy = false; }

PIO pio_get_instance(uint index) { return &s_pio_hw[index]; }

bool pio_can_add_program(PIO pio, const struct pio_program *program) {
  return s_program_used[pio_index_of(pio)] + program->length <= FAKE_PIO_SLOTS;
}

int pio_add_program(PIO pio, const struct pio_program *program) {
  const uint p = pio_index_of(pio);
  if (s_program_used[p] + program->length > FAKE_PIO_SLOTS) {
    return -1;
  }
  const uint offset = s_program_used[p];
  s_program_used[p] += program->length;
  return (int)offset;
}

void pio_remove_program(PIO pio, const struct pio_program *program,
                        uint offset) {
  (void)offset;
  s_program_used[pio_index_of(pio)] -= program->length;
}

int pio_claim_unused_sm(PIO pio, bool required) {
  (void)required;
  const uint p = pio_index_of(pio);
  for (uint sm = 0u; sm < NUM_PIO_STATE_MACHINES; ++sm) {
    if (!s_sm[p][sm].claimed) {
      s_sm[p][sm].claimed = true;
      return (int)sm;
    }
  }
  return -1;
}

void pio_sm_unclaim(PIO pio, uint sm) { s_sm[pio_index_of(pio)][sm] = sm_t{}; }

void pio_sm_set_enabled(PIO pio, uint sm, bool enabled) {
  s_sm[pio_index_of(pio)][sm].enabled = enabled;
}

void pio_sm_clear_fifos(PIO pio, uint sm) {
  s_sm[pio_index_of(pio)][sm].fifo_count = 0u;
}
void pio_sm_restart(PIO pio, uint sm) {
  (void)pio;
  (void)sm;
}
void pio_sm_clkdiv_restart(PIO pio, uint sm) {
  (void)pio;
  (void)sm;
}

int pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pin, uint count,
                                   bool is_out) {
  (void)pio;
  (void)sm;
  (void)pin;
  (void)count;
  (void)is_out;
  return 0;
}

void pio_gpio_init(PIO pio, uint pin) {
  (void)pio;
  (void)pin;
}

int pio_sm_init(PIO pio, uint sm, uint initial_pc,
                const pio_sm_config *config) {
  (void)initial_pc;
  sm_t &state = s_sm[pio_index_of(pio)][sm];
  state.config = *config;
  state.enabled = false;
  state.fifo_count = 0u;
  return 0;
}

void pio_sm_exec(PIO pio, uint sm, uint instr) {
  if ((instr & 0xffe0u) == pio_encode_set(pio_y, 0u)) {
    s_sm[pio_index_of(pio)][sm].y = instr & 0x1fu;
  }
}

void pio_sm_set_pins_with_mask(PIO pio, uint sm, uint32_t values,
                               uint32_t mask) {
  (void)pio;
  (void)sm;
  (void)values;
  (void)mask;
}

void pio_sm_set_pindirs_with_mask(PIO pio, uint sm, uint32_t dirs,
                                  uint32_t mask) {
  (void)pio;
  (void)sm;
  (void)dirs;
  (void)mask;
}

void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data) {
  (void)pio;
  (void)sm;
  (void)data;
}

uint pio_get_dreq(PIO pio, uint sm, bool is_tx) {
  return pio_index_of(pio) * 8u + sm + (is_tx ? 0u : FAKE_DREQ_PIO_RX);
}
