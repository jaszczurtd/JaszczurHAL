#include "uc81xx_driver.h"

/* SPDX-License-Identifier: Apache-2.0 */

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_UC81XX

#include "hal/system/hal_system.h"

#include <string.h>

#define UC81XX_CMD_PSR 0x00u
#define UC81XX_CMD_PWR 0x01u
#define UC81XX_CMD_POF 0x02u
#define UC81XX_CMD_PON 0x04u
#define UC81XX_CMD_BTST 0x06u
#define UC81XX_CMD_DSLP 0x07u
#define UC81XX_CMD_DTM1 0x10u
#define UC81XX_CMD_DRF 0x12u
#define UC81XX_CMD_DTM2 0x13u
#define UC81XX_CMD_LUTC 0x20u
#define UC81XX_CMD_LUTWW 0x21u
#define UC81XX_CMD_LUTKW 0x22u
#define UC81XX_CMD_LUTWK 0x23u
#define UC81XX_CMD_LUTKK 0x24u
#define UC81XX_CMD_LUTBD 0x25u
#define UC81XX_CMD_PLL 0x30u
#define UC81XX_CMD_CDI 0x50u
#define UC81XX_CMD_TCON 0x60u
#define UC81XX_CMD_TRES 0x61u
#define UC81XX_CMD_VDCS 0x82u
#define UC81XX_CMD_PTL 0x90u
#define UC81XX_CMD_PTIN 0x91u
#define UC81XX_CMD_PTOUT 0x92u

#define UC81XX_PSR_REG 0x20u
#define UC81XX_PSR_KW_R 0x10u
#define UC81XX_PSR_UD 0x08u
#define UC81XX_PSR_SHL 0x04u
#define UC81XX_PSR_SHD 0x02u
#define UC81XX_PSR_RST 0x01u
#define UC81XX_PTL_SCAN 0x01u

typedef struct {
  uint16_t max_width;
  uint16_t max_height;
  bool auto_copy;
  bool power_on_after_softstart;
} uc81xx_quirks_t;

static const uc81xx_quirks_t kQuirks[] = {
    {80u, 160u, false, false},
    {400u, 300u, false, false},
    {160u, 296u, false, true},
    {800u, 600u, true, false},
};

static const uc81xx_quirks_t *quirks_for(jh_uc81xx_controller_t controller) {
  return controller <= JH_UC81XX_UC8179 ? &kQuirks[(size_t)controller] : NULL;
}

static hal_status_t command(jh_uc81xx_t *dev, uint8_t cmd, const uint8_t *data,
                            size_t len) {
  return jh_epd_spi_command(&dev->transport, cmd, data, len);
}

static hal_status_t command_u8(jh_uc81xx_t *dev, uint8_t cmd, uint8_t value) {
  return command(dev, cmd, &value, 1u);
}

static const jh_uc81xx_profile_t *profile_for(const jh_uc81xx_t *dev,
                                              jh_uc81xx_refresh_mode_t mode) {
  return mode == JH_UC81XX_REFRESH_PARTIAL ? dev->config.partial_profile
                                           : dev->config.full_profile;
}

static bool profile_has_lut(const jh_uc81xx_profile_t *profile) {
  return profile != NULL &&
         (profile->lut_vcom.len > 0u || profile->lut_white_to_white.len > 0u ||
          profile->lut_black_to_white.len > 0u ||
          profile->lut_white_to_black.len > 0u ||
          profile->lut_black_to_black.len > 0u || profile->lut_border.len > 0u);
}

static hal_status_t command_bytes(jh_uc81xx_t *dev, uint8_t cmd,
                                  const jh_uc81xx_bytes_t *bytes) {
  return bytes->len == 0u ? HAL_OK : command(dev, cmd, bytes->data, bytes->len);
}

static hal_status_t set_resolution(jh_uc81xx_t *dev) {
  uint8_t data[4];
  size_t len;
  if (dev->config.controller == JH_UC81XX_UC8175) {
    data[0] = (uint8_t)dev->config.width;
    data[1] = (uint8_t)dev->config.height;
    len = 2u;
  } else if (dev->config.controller == JH_UC81XX_UC8151D) {
    data[0] = (uint8_t)dev->config.width;
    data[1] = (uint8_t)(dev->config.height >> 8u);
    data[2] = (uint8_t)dev->config.height;
    len = 3u;
  } else {
    data[0] = (uint8_t)(dev->config.width >> 8u);
    data[1] = (uint8_t)dev->config.width;
    data[2] = (uint8_t)(dev->config.height >> 8u);
    data[3] = (uint8_t)dev->config.height;
    len = 4u;
  }
  return command(dev, UC81XX_CMD_TRES, data, len);
}

static hal_status_t set_border(jh_uc81xx_t *dev, bool enabled) {
  const jh_uc81xx_profile_t *profile = profile_for(dev, dev->profile);
  if (dev->config.controller == JH_UC81XX_UC8151D) {
    uint8_t cdi = profile != NULL && profile->override_cdi
                      ? (uint8_t)(0xD0u | (profile->cdi & 0x0Fu))
                      : 0xD7u;
    if (!enabled) {
      cdi &= 0x3Fu;
    }
    return command_u8(dev, UC81XX_CMD_CDI, cdi);
  }
  if (profile == NULL || !profile->override_cdi) {
    return HAL_OK;
  }
  if (dev->config.controller == JH_UC81XX_UC8179) {
    const uint8_t cdi[] = {(uint8_t)(0x29u | (enabled ? 0u : 0x80u)),
                           profile->cdi};
    return command(dev, UC81XX_CMD_CDI, cdi, sizeof(cdi));
  }
  uint8_t cdi = (uint8_t)(0x90u | (profile->cdi & 0x0Fu));
  if (!enabled) {
    cdi |= 0xC0u;
  }
  return command_u8(dev, UC81XX_CMD_CDI, cdi);
}

static hal_status_t set_profile(jh_uc81xx_t *dev,
                                jh_uc81xx_refresh_mode_t mode) {
  const uc81xx_quirks_t *quirks = quirks_for(dev->config.controller);
  if (quirks == NULL) {
    return HAL_ESTATE;
  }
  const jh_uc81xx_profile_t *profile = profile_for(dev, mode);
  if (mode == JH_UC81XX_REFRESH_PARTIAL && profile == NULL) {
    return HAL_EUNSUPPORTED;
  }
  if (dev->profile == mode) {
    return HAL_OK;
  }
  uint8_t psr = UC81XX_PSR_KW_R | UC81XX_PSR_UD | UC81XX_PSR_SHL |
                UC81XX_PSR_SHD | UC81XX_PSR_RST;
  hal_status_t status = HAL_OK;
  if (profile != NULL) {
    status = command_bytes(dev, UC81XX_CMD_PWR, &profile->power);
    if (hal_status_is_error(status)) {
      return status;
    }
    if (dev->config.softstart.len > 0u) {
      status = command(dev, UC81XX_CMD_BTST, dev->config.softstart.data,
                       dev->config.softstart.len);
      if (hal_status_is_error(status)) {
        return status;
      }
      if (quirks->power_on_after_softstart) {
        status = command(dev, UC81XX_CMD_PON, NULL, 0u);
        if (hal_status_is_error(status)) {
          return status;
        }
        hal_delay_ms(100u);
      }
    }
    if (profile_has_lut(profile)) {
      psr |= UC81XX_PSR_REG;
    }
  }
  status = command_u8(dev, UC81XX_CMD_PSR, psr);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = set_resolution(dev);
  if (hal_status_is_error(status)) {
    return status;
  }
  dev->profile = mode;
  status = set_border(dev, true);
  if (hal_status_is_error(status) || profile == NULL) {
    return status;
  }
  const struct {
    uint8_t command;
    const jh_uc81xx_bytes_t *bytes;
  } luts[] = {{UC81XX_CMD_LUTC, &profile->lut_vcom},
              {UC81XX_CMD_LUTWW, &profile->lut_white_to_white},
              {UC81XX_CMD_LUTKW, &profile->lut_black_to_white},
              {UC81XX_CMD_LUTWK, &profile->lut_white_to_black},
              {UC81XX_CMD_LUTKK, &profile->lut_black_to_black},
              {UC81XX_CMD_LUTBD, &profile->lut_border}};
  for (size_t i = 0u; i < sizeof(luts) / sizeof(luts[0]); ++i) {
    status = command_bytes(dev, luts[i].command, luts[i].bytes);
    if (hal_status_is_error(status)) {
      return status;
    }
  }
  if (profile->override_pll) {
    status = command_u8(dev, UC81XX_CMD_PLL, profile->pll);
    if (hal_status_is_error(status)) {
      return status;
    }
  }
  if (profile->override_vdcs) {
    status = command_u8(dev, UC81XX_CMD_VDCS, profile->vdcs);
    if (hal_status_is_error(status)) {
      return status;
    }
  }
  if (profile->override_tcon) {
    status = command_u8(dev, UC81XX_CMD_TCON, profile->tcon);
  }
  return status;
}

static hal_status_t set_partial_window(jh_uc81xx_t *dev, uint16_t x, uint16_t y,
                                       uint16_t width, uint16_t height) {
  const uint16_t x_end = (uint16_t)(x + width - 1u);
  const uint16_t y_end = (uint16_t)(y + height - 1u);
  uint8_t data[9];
  size_t len;
  if (dev->config.controller == JH_UC81XX_UC8175) {
    data[0] = (uint8_t)x;
    data[1] = (uint8_t)x_end;
    data[2] = (uint8_t)y;
    data[3] = (uint8_t)y_end;
    data[4] = UC81XX_PTL_SCAN;
    len = 5u;
  } else if (dev->config.controller == JH_UC81XX_UC8151D) {
    data[0] = (uint8_t)x;
    data[1] = (uint8_t)x_end;
    data[2] = (uint8_t)(y >> 8u);
    data[3] = (uint8_t)y;
    data[4] = (uint8_t)(y_end >> 8u);
    data[5] = (uint8_t)y_end;
    data[6] = UC81XX_PTL_SCAN;
    len = 7u;
  } else {
    data[0] = (uint8_t)(x >> 8u);
    data[1] = (uint8_t)x;
    data[2] = (uint8_t)(x_end >> 8u);
    data[3] = (uint8_t)x_end;
    data[4] = (uint8_t)(y >> 8u);
    data[5] = (uint8_t)y;
    data[6] = (uint8_t)(y_end >> 8u);
    data[7] = (uint8_t)y_end;
    data[8] = UC81XX_PTL_SCAN;
    len = 9u;
  }
  return command(dev, UC81XX_CMD_PTL, data, len);
}

hal_status_t jh_uc81xx_refresh(jh_uc81xx_t *dev,
                               jh_uc81xx_refresh_mode_t mode) {
  if (dev == NULL || !dev->initialized || dev->suspended) {
    return dev == NULL || !dev->initialized ? HAL_EUNINIT : HAL_ESTATE;
  }
  if (mode > JH_UC81XX_REFRESH_PARTIAL) {
    return HAL_EINVAL;
  }
  if (mode == JH_UC81XX_REFRESH_PARTIAL &&
      dev->config.partial_profile == NULL) {
    return HAL_EUNSUPPORTED;
  }
  if (dev->refresh_pending && mode == JH_UC81XX_REFRESH_PARTIAL &&
      dev->pending_refresh_mode == JH_UC81XX_REFRESH_FULL) {
    return HAL_ESTATE;
  }
  hal_status_t status = set_profile(dev, mode);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = command(dev, UC81XX_CMD_PON, NULL, 0u);
  if (hal_status_is_error(status)) {
    return status;
  }
  hal_delay_ms(100u);
  status = command(dev, UC81XX_CMD_DRF, NULL, 0u);
  if (hal_status_is_error(status)) {
    return status;
  }
  hal_delay_ms(1u);
  status = command(dev, UC81XX_CMD_POF, NULL, 0u);
  if (hal_status_is_ok(status)) {
    dev->refresh_pending = false;
  }
  return status;
}

hal_status_t jh_uc81xx_init(jh_uc81xx_t *dev,
                            const jh_uc81xx_config_t *config) {
  const uc81xx_quirks_t *quirks =
      config != NULL ? quirks_for(config->controller) : NULL;
  if (dev == NULL || config == NULL || quirks == NULL || config->width == 0u ||
      config->height == 0u || (config->width & 7u) != 0u ||
      config->width > quirks->max_width ||
      config->height > quirks->max_height) {
    return HAL_EINVAL;
  }
  memset(dev, 0, sizeof(*dev));
  dev->config = *config;
  dev->profile = JH_UC81XX_REFRESH_PARTIAL;
  hal_status_t status = jh_epd_spi_init(&dev->transport, &config->transport);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = jh_epd_spi_reset(&dev->transport, 10u);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = set_profile(dev, JH_UC81XX_REFRESH_FULL);
  if (hal_status_is_error(status)) {
    return status;
  }
  const size_t frame_bytes = (size_t)config->width * config->height / 8u;
  status = jh_epd_spi_command_pattern(&dev->transport, UC81XX_CMD_DTM1, 0xFFu,
                                      frame_bytes);
  if (hal_status_is_ok(status)) {
    status = jh_epd_spi_command_pattern(&dev->transport, UC81XX_CMD_DTM2, 0xFFu,
                                        frame_bytes);
  }
  if (hal_status_is_ok(status)) {
    dev->initialized = true;
    dev->refresh_pending = true;
    dev->pending_refresh_mode = JH_UC81XX_REFRESH_FULL;
  }
  return status;
}

hal_status_t jh_uc81xx_write(jh_uc81xx_t *dev, uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             const uint8_t *buffer, size_t buf_size,
                             bool refresh) {
  if (dev == NULL || !dev->initialized || dev->suspended) {
    return dev == NULL || !dev->initialized ? HAL_EUNINIT : HAL_ESTATE;
  }
  const size_t required = (size_t)width * height / 8u;
  if (buffer == NULL || width == 0u || height == 0u || (width & 7u) != 0u ||
      (x & 7u) != 0u || (uint32_t)x + width > dev->config.width ||
      (uint32_t)y + height > dev->config.height || buf_size < required) {
    return HAL_EINVAL;
  }
  const jh_uc81xx_refresh_mode_t mode =
      refresh && dev->config.partial_profile != NULL ? JH_UC81XX_REFRESH_PARTIAL
                                                     : JH_UC81XX_REFRESH_FULL;
  const uc81xx_quirks_t *quirks = quirks_for(dev->config.controller);
  if (quirks == NULL) {
    return HAL_ESTATE;
  }
  hal_status_t status = set_profile(dev, mode);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = command(dev, UC81XX_CMD_PTIN, NULL, 0u);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = set_partial_window(dev, x, y, width, height);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = command(dev, UC81XX_CMD_DTM2, buffer, required);
  if (hal_status_is_error(status)) {
    return status;
  }
  dev->refresh_pending = true;
  dev->pending_refresh_mode = mode;
  if (refresh) {
    status = set_border(dev, false);
    if (hal_status_is_ok(status)) {
      status = jh_uc81xx_refresh(dev, mode);
    }
    if (hal_status_is_ok(status)) {
      status = set_border(dev, true);
    }
  }
  if (hal_status_is_ok(status) && !quirks->auto_copy) {
    status = set_partial_window(dev, x, y, width, height);
    if (hal_status_is_ok(status)) {
      status = command(dev, refresh ? UC81XX_CMD_DTM2 : UC81XX_CMD_DTM1, buffer,
                       required);
    }
  }
  if (hal_status_is_ok(status)) {
    status = command(dev, UC81XX_CMD_PTOUT, NULL, 0u);
  }
  return status;
}

hal_status_t jh_uc81xx_suspend(jh_uc81xx_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return HAL_EUNINIT;
  }
  const uint8_t deep_sleep = 0xA5u;
  hal_status_t status = command(dev, UC81XX_CMD_DSLP, &deep_sleep, 1u);
  if (hal_status_is_ok(status)) {
    dev->suspended = true;
  }
  return status;
}

hal_status_t jh_uc81xx_resume(jh_uc81xx_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return HAL_EUNINIT;
  }
  if (!dev->suspended) {
    return HAL_OK;
  }
  const jh_uc81xx_config_t config = dev->config;
  return jh_uc81xx_init(dev, &config);
}

#endif
