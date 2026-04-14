#include "../../hal_config.h"
#ifndef HAL_DISABLE_RGB_LED

#include "../../hal_rgb_led.h"
#include "../../hal_sync.h"
#include "drivers/Adafruit_NeoPixel/Adafruit_NeoPixel.h"
#include <new>

// Placement-new storage — avoids global C++ constructor ordering issues
static uint8_t            s_pixel_mem[sizeof(Adafruit_NeoPixel)]
    __attribute__((aligned(__alignof__(Adafruit_NeoPixel))));
static Adafruit_NeoPixel *s_pixels    = NULL;
static int                s_last_color = -1;
static uint8_t            s_brightness = 30;
static hal_mutex_t        s_rgb_mutex  = NULL;

static void rgb_ensure_mutex(void) {
    if (!s_rgb_mutex) {
        hal_critical_section_enter();
        if (!s_rgb_mutex) {
            s_rgb_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

static inline void rgb_lock(void) {
    rgb_ensure_mutex();
    hal_mutex_lock(s_rgb_mutex);
}

static inline void rgb_unlock(void) {
    rgb_ensure_mutex();
    hal_mutex_unlock(s_rgb_mutex);
}

static void s_init_with_type(uint8_t pin, uint8_t num_pixels, uint16_t neo_type) {
    s_pixels = new(s_pixel_mem) Adafruit_NeoPixel(num_pixels, pin, neo_type);
    s_pixels->begin();
    s_last_color = -1;
}

void hal_rgb_led_init(uint8_t pin, uint8_t num_pixels) {
    rgb_lock();
    s_init_with_type(pin, num_pixels, NEO_RGB + NEO_KHZ800);
    rgb_unlock();
}

void hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels,
                         hal_rgb_led_pixel_type_t pixel_type) {
    rgb_lock();
    s_init_with_type(pin, num_pixels, (uint16_t)pixel_type);
    rgb_unlock();
}

void hal_rgb_led_set_brightness(uint8_t brightness) {
    rgb_lock();
    s_brightness = (brightness < 1u) ? 1u : brightness;
    s_last_color = -1;  // force redraw on next set_color call
    rgb_unlock();
}

void hal_rgb_led_off(void) {
    hal_rgb_led_set_color(HAL_RGB_LED_NONE);
}

void hal_rgb_led_set_color(hal_rgb_led_color_t color) {
    rgb_lock();
    if (!s_pixels) {
        rgb_unlock();
        return;
    }
    if ((int)color == s_last_color) {
        rgb_unlock();
        return;
    }
    s_last_color = (int)color;

    uint8_t r = 0, g = 0, b = 0;
    const uint8_t br = s_brightness;
    switch (color) {
        case HAL_RGB_LED_RED:    r = br;            break;
        case HAL_RGB_LED_GREEN:  g = br;            break;
        case HAL_RGB_LED_BLUE:   b = br;            break;
        case HAL_RGB_LED_YELLOW: r = br; g = br;    break;
        case HAL_RGB_LED_WHITE:  r = br; g = br; b = br; break;
        case HAL_RGB_LED_PURPLE: r = br;         b = br; break;
        default: break;  // HAL_RGB_LED_NONE: r=g=b=0
    }

    s_pixels->setPixelColor(0, s_pixels->Color(r, g, b));
    s_pixels->show();
    rgb_unlock();
}

#endif /* HAL_DISABLE_RGB_LED */
