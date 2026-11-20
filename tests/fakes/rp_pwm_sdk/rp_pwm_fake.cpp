#include "rp_pwm_fake.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

namespace rp_pwm_fake {
namespace {

Slice g_slices[8] = {};
uint16_t g_step[8] = {};
bool g_top_awaits_cc[8] = {};
bool g_pin_pwm[32] = {};
uint32_t g_clock_hz = 0u;
bool g_masked = false;

void wrap(unsigned index) {
  Slice &s = g_slices[index];
  s.top = s.pending_top;
  s.cc[0] = s.pending_cc[0];
  s.cc[1] = s.pending_cc[1];
  ++s.wraps;
  if (g_top_awaits_cc[index]) {
    ++s.wraps_between_top_and_cc;
  }
}

} // namespace

void reset(uint32_t clock_hz) {
  for (unsigned i = 0u; i < 8u; ++i) {
    g_slices[i] = Slice{};
    g_step[i] = 0u;
    g_top_awaits_cc[i] = false;
  }
  for (bool &pin : g_pin_pwm) {
    pin = false;
  }
  g_clock_hz = clock_hz;
  g_masked = false;
}

Slice &slice(unsigned index) { return g_slices[index & 7u]; }

void set_counter_step(unsigned index, uint16_t step) {
  g_step[index & 7u] = step;
}

bool pin_is_pwm(unsigned pin) { return pin < 32u && g_pin_pwm[pin]; }

bool interrupts_masked() { return g_masked; }

} // namespace rp_pwm_fake

using namespace rp_pwm_fake;

uint32_t clock_get_hz(enum clock_index) { return g_clock_hz; }

uint32_t save_and_disable_interrupts(void) {
  const uint32_t previous = g_masked ? 1u : 0u;
  g_masked = true;
  return previous;
}

void restore_interrupts(uint32_t status) { g_masked = status != 0u; }

void gpio_set_function(uint gpio, enum gpio_function) {
  if (gpio < 32u) {
    g_pin_pwm[gpio] = true;
  }
}

pwm_config pwm_get_default_config(void) {
  pwm_config config = {0u, 1u << 4, 0xFFFFu};
  return config;
}

void pwm_config_set_clkdiv(pwm_config *c, float div) {
  c->div = (uint32_t)(div * 16.0f);
}

void pwm_config_set_wrap(pwm_config *c, uint16_t wrap_value) {
  c->top = wrap_value;
}

void pwm_init(uint slice_num, pwm_config *c, bool start) {
  Slice &s = slice(slice_num);
  s.div16 = c->div;
  s.top = (uint16_t)c->top;
  s.pending_top = (uint16_t)c->top;
  s.cc[0] = s.cc[1] = 0u;
  s.pending_cc[0] = s.pending_cc[1] = 0u;
  s.counter = 0u;
  s.enabled = start;
}

void pwm_set_enabled(uint slice_num, bool enabled) {
  slice(slice_num).enabled = enabled;
}

void pwm_set_wrap(uint slice_num, uint16_t wrap_value) {
  Slice &s = slice(slice_num);
  s.pending_top = wrap_value;
  if (!s.enabled) {
    s.top = wrap_value;
  }
  s.counter_at_top_write = s.counter;
  s.irq_masked_at_top_write = g_masked;
  ++s.top_writes;
  g_top_awaits_cc[slice_num & 7u] = true;
}

void pwm_set_gpio_level(uint gpio, uint16_t level) {
  Slice &s = slice(pwm_gpio_to_slice_num(gpio));
  const uint channel = pwm_gpio_to_channel(gpio);
  s.pending_cc[channel] = level;
  if (!s.enabled) {
    s.cc[channel] = level;
  }
  s.irq_masked_at_cc_write = g_masked;
  g_top_awaits_cc[pwm_gpio_to_slice_num(gpio)] = false;
}

uint16_t pwm_get_counter(uint slice_num) {
  Slice &s = slice(slice_num);
  const uint16_t value = s.counter;
  if (s.enabled) {
    const uint32_t next = (uint32_t)s.counter + g_step[slice_num & 7u];
    if (next > s.top) {
      wrap(slice_num & 7u);
      s.counter = 0u;
    } else {
      s.counter = (uint16_t)next;
    }
  }
  return value;
}

void pwm_set_clkdiv_int_frac4(uint slice_num, uint8_t div_int,
                              uint8_t div_frac4) {
  Slice &s = slice(slice_num);
  s.div16 = ((uint32_t)div_int << 4) | (div_frac4 & 0xFu);
  s.irq_masked_at_div_write = g_masked;
  ++s.div_writes;
}
