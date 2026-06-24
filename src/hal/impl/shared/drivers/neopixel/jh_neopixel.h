#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portable NeoPixel core used by HAL RGB LED backends.
 *
 * This module preserves the proven Adafruit_NeoPixel data/brightness logic
 * while moving transport details to per-target callbacks implemented on top
 * of JaszczurHAL primitives.
 *
 * Attribution: logic adapted from Adafruit_NeoPixel by Phil "Paint Your
 * Dragon" Burgess and contributors. See COPYING in this folder.
 */

typedef bool (*jh_neopixel_write_fn)(const uint8_t *pixels, uint32_t num_bytes,
                                     bool is800khz, uint8_t pin, void *user);

typedef struct {
  uint16_t num_leds;
  uint16_t num_bytes;
  uint8_t pin;
  uint8_t brightness; /* Adafruit-compatible: b + 1, where 0 == full scale */
  uint8_t r_offset;
  uint8_t g_offset;
  uint8_t b_offset;
  uint8_t w_offset;
  bool is800khz;
  uint32_t end_time_us;
  uint8_t *pixels;
} jh_neopixel_t;

bool jh_neopixel_init(jh_neopixel_t *strip, uint16_t n, uint8_t pin,
                      uint16_t neo_type);
void jh_neopixel_deinit(jh_neopixel_t *strip);
void jh_neopixel_update_type(jh_neopixel_t *strip, uint16_t neo_type);
void jh_neopixel_set_pixel_color_rgb(jh_neopixel_t *strip, uint16_t n,
                                     uint8_t r, uint8_t g, uint8_t b);
void jh_neopixel_set_pixel_color_rgbw(jh_neopixel_t *strip, uint16_t n,
                                      uint8_t r, uint8_t g, uint8_t b,
                                      uint8_t w);
void jh_neopixel_set_pixel_color_packed(jh_neopixel_t *strip, uint16_t n,
                                        uint32_t c);
uint32_t jh_neopixel_color(uint8_t r, uint8_t g, uint8_t b);
void jh_neopixel_set_brightness(jh_neopixel_t *strip, uint8_t brightness);
void jh_neopixel_clear(jh_neopixel_t *strip);
bool jh_neopixel_can_show(jh_neopixel_t *strip);
bool jh_neopixel_show(jh_neopixel_t *strip, jh_neopixel_write_fn writer,
                      void *user);

#ifdef __cplusplus
}
#endif
