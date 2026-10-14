#pragma once

#include "hal/core/hal_config.h"
#include "hal/core/hal_status.h"
#ifdef HAL_ENABLE_RGB_LED

/**
 * @file hal_rgb_led.h
 * @brief Hardware abstraction for a single-pixel NeoPixel RGB status LED.
 *
 * Hardware implementation uses the shared NeoPixel driver under
 * `src/hal/gpio/neopixel/`; the mock implementation is a no-op
 * stub that records the last requested colour for unit testing.
 */

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Colour constants for the onboard status LED. */
typedef enum {
  HAL_RGB_LED_NONE = 0,
  HAL_RGB_LED_RED = 1,
  HAL_RGB_LED_GREEN = 2,
  HAL_RGB_LED_YELLOW = 3,
  HAL_RGB_LED_WHITE = 4,
  HAL_RGB_LED_BLUE = 5,
  HAL_RGB_LED_PURPLE = 6,
} hal_rgb_led_color_t;

/**
 * @brief Pixel colour-order / speed flags for hal_rgb_led_init_ex().
 *
 * Values intentionally mirror the historical NeoPixel NEO_* layout so existing
 * project configuration values remain source-compatible.
 */
typedef enum {
  HAL_RGB_LED_PIXEL_RGB_KHZ800 = 0x0006, /**< RGB byte order, 800 kHz. */
  HAL_RGB_LED_PIXEL_GRB_KHZ800 =
      0x0052, /**< GRB byte order, 800 kHz (WS2812B, e.g. RP2040-Zero). */
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
 * @return HAL_OK on success, HAL_EINVAL for invalid configuration,
 *         HAL_ENOMEM when backend resources cannot be allocated, or a backend
 *         setup error.
 */
hal_status_t hal_rgb_led_init(uint8_t pin, uint8_t num_pixels);

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
 * @return HAL_OK on success, HAL_EINVAL for invalid configuration,
 *         HAL_ENOMEM when backend resources cannot be allocated, or a backend
 *         setup error.
 */
hal_status_t hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels,
                                 hal_rgb_led_pixel_type_t pixel_type);

#if (defined(JH_HW_NODE_BOARD_STATUS_LED_PIN_DATA) &&                          \
     defined(JH_HW_NODE_BOARD_STATUS_LED_PROP_PIXEL_ORDER)) ||                 \
    (defined(HAL_BOARD_STATUS_LED_KIND_ADDRESSABLE) &&                         \
     (defined(HAL_BOARD_STATUS_LED_PIXEL_ORDER_RGB) ||                         \
      defined(HAL_BOARD_STATUS_LED_PIXEL_ORDER_GRB)))
/**
 * @brief Initialise the board's addressable status LED.
 *
 * With the assembly's `board.statusLed` node (jh_hardware.h of a device tree
 * build, also loaded by host tests) the function uses its data pin, pixel
 * count and colour order. Otherwise it starts HAL_BOARD_STATUS_LED_PIN as one
 * pixel in the colour order of the board, or of the assembly when the board
 * leaves it to the project (`statusLed.pixelOrder`). Defined only when one of
 * them describes an addressable LED with a known colour order, so on a board
 * with a plain GPIO LED the call fails at compile time.
 *
 * @return The status of hal_rgb_led_init_ex().
 */
static inline hal_status_t hal_rgb_led_init_board_status(void) {
#if defined(JH_HW_NODE_BOARD_STATUS_LED_PIN_DATA) &&                           \
    defined(JH_HW_NODE_BOARD_STATUS_LED_PROP_PIXEL_ORDER)
  /* The board allows "RGB" or "GRB"; the comparison folds at compile time. */
  const hal_rgb_led_pixel_type_t type =
      (strcmp(JH_HW_NODE_BOARD_STATUS_LED_PROP_PIXEL_ORDER, "RGB") == 0)
          ? HAL_RGB_LED_PIXEL_RGB_KHZ800
          : HAL_RGB_LED_PIXEL_GRB_KHZ800;
#if defined(JH_HW_NODE_BOARD_STATUS_LED_PROP_COUNT)
  const uint8_t pixels = (uint8_t)JH_HW_NODE_BOARD_STATUS_LED_PROP_COUNT;
#else
  const uint8_t pixels = 1u;
#endif
  return hal_rgb_led_init_ex((uint8_t)JH_HW_NODE_BOARD_STATUS_LED_PIN_DATA,
                             pixels, type);
#elif defined(HAL_BOARD_STATUS_LED_PIXEL_ORDER_RGB)
  return hal_rgb_led_init_ex((uint8_t)HAL_BOARD_STATUS_LED_PIN, 1u,
                             HAL_RGB_LED_PIXEL_RGB_KHZ800);
#else
  return hal_rgb_led_init_ex((uint8_t)HAL_BOARD_STATUS_LED_PIN, 1u,
                             HAL_RGB_LED_PIXEL_GRB_KHZ800);
#endif
}
#endif

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
 * @brief Turn the LED off (equivalent to
 * hal_rgb_led_set_color(HAL_RGB_LED_NONE)).
 * @return HAL_OK on success or the error returned by hal_rgb_led_set_color().
 */
hal_status_t hal_rgb_led_off(void);

/**
 * @brief Set the status LED colour.
 * Repeated calls with the same colour are suppressed - the LED is only
 * updated when the colour actually changes, keeping SPI/DMA traffic minimal.
 * @param color Desired colour.
 * @return HAL_OK on success, HAL_EINVAL for an invalid colour, HAL_EUNINIT
 *         before initialisation, or HAL_EIO on a transport failure.
 */
hal_status_t hal_rgb_led_set_color(hal_rgb_led_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_RGB_LED */
