#pragma once

/* Simulated PIO blocks for the software UART: programs, state machines and
 * their FIFOs. A receive state machine turns each frame the test sends into
 * the word the jh_swserial_rx program pushes; the DMA moves it from there. */

#include "pico/platform.h"

#include <stdbool.h>
#include <stdint.h>

#if PICO_RP2040
#define NUM_PIOS 2u
#else
#define NUM_PIOS 3u
#endif
#define NUM_PIO_STATE_MACHINES 4u
#define PIO_FDEBUG_TXSTALL_LSB 24u

typedef struct {
  /* Every transmit completes at once: written stall bits read back set. */
  volatile uint32_t fdebug;
  volatile uint32_t rxf[NUM_PIO_STATE_MACHINES];
} pio_hw_t;
typedef pio_hw_t *PIO;

struct pio_program {
  const uint16_t *instructions;
  uint8_t length;
  int8_t origin;
  uint8_t pio_version;
#if PICO_PIO_VERSION > 0
  uint8_t used_gpio_ranges;
#endif
};

enum pio_src_dest {
  pio_pins = 0,
  pio_x = 1,
  pio_y = 2,
  pio_null = 3,
  pio_isr = 6
};
enum pio_fifo_join {
  PIO_FIFO_JOIN_NONE = 0,
  PIO_FIFO_JOIN_TX = 1,
  PIO_FIFO_JOIN_RX = 2
};

typedef struct {
  uint wrap_target;
  uint wrap;
  uint in_base;
  uint jmp_pin;
  uint out_base;
  uint out_count;
  uint sideset_base;
  bool in_shift_right;
  float clkdiv;
  enum pio_fifo_join join;
} pio_sm_config;

static inline uint pio_encode_set(enum pio_src_dest dest, uint value) {
  return 0xe000u | ((uint)dest << 5u) | (value & 0x1fu);
}
static inline uint pio_encode_mov(enum pio_src_dest dest,
                                  enum pio_src_dest src) {
  return 0xa000u | ((uint)dest << 5u) | (uint)src;
}

static inline pio_sm_config pio_get_default_sm_config(void) {
  pio_sm_config c = {};
  c.clkdiv = 1.0f;
  c.in_shift_right = true;
  return c;
}
static inline void sm_config_set_wrap(pio_sm_config *c, uint wrap_target,
                                      uint wrap) {
  c->wrap_target = wrap_target;
  c->wrap = wrap;
}
static inline void sm_config_set_sideset(pio_sm_config *c, uint bit_count,
                                         bool optional, bool pindirs) {
  (void)c;
  (void)bit_count;
  (void)optional;
  (void)pindirs;
}
static inline void sm_config_set_in_pins(pio_sm_config *c, uint base) {
  c->in_base = base;
}
static inline void sm_config_set_jmp_pin(pio_sm_config *c, uint pin) {
  c->jmp_pin = pin;
}
static inline void sm_config_set_in_shift(pio_sm_config *c, bool shift_right,
                                          bool autopush, uint threshold) {
  (void)autopush;
  (void)threshold;
  c->in_shift_right = shift_right;
}
static inline void sm_config_set_out_shift(pio_sm_config *c, bool shift_right,
                                           bool autopull, uint threshold) {
  (void)c;
  (void)shift_right;
  (void)autopull;
  (void)threshold;
}
static inline void sm_config_set_out_pins(pio_sm_config *c, uint base,
                                          uint count) {
  c->out_base = base;
  c->out_count = count;
}
static inline void sm_config_set_sideset_pins(pio_sm_config *c, uint base) {
  c->sideset_base = base;
}
static inline void sm_config_set_fifo_join(pio_sm_config *c,
                                           enum pio_fifo_join join) {
  c->join = join;
}
static inline void sm_config_set_clkdiv(pio_sm_config *c, float div) {
  c->clkdiv = div;
}

PIO pio_get_instance(uint index);
bool pio_can_add_program(PIO pio, const struct pio_program *program);
int pio_add_program(PIO pio, const struct pio_program *program);
void pio_remove_program(PIO pio, const struct pio_program *program,
                        uint offset);
int pio_claim_unused_sm(PIO pio, bool required);
void pio_sm_unclaim(PIO pio, uint sm);
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
void pio_sm_clear_fifos(PIO pio, uint sm);
void pio_sm_restart(PIO pio, uint sm);
void pio_sm_clkdiv_restart(PIO pio, uint sm);
int pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pin, uint count,
                                   bool is_out);
void pio_gpio_init(PIO pio, uint pin);
int pio_sm_init(PIO pio, uint sm, uint initial_pc, const pio_sm_config *config);
void pio_sm_exec(PIO pio, uint sm, uint instr);
void pio_sm_set_pins_with_mask(PIO pio, uint sm, uint32_t values,
                               uint32_t mask);
void pio_sm_set_pindirs_with_mask(PIO pio, uint sm, uint32_t dirs,
                                  uint32_t mask);
void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data);
uint pio_get_dreq(PIO pio, uint sm, bool is_tx);
