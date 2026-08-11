#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/gpio/hal_rgb_led.h"
#include "hal_mock.h"

static bool s_initialized = false;
static hal_rgb_led_color_t s_last_color = HAL_RGB_LED_NONE;
static uint8_t s_brightness = 30;
static hal_rgb_led_pixel_type_t s_pixel_type = HAL_RGB_LED_PIXEL_RGB_KHZ800;
static uint8_t s_pin = 0;
static uint8_t s_num_pixels = 0;
static bool s_fail_next_init = false;
static bool s_fail_next_write = false;

static bool rgb_pixel_type_valid(hal_rgb_led_pixel_type_t pixel_type) {
  return pixel_type == HAL_RGB_LED_PIXEL_RGB_KHZ800 ||
         pixel_type == HAL_RGB_LED_PIXEL_GRB_KHZ800 ||
         pixel_type == HAL_RGB_LED_PIXEL_RGBW_KHZ800;
}

hal_status_t hal_rgb_led_init(uint8_t pin, uint8_t num_pixels) {
  return hal_rgb_led_init_ex(pin, num_pixels, HAL_RGB_LED_PIXEL_RGB_KHZ800);
}

hal_status_t hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels,
                                 hal_rgb_led_pixel_type_t pixel_type) {
  if (num_pixels == 0u || !rgb_pixel_type_valid(pixel_type)) {
    return HAL_EINVAL;
  }
  if (s_fail_next_init) {
    s_fail_next_init = false;
    return HAL_ENOMEM;
  }
  s_initialized = true;
  s_pin = pin;
  s_num_pixels = num_pixels;
  s_pixel_type = pixel_type;
  s_last_color = HAL_RGB_LED_NONE;
  return HAL_OK;
}

void hal_rgb_led_set_brightness(uint8_t brightness) {
  s_brightness = (brightness < 1u) ? 1u : brightness;
}

hal_status_t hal_rgb_led_off(void) {
  return hal_rgb_led_set_color(HAL_RGB_LED_NONE);
}

hal_status_t hal_rgb_led_set_color(hal_rgb_led_color_t color) {
  if (color < HAL_RGB_LED_NONE || color > HAL_RGB_LED_PURPLE) {
    return HAL_EINVAL;
  }
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  if (color == s_last_color) {
    return HAL_OK;
  }
  if (s_fail_next_write) {
    s_fail_next_write = false;
    return HAL_EIO;
  }
  s_last_color = color;
  return HAL_OK;
}

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

bool hal_mock_rgb_led_is_initialized(void) { return s_initialized; }
hal_rgb_led_color_t hal_mock_rgb_led_get_color(void) { return s_last_color; }
uint8_t hal_mock_rgb_led_get_brightness(void) { return s_brightness; }
hal_rgb_led_pixel_type_t hal_mock_rgb_led_get_pixel_type(void) {
  return s_pixel_type;
}
uint8_t hal_mock_rgb_led_get_pin(void) { return s_pin; }
uint8_t hal_mock_rgb_led_get_num_pixels(void) { return s_num_pixels; }

void hal_mock_rgb_led_fail_next_init(bool fail) { s_fail_next_init = fail; }
void hal_mock_rgb_led_fail_next_write(bool fail) { s_fail_next_write = fail; }

void hal_mock_rgb_led_reset(void) {
  s_initialized = false;
  s_last_color = HAL_RGB_LED_NONE;
  s_brightness = 30;
  s_pixel_type = HAL_RGB_LED_PIXEL_RGB_KHZ800;
  s_pin = 0;
  s_num_pixels = 0;
  s_fail_next_init = false;
  s_fail_next_write = false;
}
#endif // HAL_TARGET_IS_MOCK
