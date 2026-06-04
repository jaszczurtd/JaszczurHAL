#include <hal/hal_app.h>
#include <hal/hal_rgb_led.h>
#include <hal/hal_serial.h>
#include <hal/hal_system.h>

static const uint8_t RGB_LED_PIN = 16;
static const uint8_t RGB_LED_PIXELS = 1;

static const hal_rgb_led_color_t COLORS[] = {
    HAL_RGB_LED_RED,
    HAL_RGB_LED_GREEN,
    HAL_RGB_LED_BLUE,
    HAL_RGB_LED_YELLOW,
    HAL_RGB_LED_PURPLE,
    HAL_RGB_LED_WHITE,
    HAL_RGB_LED_NONE,
};

static uint32_t last_change_ms = 0;
static uint8_t color_index = 0;

void app_start(void) {
    hal_debug_init(115200, NULL);
    hal_rgb_led_init_ex(RGB_LED_PIN, RGB_LED_PIXELS, HAL_RGB_LED_PIXEL_GRB_KHZ800);
    hal_rgb_led_set_brightness(24);
}

void app_task0(void) {
    const uint32_t now = hal_millis();
    if (now - last_change_ms < 500u) {
        return;
    }
    last_change_ms = now;

    const hal_rgb_led_color_t color = COLORS[color_index];
    hal_rgb_led_set_color(color);
    hal_deb("RGB LED color=%u", (unsigned)color);

    color_index = (uint8_t)((color_index + 1u) %
                            (sizeof(COLORS) / sizeof(COLORS[0])));
}
