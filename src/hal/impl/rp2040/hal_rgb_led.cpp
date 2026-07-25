#include "../../hal_target.h"
#if HAL_TARGET_IS_RP
#include "../../hal_config.h"
#ifdef HAL_ENABLE_RGB_LED

#include "../../hal_gpio.h"
#include "../../hal_rgb_led.h"
#include "../../hal_sync.h"
#include "../shared/drivers/neopixel/jh_neopixel.h"
#include "../shared/drivers/neopixel/rp2040_pio.h"
#include "../shared/hal_mutex_once.h"

struct rgb_rp2040_transport_t {
  PIO pio;
  int pio_sm;
  uint pio_program_offset;
};

static jh_neopixel_t s_strip;
static bool s_strip_ready = false;
static bool s_transport_ready = false;
static rgb_rp2040_transport_t s_transport = {NULL, -1, 0};
static int s_last_color = -1;
static uint8_t s_brightness = 30;
static hal_mutex_t s_rgb_mutex = NULL;

static bool rgb_pixel_type_valid(hal_rgb_led_pixel_type_t pixel_type) {
  return pixel_type == HAL_RGB_LED_PIXEL_RGB_KHZ800 ||
         pixel_type == HAL_RGB_LED_PIXEL_GRB_KHZ800 ||
         pixel_type == HAL_RGB_LED_PIXEL_RGBW_KHZ800;
}

static hal_status_t rgb_lock(void) {
  if (jh_hal_mutex_create_once(&s_rgb_mutex) == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_rgb_mutex);
  return HAL_OK;
}

static void rgb_unlock(void) { hal_mutex_unlock(s_rgb_mutex); }

static void rp2040_release_transport(void) {
  if (!s_transport_ready || !s_transport.pio || s_transport.pio_sm < 0) {
    return;
  }

  pio_remove_program_and_unclaim_sm(&ws2812_program, s_transport.pio,
                                    (uint)s_transport.pio_sm,
                                    s_transport.pio_program_offset);
  s_transport.pio = NULL;
  s_transport.pio_sm = -1;
  s_transport.pio_program_offset = 0u;
  s_transport_ready = false;
}

static bool rp2040_init_transport(uint8_t pin, bool is800khz) {
  rp2040_release_transport();

  PIO pio = NULL;
  uint sm = 0u;
  uint offset = 0u;
  if (!pio_claim_free_sm_and_add_program_for_gpio_range(
          &ws2812_program, &pio, &sm, &offset, pin, 1, true)) {
    return false;
  }

  ws2812_program_init(pio, sm, offset, pin, is800khz ? 800000.0f : 400000.0f,
                      8u);

  s_transport.pio = pio;
  s_transport.pio_sm = (int)sm;
  s_transport.pio_program_offset = offset;
  s_transport_ready = true;
  return true;
}

static bool rp2040_write_pixels(const uint8_t *pixels, uint32_t num_bytes,
                                bool is800khz, uint8_t pin, void *user) {
  (void)user;

  if (!s_transport_ready) {
    if (!rp2040_init_transport(pin, is800khz)) {
      return false;
    }
  }

  for (uint32_t i = 0; i < num_bytes; ++i) {
    pio_sm_put_blocking(s_transport.pio, (uint)s_transport.pio_sm,
                        ((uint32_t)pixels[i]) << 24);
  }
  return true;
}

static hal_status_t s_init_with_type(uint8_t pin, uint8_t num_pixels,
                                     uint16_t neo_type) {
  if (s_strip_ready) {
    jh_neopixel_deinit(&s_strip);
    s_strip_ready = false;
  }

  const bool ok = jh_neopixel_init(&s_strip, num_pixels, pin, neo_type);
  if (!ok) {
    rp2040_release_transport();
    return HAL_ENOMEM;
  }

  if (!rp2040_init_transport(pin, s_strip.is800khz)) {
    jh_neopixel_deinit(&s_strip);
    s_strip_ready = false;
    return HAL_ENOMEM;
  }

  // Leave pin mux owned by PIO. Reconfiguring to SIO GPIO here breaks
  // WS2812 output on RP2040.
  s_strip_ready = true;
  s_last_color = -1;
  return HAL_OK;
}

hal_status_t hal_rgb_led_init(uint8_t pin, uint8_t num_pixels) {
  return hal_rgb_led_init_ex(pin, num_pixels, HAL_RGB_LED_PIXEL_RGB_KHZ800);
}

hal_status_t hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels,
                                 hal_rgb_led_pixel_type_t pixel_type) {
  if (pin >= NUM_BANK0_GPIOS || num_pixels == 0u ||
      !rgb_pixel_type_valid(pixel_type)) {
    return HAL_EINVAL;
  }
  const hal_status_t lock_status = rgb_lock();
  if (hal_status_is_error(lock_status)) {
    return lock_status;
  }
  const hal_status_t status =
      s_init_with_type(pin, num_pixels, (uint16_t)pixel_type);
  rgb_unlock();
  return status;
}

void hal_rgb_led_set_brightness(uint8_t brightness) {
  if (hal_status_is_error(rgb_lock())) {
    return;
  }
  s_brightness = (brightness < 1u) ? 1u : brightness;
  s_last_color = -1; // force redraw on next set_color call
  rgb_unlock();
}

hal_status_t hal_rgb_led_off(void) {
  return hal_rgb_led_set_color(HAL_RGB_LED_NONE);
}

hal_status_t hal_rgb_led_set_color(hal_rgb_led_color_t color) {
  if (color < HAL_RGB_LED_NONE || color > HAL_RGB_LED_PURPLE) {
    return HAL_EINVAL;
  }
  const hal_status_t lock_status = rgb_lock();
  if (hal_status_is_error(lock_status)) {
    return lock_status;
  }
  if (!s_strip_ready) {
    rgb_unlock();
    return HAL_EUNINIT;
  }
  if ((int)color == s_last_color) {
    rgb_unlock();
    return HAL_OK;
  }

  uint8_t r = 0, g = 0, b = 0;
  const uint8_t br = s_brightness;
  switch (color) {
  case HAL_RGB_LED_RED:
    r = br;
    break;
  case HAL_RGB_LED_GREEN:
    g = br;
    break;
  case HAL_RGB_LED_BLUE:
    b = br;
    break;
  case HAL_RGB_LED_YELLOW:
    r = br;
    g = br;
    break;
  case HAL_RGB_LED_WHITE:
    r = br;
    g = br;
    b = br;
    break;
  case HAL_RGB_LED_PURPLE:
    r = br;
    b = br;
    break;
  default:
    break; // HAL_RGB_LED_NONE: r=g=b=0
  }

  jh_neopixel_set_pixel_color_packed(&s_strip, 0u, jh_neopixel_color(r, g, b));
  const bool shown = jh_neopixel_show(&s_strip, rp2040_write_pixels, NULL);
  if (shown) {
    s_last_color = (int)color;
  }
  rgb_unlock();
  return shown ? HAL_OK : HAL_EIO;
}

#endif /* HAL_ENABLE_RGB_LED */
#endif // HAL_TARGET_IS_RP
