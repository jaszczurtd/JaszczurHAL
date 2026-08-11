#pragma once

/*
 * UltraChip UC81xx monochrome EPD driver over shared HAL SPI/GPIO.
 * Protocol behavior follows the local Zephyr uc81xx driver and register map.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "epd_spi_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_UC81XX_UC8175 = 0,
  JH_UC81XX_UC8176,
  JH_UC81XX_UC8151D,
  JH_UC81XX_UC8179,
} jh_uc81xx_controller_t;

typedef enum {
  JH_UC81XX_REFRESH_FULL = 0,
  JH_UC81XX_REFRESH_PARTIAL,
} jh_uc81xx_refresh_mode_t;

typedef struct {
  const uint8_t *data;
  size_t len;
} jh_uc81xx_bytes_t;

typedef struct {
  jh_uc81xx_bytes_t power;
  jh_uc81xx_bytes_t lut_vcom;
  jh_uc81xx_bytes_t lut_white_to_white;
  jh_uc81xx_bytes_t lut_black_to_white;
  jh_uc81xx_bytes_t lut_white_to_black;
  jh_uc81xx_bytes_t lut_black_to_black;
  jh_uc81xx_bytes_t lut_border;
  uint8_t cdi;
  uint8_t tcon;
  uint8_t pll;
  uint8_t vdcs;
  bool override_cdi;
  bool override_tcon;
  bool override_pll;
  bool override_vdcs;
} jh_uc81xx_profile_t;

typedef struct {
  jh_uc81xx_controller_t controller;
  jh_epd_spi_config_t transport;
  uint16_t width;
  uint16_t height;
  jh_uc81xx_bytes_t softstart;
  const jh_uc81xx_profile_t *full_profile;
  const jh_uc81xx_profile_t *partial_profile;
} jh_uc81xx_config_t;

typedef struct {
  jh_uc81xx_config_t config;
  jh_epd_spi_t transport;
  jh_uc81xx_refresh_mode_t profile;
  jh_uc81xx_refresh_mode_t pending_refresh_mode;
  bool initialized;
  bool refresh_pending;
  bool suspended;
} jh_uc81xx_t;

hal_status_t jh_uc81xx_init(jh_uc81xx_t *dev, const jh_uc81xx_config_t *config);
hal_status_t jh_uc81xx_write(jh_uc81xx_t *dev, uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             const uint8_t *buffer, size_t buf_size,
                             bool refresh);
hal_status_t jh_uc81xx_refresh(jh_uc81xx_t *dev, jh_uc81xx_refresh_mode_t mode);
hal_status_t jh_uc81xx_suspend(jh_uc81xx_t *dev);
hal_status_t jh_uc81xx_resume(jh_uc81xx_t *dev);

#ifdef __cplusplus
}
#endif
