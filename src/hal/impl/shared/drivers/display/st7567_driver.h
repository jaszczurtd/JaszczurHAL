#pragma once

/*
 * Sitronix ST7567 monochrome LCD driver over HAL I2C or SPI/GPIO.
 *
 * Register definitions and init ordering follow the local Zephyr
 * drivers/display/display_st7567.c and display_st7567_regs.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef JH_ST7567_DEFAULT_I2C_HZ
#define JH_ST7567_DEFAULT_I2C_HZ 400000UL
#endif

#ifndef JH_ST7567_DEFAULT_SPI_HZ
#define JH_ST7567_DEFAULT_SPI_HZ 8000000UL
#endif

typedef enum {
  JH_ST7567_BUS_I2C = 0,
  JH_ST7567_BUS_SPI,
} jh_st7567_bus_t;

typedef enum {
  JH_ST7567_PIXEL_MONO10 = 0,
  JH_ST7567_PIXEL_MONO01,
} jh_st7567_pixel_format_t;

typedef struct {
  jh_st7567_bus_t bus_type;
  uint8_t bus;
  uint8_t i2c_addr;
  int16_t rst_pin;
  int16_t spi_dc_pin;
  int16_t spi_cs_pin;
  uint32_t clock_hz;
  uint8_t spi_mode;
  uint16_t width;
  uint16_t height;
  uint8_t column_offset;
  uint8_t line_offset;
  uint8_t regulation_ratio;
  bool segment_invdir;
  bool com_invdir;
  bool inversion_on;
  bool bias;
  jh_st7567_pixel_format_t pixel_format;
} jh_st7567_config_t;

typedef struct {
  jh_st7567_config_t config;
  uint16_t width;
  uint16_t height;
  bool initialized;
  bool suspended;
} jh_st7567_t;

bool jh_st7567_init(jh_st7567_t *dev, const jh_st7567_config_t *config);
bool jh_st7567_display(jh_st7567_t *dev, const uint8_t *buffer);
bool jh_st7567_write(jh_st7567_t *dev, uint16_t x, uint16_t y, uint16_t w,
                     uint16_t h, const uint8_t *buffer, size_t buf_size);
bool jh_st7567_set_contrast(jh_st7567_t *dev, uint8_t contrast);
bool jh_st7567_set_pixel_format(jh_st7567_t *dev,
                                jh_st7567_pixel_format_t format);
bool jh_st7567_suspend(jh_st7567_t *dev);
bool jh_st7567_resume(jh_st7567_t *dev);
size_t jh_st7567_buffer_size(const jh_st7567_t *dev);

#ifdef __cplusplus
}
#endif
