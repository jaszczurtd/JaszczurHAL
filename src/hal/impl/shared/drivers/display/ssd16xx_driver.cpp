#include "ssd16xx_driver.h"

/* SPDX-License-Identifier: Apache-2.0 */

#include "hal/hal_config.h"

#ifdef HAL_ENABLE_SSD16XX

#include <string.h>

#define SSD16XX_CMD_GDO_CTRL 0x01u
#define SSD16XX_CMD_GDV_CTRL 0x03u
#define SSD16XX_CMD_SDV_CTRL 0x04u
#define SSD16XX_CMD_SOFTSTART 0x0Cu
#define SSD16XX_CMD_SLEEP_MODE 0x10u
#define SSD16XX_CMD_ENTRY_MODE 0x11u
#define SSD16XX_CMD_SW_RESET 0x12u
#define SSD16XX_CMD_TSENSOR_SELECTION 0x18u
#define SSD16XX_CMD_TSENS_CTRL 0x1Au
#define SSD16XX_CMD_MASTER_ACTIVATION 0x20u
#define SSD16XX_CMD_UPDATE_CTRL2 0x22u
#define SSD16XX_CMD_WRITE_RAM 0x24u
#define SSD16XX_CMD_WRITE_RED_RAM 0x26u
#define SSD16XX_CMD_VCOM_VOLTAGE 0x2Cu
#define SSD16XX_CMD_UPDATE_LUT 0x32u
#define SSD16XX_CMD_DUMMY_LINE 0x3Au
#define SSD16XX_CMD_GATE_LINE_WIDTH 0x3Bu
#define SSD16XX_CMD_BWF_CTRL 0x3Cu
#define SSD16XX_CMD_RAM_XPOS_CTRL 0x44u
#define SSD16XX_CMD_RAM_YPOS_CTRL 0x45u
#define SSD16XX_CMD_RAM_XPOS_CNTR 0x4Eu
#define SSD16XX_CMD_RAM_YPOS_CNTR 0x4Fu

#define SSD16XX_CTRL2_ENABLE_CLK 0x80u
#define SSD16XX_CTRL2_ENABLE_ANALOG 0x40u
#define SSD16XX_CTRL2_LOAD_TEMPERATURE 0x20u
#define SSD16XX_CTRL2_LOAD_LUT 0x10u
#define SSD16XX_CTRL2_DISABLE_ANALOG 0x02u
#define SSD16XX_CTRL2_DISABLE_CLK 0x01u
#define SSD16XX_GEN1_TO_PATTERN 0x04u
#define SSD16XX_GEN2_MODE2 0x08u
#define SSD16XX_GEN2_DISPLAY 0x04u

#define SSD16XX_ENTRY_XDYDX 0x00u
#define SSD16XX_ENTRY_XIYDX 0x01u
#define SSD16XX_ENTRY_XIYDY 0x05u
#define SSD16XX_ENTRY_XDYIY 0x06u
#define SSD16XX_ENTRY_XIYIX 0x03u

typedef struct {
  uint16_t max_width;
  uint16_t max_height;
  uint8_t x_bits;
  uint8_t y_bits;
  uint8_t ctrl2_full;
  uint8_t ctrl2_partial;
} ssd16xx_quirks_t;

static const ssd16xx_quirks_t kQuirks[] = {
    {320u, 240u, 16u, 16u, SSD16XX_GEN1_TO_PATTERN, SSD16XX_GEN1_TO_PATTERN},
    {250u, 150u, 8u, 8u, SSD16XX_GEN1_TO_PATTERN, SSD16XX_GEN1_TO_PATTERN},
    {296u, 160u, 8u, 16u, SSD16XX_GEN1_TO_PATTERN, SSD16XX_GEN1_TO_PATTERN},
    {296u, 176u, 8u, 16u, SSD16XX_GEN2_DISPLAY,
     SSD16XX_GEN2_DISPLAY | SSD16XX_GEN2_MODE2},
    {200u, 200u, 8u, 16u, SSD16XX_GEN2_DISPLAY,
     SSD16XX_GEN2_DISPLAY | SSD16XX_GEN2_MODE2},
};

static const ssd16xx_quirks_t *quirks_for(jh_ssd16xx_controller_t controller) {
  return controller <= JH_SSD16XX_SSD1681 ? &kQuirks[(size_t)controller] : NULL;
}

static hal_status_t command(jh_ssd16xx_t *dev, uint8_t cmd, const uint8_t *data,
                            size_t len) {
  return jh_epd_spi_command(&dev->transport, cmd, data, len);
}

static hal_status_t command_u8(jh_ssd16xx_t *dev, uint8_t cmd, uint8_t value) {
  return command(dev, cmd, &value, 1u);
}

static size_t push_coord(uint8_t *out, uint16_t value, uint8_t bits) {
  out[0] = (uint8_t)(value & 0xFFu);
  if (bits == 16u) {
    out[1] = (uint8_t)(value >> 8u);
    return 2u;
  }
  return 1u;
}

static hal_status_t set_ram_area(jh_ssd16xx_t *dev, uint16_t x_start,
                                 uint16_t x_end, uint16_t y_start,
                                 uint16_t y_end) {
  const ssd16xx_quirks_t *quirks = quirks_for(dev->config.controller);
  uint8_t data[4];
  size_t len = push_coord(data, x_start, quirks->x_bits);
  len += push_coord(data + len, x_end, quirks->x_bits);
  hal_status_t status = command(dev, SSD16XX_CMD_RAM_XPOS_CTRL, data, len);
  if (hal_status_is_error(status)) {
    return status;
  }
  len = push_coord(data, y_start, quirks->y_bits);
  len += push_coord(data + len, y_end, quirks->y_bits);
  return command(dev, SSD16XX_CMD_RAM_YPOS_CTRL, data, len);
}

static hal_status_t set_ram_pointer(jh_ssd16xx_t *dev, uint16_t x, uint16_t y) {
  const ssd16xx_quirks_t *quirks = quirks_for(dev->config.controller);
  uint8_t data[2];
  size_t len = push_coord(data, x, quirks->x_bits);
  hal_status_t status = command(dev, SSD16XX_CMD_RAM_XPOS_CNTR, data, len);
  if (hal_status_is_error(status)) {
    return status;
  }
  len = push_coord(data, y, quirks->y_bits);
  return command(dev, SSD16XX_CMD_RAM_YPOS_CNTR, data, len);
}

static hal_status_t activate(jh_ssd16xx_t *dev, uint8_t ctrl2) {
  hal_status_t status = command_u8(dev, SSD16XX_CMD_UPDATE_CTRL2, ctrl2);
  return hal_status_is_error(status)
             ? status
             : command(dev, SSD16XX_CMD_MASTER_ACTIVATION, NULL, 0u);
}

static const jh_ssd16xx_profile_t *profile_for(const jh_ssd16xx_t *dev,
                                               jh_ssd16xx_refresh_mode_t mode) {
  return mode == JH_SSD16XX_REFRESH_PARTIAL ? dev->config.partial_profile
                                            : dev->config.full_profile;
}

static hal_status_t load_default_waveform(jh_ssd16xx_t *dev) {
  if (dev->config.temperature_sensor_selection != 0u) {
    return command_u8(dev, SSD16XX_CMD_TSENSOR_SELECTION,
                      dev->config.temperature_sensor_selection);
  }
  hal_status_t status = activate(dev, SSD16XX_CTRL2_ENABLE_CLK);
  if (hal_status_is_error(status)) {
    return status;
  }
  const uint8_t temperature_25c[] = {0x19u, 0x00u};
  status = command(dev, SSD16XX_CMD_TSENS_CTRL, temperature_25c,
                   sizeof(temperature_25c));
  return hal_status_is_error(status) ? status
                                     : activate(dev, SSD16XX_CTRL2_DISABLE_CLK);
}

static hal_status_t set_profile(jh_ssd16xx_t *dev,
                                jh_ssd16xx_refresh_mode_t mode) {
  const jh_ssd16xx_profile_t *profile = profile_for(dev, mode);
  if (mode == JH_SSD16XX_REFRESH_PARTIAL && profile == NULL) {
    return HAL_EUNSUPPORTED;
  }
  if (dev->profile == mode) {
    return HAL_OK;
  }
  hal_status_t status = command(dev, SSD16XX_CMD_SW_RESET, NULL, 0u);
  if (hal_status_is_error(status)) {
    return status;
  }
  const ssd16xx_quirks_t *quirks = quirks_for(dev->config.controller);
  if (quirks == NULL) {
    return HAL_ESTATE;
  }
  uint8_t gate_data[3];
  size_t gate_len =
      push_coord(gate_data, (uint16_t)(dev->config.width - 1u), quirks->y_bits);
  gate_data[gate_len++] = 0u;
  status = command(dev, SSD16XX_CMD_GDO_CTRL, gate_data, gate_len);
  if (hal_status_is_error(status)) {
    return status;
  }
  if (dev->config.softstart.len > 0u) {
    status = command(dev, SSD16XX_CMD_SOFTSTART, dev->config.softstart.data,
                     dev->config.softstart.len);
    if (hal_status_is_error(status)) {
      return status;
    }
  }
  status = profile != NULL && profile->lut.len > 0u
               ? command(dev, SSD16XX_CMD_UPDATE_LUT, profile->lut.data,
                         profile->lut.len)
               : load_default_waveform(dev);
  if (hal_status_is_error(status)) {
    return status;
  }
  if (profile != NULL) {
    if (profile->override_dummy_line) {
      status = command_u8(dev, SSD16XX_CMD_DUMMY_LINE, profile->dummy_line);
      if (hal_status_is_error(status)) {
        return status;
      }
    }
    if (profile->override_gate_line_width) {
      status = command_u8(dev, SSD16XX_CMD_GATE_LINE_WIDTH,
                          profile->gate_line_width);
      if (hal_status_is_error(status)) {
        return status;
      }
    }
    if (profile->gate_voltage.len > 0u) {
      status = command(dev, SSD16XX_CMD_GDV_CTRL, profile->gate_voltage.data,
                       profile->gate_voltage.len);
      if (hal_status_is_error(status)) {
        return status;
      }
    }
    if (profile->source_voltage.len > 0u) {
      status = command(dev, SSD16XX_CMD_SDV_CTRL, profile->source_voltage.data,
                       profile->source_voltage.len);
      if (hal_status_is_error(status)) {
        return status;
      }
    }
    if (profile->override_vcom) {
      status = command_u8(dev, SSD16XX_CMD_VCOM_VOLTAGE, profile->vcom);
      if (hal_status_is_error(status)) {
        return status;
      }
    }
    if (profile->override_border_waveform) {
      status = command_u8(dev, SSD16XX_CMD_BWF_CTRL, profile->border_waveform);
      if (hal_status_is_error(status)) {
        return status;
      }
    }
  }
  status = command_u8(dev, SSD16XX_CMD_ENTRY_MODE, dev->scan_mode);
  if (hal_status_is_ok(status)) {
    dev->profile = mode;
  }
  return status;
}

static hal_status_t set_rotation(jh_ssd16xx_t *dev, uint8_t rotation) {
  static const uint8_t scan_modes[] = {SSD16XX_ENTRY_XDYIY, SSD16XX_ENTRY_XDYDX,
                                       SSD16XX_ENTRY_XIYDY,
                                       SSD16XX_ENTRY_XIYIX};
  if (rotation > 3u) {
    return HAL_EINVAL;
  }
  dev->scan_mode = scan_modes[rotation];
  return command_u8(dev, SSD16XX_CMD_ENTRY_MODE, dev->scan_mode);
}

hal_status_t jh_ssd16xx_set_rotation(jh_ssd16xx_t *dev, uint8_t rotation) {
  if (dev == NULL || !dev->initialized || dev->suspended) {
    return dev == NULL || !dev->initialized ? HAL_EUNINIT : HAL_ESTATE;
  }
  hal_status_t status = set_rotation(dev, rotation);
  if (hal_status_is_ok(status)) {
    dev->config.rotation = rotation;
  }
  return status;
}

static hal_status_t clear_ram(jh_ssd16xx_t *dev, uint8_t ram_command,
                              uint8_t pattern) {
  const uint16_t pages = (uint16_t)((dev->config.height + 7u) / 8u);
  hal_status_t status =
      command_u8(dev, SSD16XX_CMD_ENTRY_MODE, SSD16XX_ENTRY_XIYDY);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = set_ram_area(dev, 0u, (uint16_t)(pages - 1u),
                        (uint16_t)(dev->config.width - 1u), 0u);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = set_ram_pointer(dev, 0u, (uint16_t)(dev->config.width - 1u));
  return hal_status_is_error(status)
             ? status
             : jh_epd_spi_command_pattern(&dev->transport, ram_command, pattern,
                                          (size_t)pages * dev->config.width);
}

static hal_status_t set_window(jh_ssd16xx_t *dev, uint16_t x, uint16_t y,
                               uint16_t width, uint16_t height) {
  const uint16_t panel_height = (uint16_t)(dev->config.height & ~7u);
  uint16_t x_start;
  uint16_t x_end;
  uint16_t y_start;
  uint16_t y_end;
  if (dev->config.rotation == 0u || dev->config.rotation == 2u) {
    if ((uint32_t)x + width > dev->config.width ||
        (uint32_t)y + height > panel_height || (y & 7u) != 0u ||
        (height & 7u) != 0u) {
      return HAL_EINVAL;
    }
  } else if ((uint32_t)y + height > dev->config.width ||
             (uint32_t)x + width > panel_height || (x & 7u) != 0u ||
             (width & 7u) != 0u) {
    return HAL_EINVAL;
  }
  switch (dev->config.rotation) {
  case 0u:
    x_start = (uint16_t)((panel_height - 1u - y) / 8u);
    x_end = (uint16_t)((panel_height - y - height) / 8u);
    y_start = x;
    y_end = (uint16_t)(x + width - 1u);
    break;
  case 1u:
    x_start = (uint16_t)((panel_height - 1u - x) / 8u);
    x_end = (uint16_t)((panel_height - x - width) / 8u);
    y_start = (uint16_t)(dev->config.width - 1u - y);
    y_end = (uint16_t)(dev->config.width - y - height);
    break;
  case 2u:
    x_start = (uint16_t)(y / 8u);
    x_end = (uint16_t)((y + height - 1u) / 8u);
    y_start = (uint16_t)(x + width - 1u);
    y_end = x;
    break;
  default:
    x_start = (uint16_t)(x / 8u);
    x_end = (uint16_t)((x + width - 1u) / 8u);
    y_start = y;
    y_end = (uint16_t)(y + height - 1u);
    break;
  }
  hal_status_t status = set_ram_area(dev, x_start, x_end, y_start, y_end);
  return hal_status_is_error(status) ? status
                                     : set_ram_pointer(dev, x_start, y_start);
}

hal_status_t jh_ssd16xx_refresh(jh_ssd16xx_t *dev,
                                jh_ssd16xx_refresh_mode_t mode) {
  if (dev == NULL || !dev->initialized || dev->suspended) {
    return dev == NULL || !dev->initialized ? HAL_EUNINIT : HAL_ESTATE;
  }
  if (mode > JH_SSD16XX_REFRESH_PARTIAL) {
    return HAL_EINVAL;
  }
  if (mode == JH_SSD16XX_REFRESH_PARTIAL &&
      dev->config.partial_profile == NULL) {
    return HAL_EUNSUPPORTED;
  }
  if (dev->refresh_pending && mode == JH_SSD16XX_REFRESH_PARTIAL &&
      dev->pending_refresh_mode == JH_SSD16XX_REFRESH_FULL) {
    return HAL_ESTATE;
  }
  hal_status_t status = set_profile(dev, mode);
  if (hal_status_is_error(status)) {
    return status;
  }
  const ssd16xx_quirks_t *quirks = quirks_for(dev->config.controller);
  if (quirks == NULL) {
    return HAL_ESTATE;
  }
  const jh_ssd16xx_profile_t *profile = profile_for(dev, mode);
  const bool load_lut = profile == NULL || profile->lut.len == 0u;
  const uint8_t ctrl2 =
      SSD16XX_CTRL2_ENABLE_CLK | SSD16XX_CTRL2_ENABLE_ANALOG |
      (load_lut ? SSD16XX_CTRL2_LOAD_LUT : 0u) |
      (load_lut && dev->config.temperature_sensor_selection != 0u
           ? SSD16XX_CTRL2_LOAD_TEMPERATURE
           : 0u) |
      (mode == JH_SSD16XX_REFRESH_PARTIAL ? quirks->ctrl2_partial
                                          : quirks->ctrl2_full) |
      SSD16XX_CTRL2_DISABLE_ANALOG | SSD16XX_CTRL2_DISABLE_CLK;
  status = activate(dev, ctrl2);
  if (hal_status_is_ok(status)) {
    dev->refresh_pending = false;
  }
  return status;
}

hal_status_t jh_ssd16xx_init(jh_ssd16xx_t *dev,
                             const jh_ssd16xx_config_t *config) {
  const ssd16xx_quirks_t *quirks =
      config != NULL ? quirks_for(config->controller) : NULL;
  if (dev == NULL || config == NULL || quirks == NULL || config->width == 0u ||
      config->height < 8u || config->width > quirks->max_width ||
      config->height > quirks->max_height || config->rotation > 3u) {
    return HAL_EINVAL;
  }
  memset(dev, 0, sizeof(*dev));
  dev->config = *config;
  dev->profile = JH_SSD16XX_REFRESH_PARTIAL;
  hal_status_t status = jh_epd_spi_init(&dev->transport, &config->transport);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = jh_epd_spi_reset(&dev->transport, 1u);
  if (hal_status_is_error(status)) {
    return status;
  }
  dev->scan_mode = SSD16XX_ENTRY_XDYIY;
  status = set_profile(dev, JH_SSD16XX_REFRESH_FULL);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = clear_ram(dev, SSD16XX_CMD_WRITE_RAM, 0xFFu);
  if (hal_status_is_error(status)) {
    return status;
  }
  const uint8_t red_pattern = config->partial_profile == NULL ? 0x00u : 0xFFu;
  status = clear_ram(dev, SSD16XX_CMD_WRITE_RED_RAM, red_pattern);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = set_rotation(dev, config->rotation);
  if (hal_status_is_error(status)) {
    return status;
  }
  dev->initialized = true;
  dev->refresh_pending = true;
  dev->pending_refresh_mode = JH_SSD16XX_REFRESH_FULL;
  status = jh_ssd16xx_refresh(dev, JH_SSD16XX_REFRESH_FULL);
  if (hal_status_is_error(status)) {
    dev->initialized = false;
  }
  return status;
}

hal_status_t jh_ssd16xx_write(jh_ssd16xx_t *dev, uint16_t x, uint16_t y,
                              uint16_t width, uint16_t height,
                              const uint8_t *buffer, size_t buf_size,
                              bool refresh) {
  if (dev == NULL || !dev->initialized || dev->suspended) {
    return dev == NULL || !dev->initialized ? HAL_EUNINIT : HAL_ESTATE;
  }
  const size_t required = ((size_t)width * height) / 8u;
  if (buffer == NULL || width == 0u || height == 0u || required == 0u ||
      buf_size < required) {
    return HAL_EINVAL;
  }
  const jh_ssd16xx_refresh_mode_t mode =
      refresh && dev->config.partial_profile != NULL
          ? JH_SSD16XX_REFRESH_PARTIAL
          : JH_SSD16XX_REFRESH_FULL;
  hal_status_t status = set_profile(dev, mode);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = set_window(dev, x, y, width, height);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = command(dev, SSD16XX_CMD_WRITE_RAM, buffer, required);
  if (hal_status_is_error(status)) {
    return status;
  }
  dev->refresh_pending = true;
  dev->pending_refresh_mode = mode;
  if (!refresh) {
    if (dev->config.partial_profile == NULL) {
      return HAL_OK;
    }
    status = set_window(dev, x, y, width, height);
    return hal_status_is_error(status)
               ? status
               : command(dev, SSD16XX_CMD_WRITE_RED_RAM, buffer, required);
  }
  status = jh_ssd16xx_refresh(dev, mode);
  if (hal_status_is_error(status)) {
    return status;
  }
  if (mode == JH_SSD16XX_REFRESH_PARTIAL) {
    status = set_window(dev, x, y, width, height);
    if (hal_status_is_ok(status)) {
      status = command(dev, SSD16XX_CMD_WRITE_RAM, buffer, required);
    }
  }
  return status;
}

hal_status_t jh_ssd16xx_suspend(jh_ssd16xx_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return HAL_EUNINIT;
  }
  const uint8_t deep_sleep = 0x01u;
  hal_status_t status = command(dev, SSD16XX_CMD_SLEEP_MODE, &deep_sleep, 1u);
  if (hal_status_is_ok(status)) {
    dev->suspended = true;
  }
  return status;
}

hal_status_t jh_ssd16xx_resume(jh_ssd16xx_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return HAL_EUNINIT;
  }
  if (!dev->suspended) {
    return HAL_OK;
  }
  const jh_ssd16xx_config_t config = dev->config;
  return jh_ssd16xx_init(dev, &config);
}

#endif
