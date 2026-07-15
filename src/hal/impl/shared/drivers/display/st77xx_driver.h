#pragma once

/*
 * ST7735 / ST7789 / ST7796S / GC9A01 TFT panel driver.
 *
 * The ST77xx command sequences and addressing logic are adapted from the
 * Adafruit ST7735/ST7789 library (BSD license). GC9A01 behavior follows the
 * local Zephyr GC9x01x display driver as a reference checklist. This
 * implementation drives the panels exclusively over the JaszczurHAL SPI / GPIO
 * buses and pairs with the shared GFX engine (jh_gfx.*) for rendering, so it is
 * shared identically by every backend.
 *
 * Original work: https://github.com/adafruit/Adafruit-ST7735-Library
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef JH_ST77XX_SPI_DEFAULT_HZ
#define JH_ST77XX_SPI_DEFAULT_HZ 32000000UL
#endif

#define JH_ST7735_TFTWIDTH_128 128u
#define JH_ST7735_TFTWIDTH_80 80u
#define JH_ST7735_TFTHEIGHT_128 128u
#define JH_ST7735_TFTHEIGHT_160 160u
#define JH_ST7796S_TFTWIDTH 320u
#define JH_ST7796S_TFTHEIGHT 480u
#define JH_GC9A01_TFTWIDTH 240u
#define JH_GC9A01_TFTHEIGHT 240u

#define JH_ST7735_TAB_GREENTAB 0x00u
#define JH_ST7735_TAB_REDTAB 0x01u
#define JH_ST7735_TAB_BLACKTAB 0x02u
#define JH_ST7735_TAB_18GREENTAB JH_ST7735_TAB_GREENTAB
#define JH_ST7735_TAB_18REDTAB JH_ST7735_TAB_REDTAB
#define JH_ST7735_TAB_18BLACKTAB JH_ST7735_TAB_BLACKTAB
#define JH_ST7735_TAB_144GREENTAB 0x01u
#define JH_ST7735_TAB_MINI160X80 0x04u
#define JH_ST7735_TAB_HALLOWING 0x05u
#define JH_ST7735_TAB_MINI160X80_PLUGIN 0x06u
#define JH_ST7735_TAB_B 0x80u

typedef enum {
  JH_ST77XX_CHIP_ST7735 = 0,
  JH_ST77XX_CHIP_ST7789,
  JH_ST77XX_CHIP_ST7796S,
  JH_ST77XX_CHIP_GC9A01
} jh_st77xx_chip_t;

typedef struct {
  uint8_t bus;
  int16_t cs_pin;
  int16_t dc_pin;
  int16_t rst_pin;
  uint32_t clock_hz;
  uint8_t spi_mode;
  jh_st77xx_chip_t chip;
  uint16_t width;
  uint16_t height;
  uint8_t st7735_tab;
  uint8_t row_offset;
  uint8_t col_offset;
  bool bgr;
} jh_st77xx_config_t;

typedef struct {
  jh_st77xx_config_t config;
  uint16_t width;
  uint16_t height;
  uint16_t window_width;
  uint16_t window_height;
  uint8_t col_start;
  uint8_t row_start;
  uint8_t col_start2;
  uint8_t row_start2;
  uint8_t x_start;
  uint8_t y_start;
  uint8_t rotation;
  uint8_t invert_on_command;
  uint8_t invert_off_command;
  uint8_t color_order;
  bool initialized;
  bool inverted;
  bool write_active;
} jh_st77xx_t;

typedef bool (*jh_st77xx_command_write_fn)(void *ctx, uint8_t command,
                                           const uint8_t *data,
                                           uint8_t data_len);
typedef void (*jh_st77xx_delay_ms_fn)(void *ctx, uint32_t delay_ms);

typedef struct {
  void *ctx;
  jh_st77xx_command_write_fn write_command;
  jh_st77xx_delay_ms_fn delay_ms;
} jh_st77xx_command_io_t;

bool jh_st77xx_run_sequence(const jh_st77xx_command_io_t *io,
                            const uint8_t *sequence);
bool jh_st77xx_run_st7735_init_sequence(const jh_st77xx_command_io_t *io,
                                        uint8_t tab);
bool jh_st77xx_run_st7789_init_sequence(const jh_st77xx_command_io_t *io);
bool jh_st77xx_run_st7796s_init_sequence(const jh_st77xx_command_io_t *io);
bool jh_st77xx_run_gc9a01_init_sequence(const jh_st77xx_command_io_t *io);

bool jh_st77xx_init(jh_st77xx_t *dev, const jh_st77xx_config_t *config);
bool jh_st77xx_soft_init(jh_st77xx_t *dev);
bool jh_st77xx_set_rotation(jh_st77xx_t *dev, uint8_t rotation);
bool jh_st77xx_invert(jh_st77xx_t *dev, bool invert);
bool jh_st77xx_set_addr_window(jh_st77xx_t *dev, uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h);
bool jh_st77xx_write_pixels(jh_st77xx_t *dev, const uint16_t *pixels,
                            size_t count);
bool jh_st77xx_begin_write(jh_st77xx_t *dev, uint16_t x, uint16_t y, uint16_t w,
                           uint16_t h);
bool jh_st77xx_write_pixels_fast(jh_st77xx_t *dev, const uint16_t *pixels,
                                 size_t count);
bool jh_st77xx_write_pixels_be(jh_st77xx_t *dev, const uint8_t *pixels_be,
                               size_t byte_count);
bool jh_st77xx_write_pixels_dma(jh_st77xx_t *dev, const uint8_t *pixels_be,
                                size_t byte_count);
bool jh_st77xx_write_pixels_dma_async_start(jh_st77xx_t *dev,
                                            const uint8_t *pixels_be,
                                            size_t byte_count);
bool jh_st77xx_write_pixels_dma_async_busy(jh_st77xx_t *dev);
bool jh_st77xx_write_pixels_dma_async_wait(jh_st77xx_t *dev);
bool jh_st77xx_end_write(jh_st77xx_t *dev);
bool jh_st77xx_fill_rect(jh_st77xx_t *dev, uint16_t x, uint16_t y, uint16_t w,
                         uint16_t h, uint16_t color);
bool jh_st77xx_draw_rgb_bitmap(jh_st77xx_t *dev, uint16_t x, uint16_t y,
                               const uint16_t *pixels, uint16_t w, uint16_t h);

#ifdef __cplusplus
}
#endif
