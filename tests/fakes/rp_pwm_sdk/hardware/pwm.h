#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;

typedef struct {
  uint32_t csr;
  uint32_t div;
  uint32_t top;
} pwm_config;

static inline uint pwm_gpio_to_slice_num(uint gpio) {
  return (gpio >> 1u) & 7u;
}
static inline uint pwm_gpio_to_channel(uint gpio) { return gpio & 1u; }

pwm_config pwm_get_default_config(void);
/* Truncates to 1/16 steps, as the SDK does without round-to-nearest. */
void pwm_config_set_clkdiv(pwm_config *c, float div);
void pwm_config_set_wrap(pwm_config *c, uint16_t wrap);
void pwm_init(uint slice_num, pwm_config *c, bool start);
void pwm_set_enabled(uint slice_num, bool enabled);
void pwm_set_wrap(uint slice_num, uint16_t wrap);
void pwm_set_gpio_level(uint gpio, uint16_t level);
uint16_t pwm_get_counter(uint slice_num);
void pwm_set_clkdiv_int_frac4(uint slice_num, uint8_t div_int,
                              uint8_t div_frac4);
