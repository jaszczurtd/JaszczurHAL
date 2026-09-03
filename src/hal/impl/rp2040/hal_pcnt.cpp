#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_PCNT

#include "hal/analog/hal_pcnt.h"
#include "hal/analog/hal_pcnt_common.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/system/hal_sync.h"

/*
 * The RP2040 has no dedicated pulse-counter peripheral, but it has GPIO edge
 * interrupts. This backend implements a software counter: an edge IRQ on the
 * input pin increments a per-channel counter. Simple and portable; the usable
 * input frequency is bounded by the ISR rate (rule of thumb: keep well below a
 * few hundred kHz). For high-rate counting on RP2040, a PIO-based counter is
 * the better long-term backend.
 */

#define RP2040_PCNT_CHANNELS 2

static volatile uint32_t s_count[RP2040_PCNT_CHANNELS] = {};
static bool s_init[RP2040_PCNT_CHANNELS] = {};

/* Per-channel ISR trampolines (hal_gpio callbacks take no context). */
static void isr_ch0(void) { s_count[0]++; }
static void isr_ch1(void) { s_count[1]++; }
static void (*const s_isr[RP2040_PCNT_CHANNELS])(void) = {isr_ch0, isr_ch1};

static hal_gpio_irq_mode_t to_irq_mode(hal_pcnt_edge_t edge) {
  switch (edge) {
  case HAL_PCNT_EDGE_FALLING:
    return HAL_GPIO_IRQ_FALLING;
  case HAL_PCNT_EDGE_BOTH:
    return HAL_GPIO_IRQ_CHANGE;
  case HAL_PCNT_EDGE_RISING:
  default:
    return HAL_GPIO_IRQ_RISING;
  }
}

bool hal_pcnt_is_supported(void) { return true; }

uint8_t hal_pcnt_channel_count(void) { return RP2040_PCNT_CHANNELS; }

hal_status_t hal_pcnt_init_ex(uint8_t channel, uint8_t pin,
                              hal_pcnt_edge_t edge) {
  if (channel >= RP2040_PCNT_CHANNELS || !jh_hal_pcnt_edge_valid(edge)) {
    return HAL_EINVAL;
  }
  s_count[channel] = 0u;
  hal_gpio_set_mode(pin, HAL_GPIO_INPUT_PULLUP);
  hal_gpio_attach_interrupt(pin, s_isr[channel], to_irq_mode(edge));
  s_init[channel] = true;
  return HAL_OK;
}

bool hal_pcnt_init(uint8_t channel, uint8_t pin, hal_pcnt_edge_t edge) {
  return hal_status_to_bool(hal_pcnt_init_ex(channel, pin, edge));
}

hal_status_t hal_pcnt_read_ex(uint8_t channel, uint32_t *out_count) {
  if (out_count == nullptr || channel >= RP2040_PCNT_CHANNELS) {
    return HAL_EINVAL;
  }
  *out_count = 0u;
  if (!s_init[channel]) {
    return HAL_EUNINIT;
  }
  *out_count = s_count[channel];
  return HAL_OK;
}

uint32_t hal_pcnt_read(uint8_t channel) {
  uint32_t count = 0u;
  (void)hal_pcnt_read_ex(channel, &count);
  return count;
}

hal_status_t hal_pcnt_reset(uint8_t channel) {
  if (channel >= RP2040_PCNT_CHANNELS) {
    return HAL_EINVAL;
  }
  if (!s_init[channel]) {
    return HAL_EUNINIT;
  }
  s_count[channel] = 0u;
  return HAL_OK;
}

hal_status_t hal_pcnt_read_and_reset_ex(uint8_t channel, uint32_t *out_count) {
  if (out_count == nullptr || channel >= RP2040_PCNT_CHANNELS) {
    return HAL_EINVAL;
  }
  *out_count = 0u;
  if (!s_init[channel]) {
    return HAL_EUNINIT;
  }
  hal_critical_section_enter();
  *out_count = s_count[channel];
  s_count[channel] = 0u;
  hal_critical_section_exit();
  return HAL_OK;
}

uint32_t hal_pcnt_read_and_reset(uint8_t channel) {
  uint32_t count = 0u;
  (void)hal_pcnt_read_and_reset_ex(channel, &count);
  return count;
}

#endif // HAL_ENABLE_PCNT
#endif // HAL_TARGET_IS_RP
