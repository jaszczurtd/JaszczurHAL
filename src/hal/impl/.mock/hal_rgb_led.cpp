#include "../../hal_rgb_led.h"
#include "hal_mock.h"

static bool               s_initialized = false;
static hal_rgb_led_color_t s_last_color = HAL_RGB_LED_NONE;
static uint8_t            s_brightness = 30;
static hal_rgb_led_pixel_type_t s_pixel_type = HAL_RGB_LED_PIXEL_RGB_KHZ800;
static uint8_t            s_pin = 0;
static uint8_t            s_num_pixels = 0;

void hal_rgb_led_init(uint8_t pin, uint8_t num_pixels) {
    hal_rgb_led_init_ex(pin, num_pixels, HAL_RGB_LED_PIXEL_RGB_KHZ800);
}

void hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels,
                         hal_rgb_led_pixel_type_t pixel_type) {
    s_initialized = true;
    s_pin = pin;
    s_num_pixels = num_pixels;
    s_pixel_type = pixel_type;
    s_last_color  = HAL_RGB_LED_NONE;
}

void hal_rgb_led_set_brightness(uint8_t brightness) {
    s_brightness = (brightness < 1u) ? 1u : brightness;
}

void hal_rgb_led_off(void) {
    hal_rgb_led_set_color(HAL_RGB_LED_NONE);
}

void hal_rgb_led_set_color(hal_rgb_led_color_t color) {
    if (!s_initialized) return;
    s_last_color = color;
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

bool               hal_mock_rgb_led_is_initialized(void) { return s_initialized; }
hal_rgb_led_color_t hal_mock_rgb_led_get_color(void)     { return s_last_color;  }
uint8_t            hal_mock_rgb_led_get_brightness(void) { return s_brightness;  }
hal_rgb_led_pixel_type_t hal_mock_rgb_led_get_pixel_type(void) { return s_pixel_type; }
uint8_t            hal_mock_rgb_led_get_pin(void)        { return s_pin; }
uint8_t            hal_mock_rgb_led_get_num_pixels(void) { return s_num_pixels; }

void hal_mock_rgb_led_reset(void) {
    s_initialized = false;
    s_last_color  = HAL_RGB_LED_NONE;
    s_brightness  = 30;
    s_pixel_type  = HAL_RGB_LED_PIXEL_RGB_KHZ800;
    s_pin         = 0;
    s_num_pixels  = 0;
}
