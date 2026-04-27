#pragma once

#include "hal_config.h"
#ifndef HAL_DISABLE_RGB_LED

/**
 * @file hal_rgb_led.h
 * @brief Hardware abstraction for a single-pixel NeoPixel RGB status LED.
 *
 * Hardware implementation uses Adafruit_NeoPixel; the mock implementation
 * is a no-op stub that records the last requested colour for unit testing.
 * Project code only depends on this header - no Adafruit headers are required.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Colour constants for the onboard status LED. */
typedef enum {
    HAL_RGB_LED_NONE   = 0,
    HAL_RGB_LED_RED    = 1,
    HAL_RGB_LED_GREEN  = 2,
    HAL_RGB_LED_YELLOW = 3,
    HAL_RGB_LED_WHITE  = 4,
    HAL_RGB_LED_BLUE   = 5,
    HAL_RGB_LED_PURPLE = 6,
} hal_rgb_led_color_t;

/**
 * @brief Pixel colour-order / speed flags for hal_rgb_led_init_ex().
 *
 * Values intentionally mirror the matching Adafruit_NeoPixel NEO_* constants
 * so the HAL implementation can cast them directly - but project code never
 * needs to include Adafruit headers.
 */
typedef enum {
    HAL_RGB_LED_PIXEL_RGB_KHZ800 = 0x0006, /**< RGB byte order, 800 kHz. */
    HAL_RGB_LED_PIXEL_GRB_KHZ800 = 0x0052, /**< GRB byte order, 800 kHz (WS2812B, e.g. RP2040-Zero). */
    HAL_RGB_LED_PIXEL_RGBW_KHZ800 = 0x0018, /**< RGBW byte order, 800 kHz. */
} hal_rgb_led_pixel_type_t;

/**
 * @brief Initialise the LED driver and light the LED with HAL_RGB_LED_NONE.
 *
 * Assumes RGB byte order (NEO_RGB, 800 kHz).  For WS2812B or other GRB
 * strips use hal_rgb_led_init_ex() with HAL_RGB_LED_PIXEL_GRB_KHZ800.
 *
 * @param pin        GPIO pin connected to the data line of the NeoPixel.
 * @param num_pixels Number of pixels in the chain (typically 1).
 */
void hal_rgb_led_init(uint8_t pin, uint8_t num_pixels);

/**
 * @brief Initialise the LED driver with an explicit pixel type.
 *
 * Use this instead of hal_rgb_led_init() when the LED strip uses a colour
 * order other than RGB (most WS2812B are GRB).  Existing code that calls
 * hal_rgb_led_init() is unaffected.
 *
 * @param pin        GPIO pin connected to the data line.
 * @param num_pixels Number of pixels in the chain.
 * @param pixel_type Colour order and speed (hal_rgb_led_pixel_type_t).
 */
void hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels,
                         hal_rgb_led_pixel_type_t pixel_type);

/**
 * @brief Set the brightness used by hal_rgb_led_set_color().
 *
 * Clamped to [1, 255].  Default is 30 (dim, suitable for status
 * indicators).  Takes effect on the next hal_rgb_led_set_color() call.
 *
 * @param brightness Brightness in [1, 255].
 */
void hal_rgb_led_set_brightness(uint8_t brightness);

/**
 * @brief Turn the LED off (equivalent to hal_rgb_led_set_color(HAL_RGB_LED_NONE)).
 */
void hal_rgb_led_off(void);

/**
 * @brief Set the status LED colour.
 * Repeated calls with the same colour are suppressed - the LED is only
 * updated when the colour actually changes, keeping SPI/DMA traffic minimal.
 * @param color Desired colour.
 */
void hal_rgb_led_set_color(hal_rgb_led_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISABLE_RGB_LED */
