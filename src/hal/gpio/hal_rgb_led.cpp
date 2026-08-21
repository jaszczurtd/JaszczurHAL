#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_RGB_LED

#include "hal/core/hal_mutex_once.h"
#include "hal/gpio/hal_rgb_led.h"
#include "hal/gpio/hal_rgb_led_internal.h"
#include "hal/gpio/neopixel/jh_neopixel.h"
#include "hal/system/hal_sync.h"

namespace {

jh_neopixel_t s_strip;
bool s_strip_ready = false;
int s_last_color = -1;
uint8_t s_brightness = 30u;
hal_mutex_t s_rgb_mutex = nullptr;

bool pixel_type_valid(hal_rgb_led_pixel_type_t pixel_type) {
  return pixel_type == HAL_RGB_LED_PIXEL_RGB_KHZ800 ||
         pixel_type == HAL_RGB_LED_PIXEL_GRB_KHZ800 ||
         pixel_type == HAL_RGB_LED_PIXEL_RGBW_KHZ800;
}

hal_status_t lock_rgb(void) {
  if (jh_hal_mutex_create_once(&s_rgb_mutex) == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_rgb_mutex);
  return HAL_OK;
}

void unlock_rgb(void) { hal_mutex_unlock(s_rgb_mutex); }

hal_status_t init_strip(uint8_t pin, uint8_t num_pixels, uint16_t neo_type) {
  if (s_strip_ready) {
    jh_neopixel_deinit(&s_strip);
    jh_hal_rgb_led_release_transport();
    s_strip_ready = false;
  }
  if (!jh_neopixel_init(&s_strip, num_pixels, pin, neo_type)) {
    return HAL_ENOMEM;
  }
  const hal_status_t status =
      jh_hal_rgb_led_prepare_transport(pin, s_strip.is800khz);
  if (hal_status_is_error(status)) {
    jh_neopixel_deinit(&s_strip);
    jh_hal_rgb_led_release_transport();
    return status;
  }
  s_strip_ready = true;
  s_last_color = -1;
  return HAL_OK;
}

} // namespace

hal_status_t hal_rgb_led_init(uint8_t pin, uint8_t num_pixels) {
  return hal_rgb_led_init_ex(pin, num_pixels, HAL_RGB_LED_PIXEL_RGB_KHZ800);
}

hal_status_t hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels,
                                 hal_rgb_led_pixel_type_t pixel_type) {
  if (!jh_hal_rgb_led_pin_valid(pin) || num_pixels == 0u ||
      !pixel_type_valid(pixel_type)) {
    return HAL_EINVAL;
  }
  const hal_status_t lock_status = lock_rgb();
  if (hal_status_is_error(lock_status)) {
    return lock_status;
  }
  const hal_status_t status =
      init_strip(pin, num_pixels, static_cast<uint16_t>(pixel_type));
  unlock_rgb();
  return status;
}

void hal_rgb_led_set_brightness(uint8_t brightness) {
  if (hal_status_is_error(lock_rgb())) {
    return;
  }
  s_brightness = brightness < 1u ? 1u : brightness;
  s_last_color = -1;
  unlock_rgb();
}

hal_status_t hal_rgb_led_off(void) {
  return hal_rgb_led_set_color(HAL_RGB_LED_NONE);
}

hal_status_t hal_rgb_led_set_color(hal_rgb_led_color_t color) {
  if (color < HAL_RGB_LED_NONE || color > HAL_RGB_LED_PURPLE) {
    return HAL_EINVAL;
  }
  const hal_status_t lock_status = lock_rgb();
  if (hal_status_is_error(lock_status)) {
    return lock_status;
  }
  if (!s_strip_ready) {
    unlock_rgb();
    return HAL_EUNINIT;
  }
  if (static_cast<int>(color) == s_last_color) {
    unlock_rgb();
    return HAL_OK;
  }

  uint8_t red = 0u;
  uint8_t green = 0u;
  uint8_t blue = 0u;
  switch (color) {
  case HAL_RGB_LED_RED:
    red = s_brightness;
    break;
  case HAL_RGB_LED_GREEN:
    green = s_brightness;
    break;
  case HAL_RGB_LED_BLUE:
    blue = s_brightness;
    break;
  case HAL_RGB_LED_YELLOW:
    red = s_brightness;
    green = s_brightness;
    break;
  case HAL_RGB_LED_WHITE:
    red = s_brightness;
    green = s_brightness;
    blue = s_brightness;
    break;
  case HAL_RGB_LED_PURPLE:
    red = s_brightness;
    blue = s_brightness;
    break;
  default:
    break;
  }

  jh_neopixel_set_pixel_color_packed(&s_strip, 0u,
                                     jh_neopixel_color(red, green, blue));
  const bool shown =
      jh_neopixel_show(&s_strip, jh_hal_rgb_led_write_pixels, nullptr);
  if (shown) {
    s_last_color = static_cast<int>(color);
  }
  unlock_rgb();
  return shown ? HAL_OK : HAL_EIO;
}

#endif
#endif
