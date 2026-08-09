#pragma once

#include "hal/hal_spi_device.h"

/*
 * SSD1331 / SSD135x RGB OLED panel driver over HAL SPI/GPIO.
 *
 * Command ordering and register choices follow the local Zephyr drivers:
 * drivers/display/display_ssd1331.c and display_ssd135x.c. The transport is
 * native JaszczurHAL SPI/GPIO, so this driver is backend-neutral.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef JH_RGB_OLED_SPI_DEFAULT_HZ
#define JH_RGB_OLED_SPI_DEFAULT_HZ 16000000UL
#endif

typedef enum {
  JH_RGB_OLED_SSD1331 = 0,
  JH_RGB_OLED_SSD1351,
  JH_RGB_OLED_SSD1357
} jh_rgb_oled_controller_t;

typedef struct {
  uint8_t bus;
  int16_t cs_pin;
  int16_t dc_pin;
  int16_t rst_pin;
  uint32_t clock_hz;
  uint8_t spi_mode;
  jh_rgb_oled_controller_t controller;
  uint16_t width;
  uint16_t height;
  uint8_t start_line;
  uint8_t display_offset;
  uint8_t multiplex_ratio;
  uint8_t phase_length;
  uint8_t oscillator_freq;
  uint8_t precharge_time_a;
  uint8_t precharge_time_b;
  uint8_t precharge_time_c;
  uint8_t precharge_time;
  uint8_t precharge_voltage;
  uint8_t vcomh_voltage;
  uint8_t current_att;
  uint8_t remap_value;
  uint8_t column_offset;
  uint8_t contrast_a;
  uint8_t contrast_b;
  uint8_t contrast_c;
  bool power_save;
  bool inverted;
} jh_rgb_oled_config_t;

typedef struct {
  jh_rgb_oled_config_t config;
  hal_spi_device_t spi_device;
  uint16_t width;
  uint16_t height;
  uint8_t rotation;
  bool initialized;
  bool write_active;
} jh_rgb_oled_t;

bool jh_rgb_oled_init(jh_rgb_oled_t *dev, const jh_rgb_oled_config_t *config);
/* Only native orientation is currently supported for raw RGB565 writes. */
bool jh_rgb_oled_set_rotation(jh_rgb_oled_t *dev, uint8_t rotation);
bool jh_rgb_oled_invert(jh_rgb_oled_t *dev, bool invert);
bool jh_rgb_oled_suspend(jh_rgb_oled_t *dev);
bool jh_rgb_oled_resume(jh_rgb_oled_t *dev);
bool jh_rgb_oled_set_contrast(jh_rgb_oled_t *dev, uint8_t contrast);
bool jh_rgb_oled_set_addr_window(jh_rgb_oled_t *dev, uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h);
bool jh_rgb_oled_begin_write(jh_rgb_oled_t *dev, uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h);
bool jh_rgb_oled_write_pixels_be(jh_rgb_oled_t *dev, const uint8_t *pixels_be,
                                 size_t byte_count);
bool jh_rgb_oled_write_pixels_fast(jh_rgb_oled_t *dev, const uint16_t *pixels,
                                   size_t count);
bool jh_rgb_oled_end_write(jh_rgb_oled_t *dev);
bool jh_rgb_oled_fill_rect(jh_rgb_oled_t *dev, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, uint16_t color);
bool jh_rgb_oled_draw_rgb_bitmap(jh_rgb_oled_t *dev, uint16_t x, uint16_t y,
                                 const uint16_t *pixels, uint16_t w,
                                 uint16_t h);

#ifdef __cplusplus
}
#endif
