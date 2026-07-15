#include "rgb_oled_driver.h"

#include "hal/hal_config.h"

#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)

#include "hal/hal_gpio.h"
#include "hal/hal_spi.h"
#include "hal/hal_system.h"

#include <string.h>

#define SSD1331_DISPLAY_OFF 0xAEu
#define SSD1331_DISPLAY_ON 0xAFu
#define SSD1331_SET_NORMAL_DISPLAY 0xA4u
#define SSD1331_SET_REVERSE_DISPLAY 0xA7u
#define SSD1331_SET_COLUMN_ADDR 0x15u
#define SSD1331_SET_ROW_ADDR 0x75u
#define SSD1331_SET_DISPLAY_START_LINE 0xA1u
#define SSD1331_SET_DISPLAY_OFFSET 0xA2u
#define SSD1331_SET_MULTIPLEX_RATIO 0xA8u
#define SSD1331_SET_PHASE_LENGTH 0xB1u
#define SSD1331_SET_OSC_FREQ 0xB3u
#define SSD1331_SET_PRECHARGE_A 0x8Au
#define SSD1331_SET_PRECHARGE_B 0x8Bu
#define SSD1331_SET_PRECHARGE_C 0x8Cu
#define SSD1331_SET_PRECHARGE_V 0xBBu
#define SSD1331_SET_VCOMH 0xBEu
#define SSD1331_SET_CURRENT_ATT 0x87u
#define SSD1331_SET_REMAP 0xA0u
#define SSD1331_DISABLE_SCROLL 0x2Eu
#define SSD1331_SET_EXTERNAL_SUPPLY 0xADu
#define SSD1331_EXTERNAL_SUPPLY 0x8Eu
#define SSD1331_SET_POWER_SAVE 0xB0u
#define SSD1331_POWER_SAVE 0x1Au
#define SSD1331_NOT_POWER_SAVE 0x0Bu
#define SSD1331_CONTRASTA 0x81u
#define SSD1331_CONTRASTB 0x82u
#define SSD1331_CONTRASTC 0x83u

#define SSD135X_DISPLAY_OFF 0xAEu
#define SSD135X_DISPLAY_ON 0xAFu
#define SSD135X_SET_NORMAL_DISPLAY 0xA6u
#define SSD135X_SET_REVERSE_DISPLAY 0xA7u
#define SSD135X_SET_COLUMN_ADDR 0x15u
#define SSD135X_SET_ROW_ADDR 0x75u
#define SSD135X_SET_DISPLAY_START_LINE 0xA1u
#define SSD135X_SET_DISPLAY_OFFSET 0xA2u
#define SSD135X_SET_MULTIPLEX_RATIO 0xCAu
#define SSD135X_SET_PHASE_LENGTH 0xB1u
#define SSD135X_SET_OSC_FREQ 0xB3u
#define SSD135X_SET_PRECHARGE_V 0xBBu
#define SSD135X_SET_VCOMH 0xBEu
#define SSD135X_SET_CURRENT_ATT 0xC7u
#define SSD135X_SET_PRECHARGE_P 0xB6u
#define SSD135X_SET_REMAP 0xA0u
#define SSD135X_STOP_SCROLL 0x9Eu
#define SSD135X_CONTRAST 0xC1u
#define SSD135X_SET_LOCK 0xFDu
#define SSD135X_UNLOCK_1 0x12u
#define SSD135X_UNLOCK_2 0xB1u
#define SSD135X_WRITE 0x5Cu

#define RGB_OLED_PIXEL_CHUNK_BYTES 512u

static bool pin_is_connected(int16_t pin) { return pin >= 0 && pin <= 255; }
static uint8_t pin_to_u8(int16_t pin) { return (uint8_t)pin; }

static uint32_t normalized_clock(const jh_rgb_oled_config_t *config) {
  return (config != NULL && config->clock_hz != 0u)
             ? config->clock_hz
             : JH_RGB_OLED_SPI_DEFAULT_HZ;
}

static uint8_t normalized_spi_mode(const jh_rgb_oled_config_t *config) {
  return (config != NULL && config->spi_mode <= HAL_SPI_MODE3)
             ? config->spi_mode
             : HAL_SPI_MODE0;
}

static hal_spi_settings_t spi_settings_for(const jh_rgb_oled_t *dev) {
  hal_spi_settings_t settings = {normalized_clock(&dev->config),
                                 HAL_SPI_MSBFIRST,
                                 normalized_spi_mode(&dev->config)};
  return settings;
}

static bool command_write(jh_rgb_oled_t *dev, uint8_t command,
                          const uint8_t *data, uint8_t len) {
  if (dev == NULL || !pin_is_connected(dev->config.dc_pin)) {
    return false;
  }
  const uint8_t bus = dev->config.bus;
  const hal_spi_settings_t settings = spi_settings_for(dev);
  hal_status_t end_status = HAL_OK;

  hal_spi_lock(bus);
  if (hal_status_is_error(hal_spi_begin_transaction(bus, &settings))) {
    hal_spi_unlock(bus);
    return false;
  }
  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), false);
  }

  hal_gpio_write(pin_to_u8(dev->config.dc_pin), false);
  if (hal_status_is_error(hal_spi_write(bus, &command, 1u))) {
    goto fail;
  }
  if (data != NULL && len > 0u) {
    if (dev->config.controller == JH_RGB_OLED_SSD1331) {
      for (uint8_t i = 0u; i < len; ++i) {
        if (hal_status_is_error(hal_spi_write(bus, &data[i], 1u))) {
          goto fail;
        }
      }
    } else {
      hal_gpio_write(pin_to_u8(dev->config.dc_pin), true);
      if (hal_status_is_error(hal_spi_write(bus, data, len))) {
        goto fail;
      }
    }
  }

  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), true);
  }
  end_status = hal_spi_end_transaction(bus);
  hal_spi_unlock(bus);
  return hal_status_is_ok(end_status);

fail:
  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), true);
  }
  (void)hal_spi_end_transaction(bus);
  hal_spi_unlock(bus);
  return false;
}

static bool setup_pins_and_reset(jh_rgb_oled_t *dev) {
  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_set_mode(pin_to_u8(dev->config.cs_pin), HAL_GPIO_OUTPUT);
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), true);
  }
  hal_gpio_set_mode(pin_to_u8(dev->config.dc_pin), HAL_GPIO_OUTPUT);
  hal_gpio_write(pin_to_u8(dev->config.dc_pin), true);
  if (pin_is_connected(dev->config.rst_pin)) {
    const uint8_t rst = pin_to_u8(dev->config.rst_pin);
    hal_gpio_set_mode(rst, HAL_GPIO_OUTPUT);
    hal_gpio_write(rst, true);
    hal_delay_ms(10u);
    hal_gpio_write(rst, false);
    hal_delay_ms(10u);
    hal_gpio_write(rst, true);
    hal_delay_ms(10u);
  }
  return true;
}

static uint8_t default_u8(uint8_t value, uint8_t fallback) {
  return value == 0u ? fallback : value;
}

static uint8_t scaled(uint8_t contrast, uint8_t channel) {
  return (uint8_t)(((uint16_t)contrast * (uint16_t)channel) / 0xFFu);
}

static bool init_ssd1331(jh_rgb_oled_t *dev) {
  uint8_t tmp = 0u;
  uint8_t remap = default_u8(dev->config.remap_value, 0x72u);

  if (!command_write(dev, SSD1331_DISPLAY_OFF, NULL, 0u) ||
      !command_write(dev, SSD1331_SET_REMAP, &remap, 1u)) {
    return false;
  }
  tmp = dev->config.start_line;
  if (!command_write(dev, SSD1331_SET_DISPLAY_START_LINE, &tmp, 1u)) {
    return false;
  }
  tmp = dev->config.display_offset;
  if (!command_write(dev, SSD1331_SET_DISPLAY_OFFSET, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.multiplex_ratio, (uint8_t)(dev->height - 1u));
  if (!command_write(dev, SSD1331_SET_MULTIPLEX_RATIO, &tmp, 1u)) {
    return false;
  }
  tmp = SSD1331_EXTERNAL_SUPPLY;
  if (!command_write(dev, SSD1331_SET_EXTERNAL_SUPPLY, &tmp, 1u)) {
    return false;
  }
  tmp = dev->config.power_save ? SSD1331_POWER_SAVE : SSD1331_NOT_POWER_SAVE;
  if (!command_write(dev, SSD1331_SET_POWER_SAVE, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.phase_length, 0x74u);
  if (!command_write(dev, SSD1331_SET_PHASE_LENGTH, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.oscillator_freq, 0xD0u);
  if (!command_write(dev, SSD1331_SET_OSC_FREQ, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.precharge_time_a, 0x80u);
  if (!command_write(dev, SSD1331_SET_PRECHARGE_A, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.precharge_time_b, 0x80u);
  if (!command_write(dev, SSD1331_SET_PRECHARGE_B, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.precharge_time_c, 0x80u);
  if (!command_write(dev, SSD1331_SET_PRECHARGE_C, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.precharge_voltage, 0x3Au);
  if (!command_write(dev, SSD1331_SET_PRECHARGE_V, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.vcomh_voltage, 0x3Eu);
  if (!command_write(dev, SSD1331_SET_VCOMH, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.current_att, 0x06u);
  if (!command_write(dev, SSD1331_SET_CURRENT_ATT, &tmp, 1u) ||
      !command_write(dev, SSD1331_DISABLE_SCROLL, NULL, 0u) ||
      !jh_rgb_oled_set_contrast(dev, 0xCFu) ||
      !jh_rgb_oled_invert(dev, dev->config.inverted)) {
    return false;
  }
  return command_write(dev, SSD1331_DISPLAY_ON, NULL, 0u);
}

static bool init_ssd135x(jh_rgb_oled_t *dev) {
  uint8_t tmp = SSD135X_UNLOCK_1;
  if (!command_write(dev, SSD135X_DISPLAY_OFF, NULL, 0u) ||
      !command_write(dev, SSD135X_SET_LOCK, &tmp, 1u)) {
    return false;
  }
  if (dev->config.controller != JH_RGB_OLED_SSD1357) {
    tmp = SSD135X_UNLOCK_2;
    if (!command_write(dev, SSD135X_SET_LOCK, &tmp, 1u)) {
      return false;
    }
  }
  tmp = default_u8(dev->config.oscillator_freq, 0xF1u);
  if (!command_write(dev, SSD135X_SET_OSC_FREQ, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.multiplex_ratio, (uint8_t)(dev->height - 1u));
  if (!command_write(dev, SSD135X_SET_MULTIPLEX_RATIO, &tmp, 1u)) {
    return false;
  }
  tmp = dev->config.display_offset;
  if (!command_write(dev, SSD135X_SET_DISPLAY_OFFSET, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.remap_value, 0x74u);
  if (!command_write(dev, SSD135X_SET_REMAP, &tmp, 1u)) {
    return false;
  }
  tmp = dev->config.start_line;
  if (!command_write(dev, SSD135X_SET_DISPLAY_START_LINE, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.phase_length, 0x32u);
  if (!command_write(dev, SSD135X_SET_PHASE_LENGTH, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.vcomh_voltage, 0x05u);
  if (!command_write(dev, SSD135X_SET_VCOMH, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.current_att, 0x0Fu);
  if (!command_write(dev, SSD135X_SET_CURRENT_ATT, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.precharge_voltage, 0x17u);
  if (!command_write(dev, SSD135X_SET_PRECHARGE_V, &tmp, 1u)) {
    return false;
  }
  tmp = default_u8(dev->config.precharge_time, 0x08u);
  if (!command_write(dev, SSD135X_SET_PRECHARGE_P, &tmp, 1u) ||
      !command_write(dev, SSD135X_STOP_SCROLL, NULL, 0u) ||
      !jh_rgb_oled_set_contrast(dev, 0xCFu) ||
      !jh_rgb_oled_invert(dev, dev->config.inverted)) {
    return false;
  }
  return command_write(dev, SSD135X_DISPLAY_ON, NULL, 0u);
}

bool jh_rgb_oled_init(jh_rgb_oled_t *dev, const jh_rgb_oled_config_t *config) {
  if (dev == NULL || config == NULL || !pin_is_connected(config->dc_pin)) {
    return false;
  }
  memset(dev, 0, sizeof(*dev));
  dev->config = *config;
  dev->config.clock_hz = normalized_clock(config);
  dev->config.spi_mode = normalized_spi_mode(config);
  dev->width = config->width != 0u ? config->width : 128u;
  dev->height = config->height != 0u ? config->height : 128u;
  if (dev->config.controller == JH_RGB_OLED_SSD1331) {
    dev->width = config->width != 0u ? config->width : 96u;
    dev->height = config->height != 0u ? config->height : 64u;
  }
  if (!setup_pins_and_reset(dev)) {
    return false;
  }
  dev->initialized = true;
  if (dev->config.controller == JH_RGB_OLED_SSD1331) {
    return init_ssd1331(dev);
  }
  return init_ssd135x(dev);
}

bool jh_rgb_oled_set_rotation(jh_rgb_oled_t *dev, uint8_t rotation) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  if ((rotation & 0x03u) != 0u) {
    return false;
  }
  dev->rotation = rotation & 0x03u;
  return true;
}

bool jh_rgb_oled_invert(jh_rgb_oled_t *dev, bool invert) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  dev->config.inverted = invert;
  if (dev->config.controller == JH_RGB_OLED_SSD1331) {
    return command_write(
        dev, invert ? SSD1331_SET_REVERSE_DISPLAY : SSD1331_SET_NORMAL_DISPLAY,
        NULL, 0u);
  }
  return command_write(
      dev, invert ? SSD135X_SET_REVERSE_DISPLAY : SSD135X_SET_NORMAL_DISPLAY,
      NULL, 0u);
}

bool jh_rgb_oled_suspend(jh_rgb_oled_t *dev) {
  if (dev == NULL || !dev->initialized || dev->write_active) {
    return false;
  }
  const uint8_t command = dev->config.controller == JH_RGB_OLED_SSD1331
                              ? SSD1331_DISPLAY_OFF
                              : SSD135X_DISPLAY_OFF;
  return command_write(dev, command, NULL, 0u);
}

bool jh_rgb_oled_resume(jh_rgb_oled_t *dev) {
  if (dev == NULL || !dev->initialized || dev->write_active) {
    return false;
  }
  const uint8_t command = dev->config.controller == JH_RGB_OLED_SSD1331
                              ? SSD1331_DISPLAY_ON
                              : SSD135X_DISPLAY_ON;
  return command_write(dev, command, NULL, 0u);
}

bool jh_rgb_oled_set_contrast(jh_rgb_oled_t *dev, uint8_t contrast) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  const uint8_t a = default_u8(dev->config.contrast_a, 0x91u);
  const uint8_t b = default_u8(dev->config.contrast_b, 0x50u);
  const uint8_t c = default_u8(dev->config.contrast_c, 0x7Du);
  if (dev->config.controller == JH_RGB_OLED_SSD1331) {
    uint8_t tmp = scaled(contrast, a);
    if (!command_write(dev, SSD1331_CONTRASTA, &tmp, 1u)) {
      return false;
    }
    tmp = scaled(contrast, b);
    if (!command_write(dev, SSD1331_CONTRASTB, &tmp, 1u)) {
      return false;
    }
    tmp = scaled(contrast, c);
    return command_write(dev, SSD1331_CONTRASTC, &tmp, 1u);
  }
  const uint8_t data[3] = {scaled(contrast, a), scaled(contrast, b),
                           scaled(contrast, c)};
  return command_write(dev, SSD135X_CONTRAST, data, sizeof(data));
}

bool jh_rgb_oled_set_addr_window(jh_rgb_oled_t *dev, uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h) {
  if (dev == NULL || !dev->initialized || w == 0u || h == 0u) {
    return false;
  }
  const uint8_t column_offset = dev->config.controller == JH_RGB_OLED_SSD1331
                                    ? 0u
                                    : dev->config.column_offset;
  uint8_t data[2] = {(uint8_t)(x + column_offset),
                     (uint8_t)(x + w - 1u + column_offset)};
  const uint8_t col_cmd = dev->config.controller == JH_RGB_OLED_SSD1331
                              ? SSD1331_SET_COLUMN_ADDR
                              : SSD135X_SET_COLUMN_ADDR;
  const uint8_t row_cmd = dev->config.controller == JH_RGB_OLED_SSD1331
                              ? SSD1331_SET_ROW_ADDR
                              : SSD135X_SET_ROW_ADDR;
  if (!command_write(dev, col_cmd, data, sizeof(data))) {
    return false;
  }
  data[0] = (uint8_t)y;
  data[1] = (uint8_t)(y + h - 1u);
  if (!command_write(dev, row_cmd, data, sizeof(data))) {
    return false;
  }
  if (dev->config.controller == JH_RGB_OLED_SSD1331) {
    return true;
  }
  return command_write(dev, SSD135X_WRITE, NULL, 0u);
}

bool jh_rgb_oled_begin_write(jh_rgb_oled_t *dev, uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h) {
  if (dev == NULL || dev->write_active ||
      !jh_rgb_oled_set_addr_window(dev, x, y, w, h)) {
    return false;
  }
  const uint8_t bus = dev->config.bus;
  const hal_spi_settings_t settings = spi_settings_for(dev);
  hal_spi_lock(bus);
  if (hal_status_is_error(hal_spi_begin_transaction(bus, &settings))) {
    hal_spi_unlock(bus);
    return false;
  }
  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), false);
  }
  hal_gpio_write(pin_to_u8(dev->config.dc_pin), true);
  dev->write_active = true;
  return true;
}

bool jh_rgb_oled_write_pixels_be(jh_rgb_oled_t *dev, const uint8_t *pixels_be,
                                 size_t byte_count) {
  if (dev == NULL || !dev->write_active ||
      (pixels_be == NULL && byte_count > 0u) || (byte_count & 1u) != 0u) {
    return false;
  }
  return hal_status_is_ok(
      hal_spi_write(dev->config.bus, pixels_be, byte_count));
}

static void put_u16_be(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
}

bool jh_rgb_oled_write_pixels_fast(jh_rgb_oled_t *dev, const uint16_t *pixels,
                                   size_t count) {
  if (dev == NULL || !dev->write_active || (pixels == NULL && count > 0u)) {
    return false;
  }
  uint8_t chunk[RGB_OLED_PIXEL_CHUNK_BYTES];
  while (count > 0u) {
    const size_t pixel_count =
        count < (sizeof(chunk) / 2u) ? count : (sizeof(chunk) / 2u);
    for (size_t i = 0u; i < pixel_count; ++i) {
      put_u16_be(&chunk[i * 2u], pixels[i]);
    }
    if (!jh_rgb_oled_write_pixels_be(dev, chunk, pixel_count * 2u)) {
      return false;
    }
    pixels += pixel_count;
    count -= pixel_count;
  }
  return true;
}

bool jh_rgb_oled_end_write(jh_rgb_oled_t *dev) {
  if (dev == NULL || !dev->write_active) {
    return false;
  }
  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), true);
  }
  const hal_status_t status = hal_spi_end_transaction(dev->config.bus);
  hal_spi_unlock(dev->config.bus);
  dev->write_active = false;
  return hal_status_is_ok(status);
}

bool jh_rgb_oled_fill_rect(jh_rgb_oled_t *dev, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, uint16_t color) {
  if (dev == NULL || w == 0u || h == 0u ||
      !jh_rgb_oled_begin_write(dev, x, y, w, h)) {
    return false;
  }
  uint8_t chunk[RGB_OLED_PIXEL_CHUNK_BYTES];
  for (size_t i = 0u; i < sizeof(chunk); i += 2u) {
    put_u16_be(&chunk[i], color);
  }
  size_t remaining = (size_t)w * (size_t)h;
  bool ok = true;
  while (remaining > 0u) {
    const size_t pixels =
        remaining < (sizeof(chunk) / 2u) ? remaining : (sizeof(chunk) / 2u);
    ok = jh_rgb_oled_write_pixels_be(dev, chunk, pixels * 2u);
    if (!ok) {
      break;
    }
    remaining -= pixels;
  }
  return jh_rgb_oled_end_write(dev) && ok;
}

bool jh_rgb_oled_draw_rgb_bitmap(jh_rgb_oled_t *dev, uint16_t x, uint16_t y,
                                 const uint16_t *pixels, uint16_t w,
                                 uint16_t h) {
  if (dev == NULL || pixels == NULL || w == 0u || h == 0u ||
      !jh_rgb_oled_begin_write(dev, x, y, w, h)) {
    return false;
  }
  const bool ok =
      jh_rgb_oled_write_pixels_fast(dev, pixels, (size_t)w * (size_t)h);
  return jh_rgb_oled_end_write(dev) && ok;
}

#endif /* HAL_ENABLE_SSD1331 || HAL_ENABLE_SSD135X */
