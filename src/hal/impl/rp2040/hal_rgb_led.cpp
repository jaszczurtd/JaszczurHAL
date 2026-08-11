#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_RGB_LED

#include "hal/gpio/hal_rgb_led_internal.h"
#include "hal/gpio/neopixel/rp2040_pio.h"

namespace {

struct RpRgbTransport {
  PIO pio;
  int state_machine;
  uint program_offset;
};

RpRgbTransport s_transport{nullptr, -1, 0u};

} // namespace

bool jh_hal_rgb_led_pin_valid(uint8_t pin) { return pin < NUM_BANK0_GPIOS; }

void jh_hal_rgb_led_release_transport(void) {
  if (s_transport.pio == nullptr || s_transport.state_machine < 0) {
    return;
  }
  pio_remove_program_and_unclaim_sm(&ws2812_program, s_transport.pio,
                                    (uint)s_transport.state_machine,
                                    s_transport.program_offset);
  s_transport = {nullptr, -1, 0u};
}

hal_status_t jh_hal_rgb_led_prepare_transport(uint8_t pin, bool is800khz) {
  jh_hal_rgb_led_release_transport();
  PIO pio = nullptr;
  uint state_machine = 0u;
  uint offset = 0u;
  if (!pio_claim_free_sm_and_add_program_for_gpio_range(
          &ws2812_program, &pio, &state_machine, &offset, pin, 1u, true)) {
    return HAL_ENOMEM;
  }
  ws2812_program_init(pio, state_machine, offset, pin,
                      is800khz ? 800000.0f : 400000.0f, 8u);
  s_transport = {pio, (int)state_machine, offset};
  return HAL_OK;
}

bool jh_hal_rgb_led_write_pixels(const uint8_t *pixels, uint32_t num_bytes,
                                 bool is800khz, uint8_t pin, void *user) {
  (void)user;
  if (s_transport.pio == nullptr &&
      hal_status_is_error(jh_hal_rgb_led_prepare_transport(pin, is800khz))) {
    return false;
  }
  for (uint32_t i = 0u; i < num_bytes; ++i) {
    pio_sm_put_blocking(s_transport.pio, (uint)s_transport.state_machine,
                        (uint32_t)pixels[i] << 24u);
  }
  return true;
}

#endif
#endif
