#include "st7567_driver.h"

#include "hal/hal_config.h"

#ifdef HAL_ENABLE_ST7567

#include "hal/hal_gpio.h"
#include "hal/hal_i2c.h"
#include "hal/hal_spi.h"
#include "hal/hal_status.h"
#include "hal/hal_system.h"

#include <string.h>

#define ST7567_CONTROL_ALL_BYTES_CMD 0x00u
#define ST7567_CONTROL_ALL_BYTES_DATA 0x40u
#define ST7567_DISPLAY_OFF 0xAEu
#define ST7567_DISPLAY_ON 0xAFu
#define ST7567_DISPLAY_ALL_PIXEL_ON 0xA5u
#define ST7567_DISPLAY_ALL_PIXEL_NORMAL 0xA4u
#define ST7567_SET_CONTRAST_CTRL 0x81u
#define ST7567_SET_BIAS 0xA2u
#define ST7567_SET_REGULATION_RATIO 0x20u
#define ST7567_SET_NORMAL_DISPLAY 0xA6u
#define ST7567_SET_REVERSE_DISPLAY 0xA7u
#define ST7567_SET_COM_OUTPUT_SCAN_FLIPPED 0xC8u
#define ST7567_SET_COM_OUTPUT_SCAN_NORMAL 0xC0u
#define ST7567_SET_SEGMENT_MAP_FLIPPED 0xA1u
#define ST7567_SET_SEGMENT_MAP_NORMAL 0xA0u
#define ST7567_POWER_CONTROL 0x28u
#define ST7567_POWER_CONTROL_VF 0x01u
#define ST7567_POWER_CONTROL_VR 0x02u
#define ST7567_POWER_CONTROL_VB 0x04u
#define ST7567_COLUMN_MSB 0x10u
#define ST7567_COLUMN_LSB 0x00u
#define ST7567_PAGE 0xB0u
#define ST7567_LINE_SCROLL 0x40u
#define ST7567_RESET 0xE2u

static bool pin_is_connected(int16_t pin) { return pin >= 0 && pin <= 255; }
static uint8_t pin_to_u8(int16_t pin) { return (uint8_t)pin; }

static uint32_t normalized_clock(const jh_st7567_config_t *config) {
  if (config == NULL || config->clock_hz == 0u) {
    return config != NULL && config->bus_type == JH_ST7567_BUS_SPI
               ? JH_ST7567_DEFAULT_SPI_HZ
               : JH_ST7567_DEFAULT_I2C_HZ;
  }
  return config->clock_hz;
}

static uint8_t normalized_spi_mode(const jh_st7567_config_t *config) {
  return (config != NULL && config->spi_mode <= HAL_SPI_MODE3)
             ? config->spi_mode
             : HAL_SPI_MODE0;
}

static bool i2c_write_control(const jh_st7567_t *dev, uint8_t control,
                              const uint8_t *data, size_t len) {
  if (dev == NULL || data == NULL || len == 0u) {
    return false;
  }
  hal_i2c_begin_transmission_bus(dev->config.bus, dev->config.i2c_addr);
  hal_i2c_write_bus(dev->config.bus, control);
  for (size_t i = 0u; i < len; ++i) {
    hal_i2c_write_bus(dev->config.bus, data[i]);
  }
  return hal_i2c_end_transmission_bus(dev->config.bus) == 0u;
}

static hal_spi_settings_t spi_settings_for(const jh_st7567_t *dev) {
  hal_spi_settings_t settings = {normalized_clock(&dev->config),
                                 HAL_SPI_MSBFIRST,
                                 normalized_spi_mode(&dev->config)};
  return settings;
}

static bool spi_write_control(const jh_st7567_t *dev, bool command,
                              const uint8_t *data, size_t len) {
  if (dev == NULL || data == NULL || len == 0u ||
      !pin_is_connected(dev->config.spi_dc_pin)) {
    return false;
  }
  const uint8_t bus = dev->config.bus;
  const hal_spi_settings_t settings = spi_settings_for(dev);
  hal_spi_lock(bus);
  if (hal_status_is_error(hal_spi_begin_transaction(bus, &settings))) {
    hal_spi_unlock(bus);
    return false;
  }
  if (pin_is_connected(dev->config.spi_cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.spi_cs_pin), false);
  }
  hal_gpio_write(pin_to_u8(dev->config.spi_dc_pin), !command);
  const bool ok = hal_status_is_ok(hal_spi_write(bus, data, len));
  if (pin_is_connected(dev->config.spi_cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.spi_cs_pin), true);
  }
  (void)hal_spi_end_transaction(bus);
  hal_spi_unlock(bus);
  return ok;
}

static bool write_command(const jh_st7567_t *dev, const uint8_t *data,
                          size_t len) {
  if (dev->config.bus_type == JH_ST7567_BUS_SPI) {
    return spi_write_control(dev, true, data, len);
  }
  return i2c_write_control(dev, ST7567_CONTROL_ALL_BYTES_CMD, data, len);
}

static bool write_data(const jh_st7567_t *dev, const uint8_t *data,
                       size_t len) {
  if (dev->config.bus_type == JH_ST7567_BUS_SPI) {
    return spi_write_control(dev, false, data, len);
  }
  return i2c_write_control(dev, ST7567_CONTROL_ALL_BYTES_DATA, data, len);
}

static bool setup_bus_and_pins(jh_st7567_t *dev) {
  if (dev->config.bus_type == JH_ST7567_BUS_SPI) {
    if (!pin_is_connected(dev->config.spi_dc_pin)) {
      return false;
    }
    if (pin_is_connected(dev->config.spi_cs_pin)) {
      hal_gpio_set_mode(pin_to_u8(dev->config.spi_cs_pin), HAL_GPIO_OUTPUT);
      hal_gpio_write(pin_to_u8(dev->config.spi_cs_pin), true);
    }
    hal_gpio_set_mode(pin_to_u8(dev->config.spi_dc_pin), HAL_GPIO_OUTPUT);
    hal_gpio_write(pin_to_u8(dev->config.spi_dc_pin), true);
  } else {
    (void)hal_i2c_set_clock_bus(dev->config.bus, dev->config.clock_hz);
  }
  if (pin_is_connected(dev->config.rst_pin)) {
    const uint8_t rst = pin_to_u8(dev->config.rst_pin);
    hal_gpio_set_mode(rst, HAL_GPIO_OUTPUT);
    hal_gpio_write(rst, true);
    hal_delay_ms(1u);
    hal_gpio_write(rst, false);
    hal_delay_ms(1u);
    hal_gpio_write(rst, true);
    hal_delay_ms(1u);
  }
  return true;
}

static bool hardware_config(jh_st7567_t *dev) {
  uint8_t cmd = ST7567_SET_BIAS | (dev->config.bias ? 1u : 0u);
  if (!write_command(dev, &cmd, 1u)) {
    return false;
  }
  cmd = ST7567_POWER_CONTROL | ST7567_POWER_CONTROL_VB;
  if (!write_command(dev, &cmd, 1u)) {
    return false;
  }
  cmd =
      ST7567_POWER_CONTROL | ST7567_POWER_CONTROL_VB | ST7567_POWER_CONTROL_VR;
  if (!write_command(dev, &cmd, 1u)) {
    return false;
  }
  cmd = ST7567_POWER_CONTROL | ST7567_POWER_CONTROL_VB |
        ST7567_POWER_CONTROL_VR | ST7567_POWER_CONTROL_VF;
  if (!write_command(dev, &cmd, 1u)) {
    return false;
  }
  cmd = ST7567_SET_REGULATION_RATIO | (dev->config.regulation_ratio & 0x07u);
  if (!write_command(dev, &cmd, 1u)) {
    return false;
  }
  cmd = ST7567_LINE_SCROLL | (dev->config.line_offset & 0x3Fu);
  return write_command(dev, &cmd, 1u);
}

bool jh_st7567_init(jh_st7567_t *dev, const jh_st7567_config_t *config) {
  if (dev == NULL || config == NULL || config->width == 0u ||
      config->height == 0u || config->bus_type > JH_ST7567_BUS_SPI ||
      config->pixel_format > JH_ST7567_PIXEL_MONO01) {
    return false;
  }
  memset(dev, 0, sizeof(*dev));
  dev->config = *config;
  dev->config.clock_hz = normalized_clock(config);
  dev->config.spi_mode = normalized_spi_mode(config);
  dev->width = config->width;
  dev->height = config->height;
  if (!setup_bus_and_pins(dev)) {
    return false;
  }
  const uint8_t init[] = {
      ST7567_DISPLAY_OFF,
      (uint8_t)(config->inversion_on ? ST7567_SET_REVERSE_DISPLAY
                                     : ST7567_SET_NORMAL_DISPLAY),
      ST7567_RESET,
  };
  if (!write_command(dev, init, sizeof(init)) || !hardware_config(dev)) {
    return false;
  }
  const uint8_t orientation[] = {
      (uint8_t)(config->segment_invdir ? ST7567_SET_SEGMENT_MAP_FLIPPED
                                       : ST7567_SET_SEGMENT_MAP_NORMAL),
      (uint8_t)(config->com_invdir ? ST7567_SET_COM_OUTPUT_SCAN_FLIPPED
                                   : ST7567_SET_COM_OUTPUT_SCAN_NORMAL),
  };
  if (!write_command(dev, orientation, sizeof(orientation)) ||
      (dev->initialized = true, !jh_st7567_set_contrast(dev, 0x80u))) {
    dev->initialized = false;
    return false;
  }
  const uint8_t on[] = {ST7567_DISPLAY_ALL_PIXEL_NORMAL, ST7567_DISPLAY_ON};
  dev->initialized = write_command(dev, on, sizeof(on));
  dev->suspended = !dev->initialized;
  return dev->initialized;
}

size_t jh_st7567_buffer_size(const jh_st7567_t *dev) {
  if (dev == NULL || dev->width == 0u || dev->height == 0u) {
    return 0u;
  }
  return (size_t)dev->width * (((size_t)dev->height + 7u) / 8u);
}

bool jh_st7567_write(jh_st7567_t *dev, uint16_t x, uint16_t y, uint16_t w,
                     uint16_t h, const uint8_t *buffer, size_t buf_size) {
  const uint16_t page_rounded_height =
      dev != NULL ? (uint16_t)(((dev->height + 7u) / 8u) * 8u) : 0u;
  if (dev == NULL || !dev->initialized || dev->suspended || buffer == NULL ||
      w == 0u || h == 0u || (y & 0x07u) != 0u || (h & 0x07u) != 0u ||
      x >= dev->width || y >= page_rounded_height || w > dev->width - x ||
      h > page_rounded_height - y ||
      buf_size < ((size_t)w * ((size_t)h / 8u))) {
    return false;
  }
  uint8_t cmd[3];
  const size_t bytes_per_page = w;
  const uint8_t pages = (uint8_t)(h / 8u);
  for (uint8_t page = 0u; page < pages; ++page) {
    const uint16_t column = (uint16_t)(x + dev->config.column_offset);
    cmd[0] = (uint8_t)(ST7567_COLUMN_LSB | (column & 0x0Fu));
    cmd[1] = (uint8_t)(ST7567_COLUMN_MSB | ((column >> 4) & 0x0Fu));
    cmd[2] = (uint8_t)(ST7567_PAGE | ((y >> 3u) + page));
    if (!write_command(dev, cmd, sizeof(cmd)) ||
        !write_data(dev, buffer + ((size_t)page * bytes_per_page),
                    bytes_per_page)) {
      return false;
    }
  }
  return true;
}

bool jh_st7567_display(jh_st7567_t *dev, const uint8_t *buffer) {
  if (dev == NULL) {
    return false;
  }
  return jh_st7567_write(dev, 0u, 0u, dev->width,
                         (uint16_t)(((dev->height + 7u) / 8u) * 8u), buffer,
                         jh_st7567_buffer_size(dev));
}

bool jh_st7567_set_contrast(jh_st7567_t *dev, uint8_t contrast) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  const uint8_t cmd[] = {ST7567_SET_CONTRAST_CTRL, contrast};
  return write_command(dev, cmd, sizeof(cmd));
}

bool jh_st7567_set_pixel_format(jh_st7567_t *dev,
                                jh_st7567_pixel_format_t format) {
  if (dev == NULL || !dev->initialized || format > JH_ST7567_PIXEL_MONO01) {
    return false;
  }
  const bool reverse =
      (format == JH_ST7567_PIXEL_MONO01) ^ dev->config.inversion_on;
  const uint8_t cmd =
      reverse ? ST7567_SET_REVERSE_DISPLAY : ST7567_SET_NORMAL_DISPLAY;
  if (!write_command(dev, &cmd, 1u)) {
    return false;
  }
  dev->config.pixel_format = format;
  return true;
}

bool jh_st7567_suspend(jh_st7567_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  const uint8_t cmd[] = {ST7567_DISPLAY_OFF, ST7567_DISPLAY_ALL_PIXEL_ON};
  dev->suspended = write_command(dev, cmd, sizeof(cmd));
  return dev->suspended;
}

bool jh_st7567_resume(jh_st7567_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  const uint8_t cmd[] = {ST7567_DISPLAY_ALL_PIXEL_NORMAL, ST7567_DISPLAY_ON};
  if (!write_command(dev, cmd, sizeof(cmd))) {
    return false;
  }
  dev->suspended = false;
  return true;
}

#endif /* HAL_ENABLE_ST7567 */
