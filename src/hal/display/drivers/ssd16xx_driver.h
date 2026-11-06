#pragma once

/*
 * Solomon Systech SSD16xx monochrome EPD driver over shared HAL SPI/GPIO.
 * Protocol behavior follows the local Zephyr ssd16xx driver and register map.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal/core/hal_layout_check.h"

#include "epd_spi_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_SSD16XX_SSD1608 = 0,
  JH_SSD16XX_SSD1673,
  JH_SSD16XX_SSD1675A,
  JH_SSD16XX_SSD1680,
  JH_SSD16XX_SSD1681,
} jh_ssd16xx_controller_t;

typedef enum {
  JH_SSD16XX_REFRESH_FULL = 0,
  JH_SSD16XX_REFRESH_PARTIAL,
} jh_ssd16xx_refresh_mode_t;

typedef struct {
  const uint8_t *data;
  size_t len;
} jh_ssd16xx_bytes_t;

typedef struct {
  jh_ssd16xx_bytes_t lut;
  jh_ssd16xx_bytes_t gate_voltage;
  jh_ssd16xx_bytes_t source_voltage;
  uint8_t vcom;
  uint8_t border_waveform;
  uint8_t dummy_line;
  uint8_t gate_line_width;
  bool override_vcom;
  bool override_border_waveform;
  bool override_dummy_line;
  bool override_gate_line_width;
} jh_ssd16xx_profile_t;

typedef struct {
  jh_ssd16xx_controller_t controller;
  jh_epd_spi_config_t transport;
  uint16_t width;
  uint16_t height;
  uint8_t rotation;
  uint8_t temperature_sensor_selection;
  jh_ssd16xx_bytes_t softstart;
  const jh_ssd16xx_profile_t *full_profile;
  const jh_ssd16xx_profile_t *partial_profile;
} jh_ssd16xx_config_t;

typedef struct {
  jh_ssd16xx_config_t config;
  jh_epd_spi_t transport;
  jh_ssd16xx_refresh_mode_t profile;
  jh_ssd16xx_refresh_mode_t pending_refresh_mode;
  uint8_t scan_mode;
  bool initialized;
  bool refresh_pending;
  bool suspended;
} jh_ssd16xx_t;

hal_status_t jh_ssd16xx_init(jh_ssd16xx_t *dev,
                             const jh_ssd16xx_config_t *config);
hal_status_t jh_ssd16xx_write(jh_ssd16xx_t *dev, uint16_t x, uint16_t y,
                              uint16_t width, uint16_t height,
                              const uint8_t *buffer, size_t buf_size,
                              bool refresh);
/**
 * @brief Write a MONO10 rectangle with source padding and optional refresh.
 * @param dev Initialized, awake controller; NULL returns HAL_EUNINIT.
 * @param x Destination column in the current rotation.
 * @param y Destination row in the current rotation.
 * @param width Rectangle width in pixels, greater than zero.
 * @param height Rectangle height in pixels, greater than zero.
 * @param pitch Source width in pixels, at least width. At rotations 0/180,
 * this is the byte stride between 8-row pages (y and height aligned to 8).
 * At 90/270, pitch, x and width must be multiples of 8; stride is pitch / 8.
 * @param buffer Non-NULL source; final row/page padding may be omitted.
 * @param buf_size Available source bytes.
 * @param refresh Refresh once after writing; false leaves a pending full
 * refresh. Partial refresh needs a partial profile and no pending full
 * refresh; otherwise the write commits the full refresh for the whole panel.
 * @return HAL_EINVAL for invalid geometry or source size, HAL_ESTATE while
 * suspended, or the transport/refresh status.
 */
hal_status_t jh_ssd16xx_write_pitched(jh_ssd16xx_t *dev, uint16_t x, uint16_t y,
                                      uint16_t width, uint16_t height,
                                      uint16_t pitch, const uint8_t *buffer,
                                      size_t buf_size, bool refresh);
hal_status_t jh_ssd16xx_refresh(jh_ssd16xx_t *dev,
                                jh_ssd16xx_refresh_mode_t mode);
hal_status_t jh_ssd16xx_set_rotation(jh_ssd16xx_t *dev, uint8_t rotation);
hal_status_t jh_ssd16xx_suspend(jh_ssd16xx_t *dev);
hal_status_t jh_ssd16xx_resume(jh_ssd16xx_t *dev);

#ifdef __cplusplus
}
#endif
