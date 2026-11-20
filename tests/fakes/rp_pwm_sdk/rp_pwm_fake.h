#pragma once

/* Simulated RP PWM slices behind the Pico SDK calls used by the frequency
 * PWM backend. TOP and CC are double-buffered as on the chip: a write lands
 * in a pending register and becomes active when the counter wraps. The
 * divider is not buffered. Time moves only when the code reads the counter,
 * by a step the test selects. */

#include <stdint.h>

namespace rp_pwm_fake {

struct Slice {
  bool enabled;
  uint32_t div16;       /* clock divider in 1/16 steps */
  uint16_t top;         /* active period register */
  uint16_t pending_top; /* value loaded at the next wrap */
  uint16_t cc[2];
  uint16_t pending_cc[2];
  uint16_t counter;
  /* Counter value and interrupt mask at the latest TOP write. */
  uint16_t counter_at_top_write;
  bool irq_masked_at_top_write;
  bool irq_masked_at_cc_write;
  bool irq_masked_at_div_write;
  /* Wraps seen between the latest TOP write and the CC write after it. */
  uint32_t wraps_between_top_and_cc;
  uint32_t top_writes;
  uint32_t div_writes;
  uint32_t wraps;
};

void reset(uint32_t clock_hz);
Slice &slice(unsigned index);
/* Counter increment applied by every pwm_get_counter() call. */
void set_counter_step(unsigned index, uint16_t step);
bool pin_is_pwm(unsigned pin);
bool interrupts_masked();

} // namespace rp_pwm_fake
