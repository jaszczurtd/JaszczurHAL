#pragma once

#include "hal/hal_spi_device.h"

/*
 * ILI9341 TFT panel driver.
 *
 * The controller command sequences and addressing logic are adapted from the
 * Adafruit ILI9341 library (BSD license).  This implementation drives the
 * panel exclusively over the JaszczurHAL SPI / GPIO buses and pairs with the
 * shared GFX engine (jh_gfx.*) for rendering, so it is shared identically by
 * every backend.
 *
 * Original work: https://github.com/adafruit/Adafruit_ILI9341
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JH_ILI9341_TFTWIDTH 240u
#define JH_ILI9341_TFTHEIGHT 320u

/* Overridable so a project (e.g. via compiler -D flags) can raise the TFT SPI
 * clock for higher frame throughput. */
#ifndef JH_ILI9341_SPI_DEFAULT_HZ
#define JH_ILI9341_SPI_DEFAULT_HZ 24000000UL
#endif

typedef struct {
  uint8_t bus;
  int16_t cs_pin;
  int16_t dc_pin;
  int16_t rst_pin;
  uint32_t clock_hz;
} jh_ili9341_config_t;

typedef struct {
  jh_ili9341_config_t config;
  hal_spi_device_t spi_device;
  uint16_t width;
  uint16_t height;
  uint8_t rotation;
  bool initialized;
  bool inverted;
  bool write_active;
} jh_ili9341_t;

typedef bool (*jh_ili9341_command_write_fn)(void *ctx, uint8_t command,
                                            const uint8_t *data,
                                            uint8_t data_len);
typedef void (*jh_ili9341_delay_ms_fn)(void *ctx, uint32_t delay_ms);

typedef struct {
  void *ctx;
  jh_ili9341_command_write_fn write_command;
  jh_ili9341_delay_ms_fn delay_ms;
} jh_ili9341_command_io_t;

bool jh_ili9341_run_init_sequence(const jh_ili9341_command_io_t *io,
                                  uint32_t delay_ms);

bool jh_ili9341_init(jh_ili9341_t *dev, const jh_ili9341_config_t *config);
bool jh_ili9341_soft_init(jh_ili9341_t *dev, uint32_t delay_ms);
bool jh_ili9341_set_rotation(jh_ili9341_t *dev, uint8_t rotation);
bool jh_ili9341_invert(jh_ili9341_t *dev, bool invert);
bool jh_ili9341_set_addr_window(jh_ili9341_t *dev, uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h);
bool jh_ili9341_write_pixels(jh_ili9341_t *dev, const uint16_t *pixels,
                             size_t count);
bool jh_ili9341_begin_write(jh_ili9341_t *dev, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h);
bool jh_ili9341_write_pixels_fast(jh_ili9341_t *dev, const uint16_t *pixels,
                                  size_t count);
bool jh_ili9341_write_pixels_be(jh_ili9341_t *dev, const uint8_t *pixels_be,
                                size_t byte_count);
bool jh_ili9341_write_pixels_dma(jh_ili9341_t *dev, const uint8_t *pixels_be,
                                 size_t byte_count);
bool jh_ili9341_write_pixels_dma_async_start(jh_ili9341_t *dev,
                                             const uint8_t *pixels_be,
                                             size_t byte_count);
bool jh_ili9341_write_pixels_dma_async_busy(jh_ili9341_t *dev);
bool jh_ili9341_write_pixels_dma_async_wait(jh_ili9341_t *dev);
bool jh_ili9341_end_write(jh_ili9341_t *dev);
bool jh_ili9341_fill_rect(jh_ili9341_t *dev, uint16_t x, uint16_t y, uint16_t w,
                          uint16_t h, uint16_t color);
bool jh_ili9341_draw_rgb_bitmap(jh_ili9341_t *dev, uint16_t x, uint16_t y,
                                const uint16_t *pixels, uint16_t w, uint16_t h);

#ifdef __cplusplus
}
#endif
