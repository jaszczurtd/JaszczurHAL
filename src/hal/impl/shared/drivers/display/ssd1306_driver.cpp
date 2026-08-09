#include "ssd1306_driver.h"

#include "hal/hal_config.h"

#if defined(HAL_ENABLE_DISPLAY) && defined(HAL_ENABLE_SSD1306)

#include "hal/hal_gpio.h"
#include "hal/hal_i2c.h"
#ifdef HAL_ENABLE_SPI
#include "hal/hal_spi.h"
#include "hal/hal_spi_device.h"
#endif
#include "hal/hal_system.h"

#include <string.h>

/* SSD1306-family command set. */
#define SSD1306_SETLOWCOLUMN 0x00u
#define SSD1306_SETHIGHCOLUMN 0x10u
#define SSD1306_MEMORYMODE 0x20u
#define SSD1306_COLUMNADDR 0x21u
#define SSD1306_PAGEADDR 0x22u
#define SSD1306_SET_PUMP_VOLTAGE_90 0x33u
#define SSD1306_SETCONTRAST 0x81u
#define CH1115_SETIREF 0x82u
#define SSD1306_CHARGEPUMP 0x8Du
#define SSD1306_SEGREMAP 0xA0u
#define SSD1306_DISPLAYALLON_RESUME 0xA4u
#define SSD1306_NORMALDISPLAY 0xA6u
#define SSD1306_INVERTDISPLAY 0xA7u
#define SSD1306_SETMULTIPLEX 0xA8u
#define SSD1306_SETIREF 0xADu
#define SH1106_SETDCDC 0xADu
#define SSD1306_DISPLAYOFF 0xAEu
#define SSD1306_DISPLAYON 0xAFu
#define SSD1306_SETPAGESTART 0xB0u
#define SSD1306_COMSCANINC 0xC0u
#define SSD1306_COMSCANDEC 0xC8u
#define SSD1306_SETDISPLAYOFFSET 0xD3u
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5u
#define SSD1306_SETPRECHARGE 0xD9u
#define SSD1306_SETCOMPINS 0xDAu
#define SSD1306_SETVCOMDETECT 0xDBu
#define SSD1306_SETSTARTLINE 0x40u
#define SSD1306_DEACTIVATE_SCROLL 0x2Eu

/* Control bytes prefixing an I2C transmission. */
#define SSD1306_CTRL_COMMAND 0x00u
#define SSD1306_CTRL_DATA 0x40u

/* Maximum payload bytes per I2C transmission (Wire-compatible chunking). */
#define SSD1306_I2C_CHUNK 16u

static bool pin_is_connected(int16_t pin) { return pin >= 0 && pin <= 255; }
static uint8_t pin_to_u8(int16_t pin) { return (uint8_t)pin; }

static bool controller_valid(jh_ssd1306_controller_t controller) {
  return controller >= JH_SSD1306_CONTROLLER_SSD1306 &&
         controller <= JH_SSD1306_CONTROLLER_CH1115;
}

static bool bus_type_valid(jh_ssd1306_bus_t bus_type) {
  return bus_type == JH_SSD1306_BUS_I2C || bus_type == JH_SSD1306_BUS_SPI;
}

static bool uses_page_addressing(jh_ssd1306_controller_t controller) {
  return controller == JH_SSD1306_CONTROLLER_SH1106 ||
         controller == JH_SSD1306_CONTROLLER_CH1115;
}

static uint32_t normalized_clock(const jh_ssd1306_config_t *config) {
  if (config != NULL && config->clock_hz != 0u) {
    return config->clock_hz;
  }
  return (config != NULL && config->bus_type == JH_SSD1306_BUS_SPI)
             ? JH_SSD1306_DEFAULT_SPI_HZ
             : JH_SSD1306_DEFAULT_I2C_HZ;
}

static uint8_t normalized_spi_mode(const jh_ssd1306_config_t *config) {
#ifdef HAL_ENABLE_SPI
  return (config != NULL && config->spi_mode <= HAL_SPI_MODE3)
             ? config->spi_mode
             : HAL_SPI_MODE0;
#else
  (void)config;
  return 0u;
#endif
}

static uint8_t default_address(uint16_t height) {
  /* Match the Adafruit default: 0x3C for 32px-tall panels, 0x3D otherwise. */
  return (height == 32u) ? 0x3Cu : 0x3Du;
}

static uint8_t default_com_pins(const jh_ssd1306_t *dev) {
  if (dev->width == 128u && dev->height == 64u) {
    return 0x12u;
  }
  if (dev->width == 64u && dev->height == 32u) {
    return 0x12u;
  }
  return 0x02u;
}

static uint8_t default_contrast(const jh_ssd1306_t *dev, uint8_t vcc) {
  if (dev->width == 128u && dev->height == 64u) {
    return (vcc == JH_SSD1306_EXTERNALVCC) ? 0x9Fu : 0xCFu;
  }
  if (dev->width == 96u && dev->height == 16u) {
    return (vcc == JH_SSD1306_EXTERNALVCC) ? 0x10u : 0xAFu;
  }
  if (dev->width == 64u && dev->height == 32u) {
    return (vcc == JH_SSD1306_EXTERNALVCC) ? 0x10u : 0xCFu;
  }
  return 0x8Fu;
}

static uint8_t vcom_level_for(const jh_ssd1306_t *dev) {
  if (dev->controller == JH_SSD1306_CONTROLLER_SSD1309) {
    return 0x34u;
  }
  if (dev->controller == JH_SSD1306_CONTROLLER_SH1106 ||
      dev->controller == JH_SSD1306_CONTROLLER_CH1115) {
    return 0x35u;
  }
  /* Keep the historical JaszczurHAL/Adafruit SSD1306 default. */
  return 0x40u;
}

static bool i2c_write_control(const jh_ssd1306_t *dev, uint8_t control,
                              const uint8_t *bytes, size_t len) {
  if (bytes == NULL && len > 0u) {
    return false;
  }
  hal_i2c_begin_transmission_bus(dev->config.bus, dev->i2c_addr);
  hal_i2c_write_bus(dev->config.bus, control);
  for (size_t i = 0u; i < len; ++i) {
    hal_i2c_write_bus(dev->config.bus, bytes[i]);
  }
  return hal_i2c_end_transmission_bus(dev->config.bus) == 0u;
}

static bool spi_write_control(jh_ssd1306_t *dev, bool command,
                              const uint8_t *bytes, size_t len) {
#ifndef HAL_ENABLE_SPI
  (void)dev;
  (void)command;
  (void)bytes;
  (void)len;
  return false;
#else
  if (dev == NULL || !pin_is_connected(dev->config.spi_dc_pin) ||
      (bytes == NULL && len > 0u)) {
    return false;
  }

  hal_status_t status = hal_spi_device_acquire(&dev->spi_device);
  if (hal_status_is_error(status)) {
    return false;
  }
  hal_gpio_write(pin_to_u8(dev->config.spi_dc_pin), !command);
  if (len > 0u) {
    status = hal_spi_write(dev->spi_device.bus, bytes, len);
  }
  return hal_status_is_ok(hal_spi_device_finish(&dev->spi_device, status));
#endif
}

static bool bus_write_control(jh_ssd1306_t *dev, bool command,
                              const uint8_t *bytes, size_t len) {
  if (dev == NULL) {
    return false;
  }
  if (dev->bus_type == JH_SSD1306_BUS_SPI) {
    return spi_write_control(dev, command, bytes, len);
  }
  return i2c_write_control(
      dev, command ? SSD1306_CTRL_COMMAND : SSD1306_CTRL_DATA, bytes, len);
}

static bool ssd1306_command(jh_ssd1306_t *dev, uint8_t command) {
  return bus_write_control(dev, true, &command, 1u);
}

static bool ssd1306_data(jh_ssd1306_t *dev, const uint8_t *data, size_t len) {
  return bus_write_control(dev, false, data, len);
}

static bool setup_bus_and_pins(jh_ssd1306_t *dev) {
  if (dev->bus_type == JH_SSD1306_BUS_SPI) {
#ifndef HAL_ENABLE_SPI
    return false;
#else
    if (!pin_is_connected(dev->config.spi_dc_pin)) {
      return false;
    }
    const hal_spi_settings_t settings = {dev->config.clock_hz, HAL_SPI_MSBFIRST,
                                         dev->config.spi_mode};
    const uint8_t cs_pin = pin_is_connected(dev->config.spi_cs_pin)
                               ? pin_to_u8(dev->config.spi_cs_pin)
                               : HAL_SPI_DEVICE_CS_NONE;
    if (hal_status_is_error(hal_spi_device_init(
            &dev->spi_device, dev->config.bus, cs_pin, &settings))) {
      return false;
    }
    hal_gpio_set_mode(pin_to_u8(dev->config.spi_dc_pin), HAL_GPIO_OUTPUT);
    hal_gpio_write(pin_to_u8(dev->config.spi_dc_pin), true);
    return true;
#endif
  }

  hal_i2c_set_clock_bus(dev->config.bus, dev->config.clock_hz);
  return true;
}

static bool reset_panel(const jh_ssd1306_t *dev) {
  if (!pin_is_connected(dev->config.rst_pin)) {
    return true;
  }
  const uint8_t rst = pin_to_u8(dev->config.rst_pin);
  hal_gpio_set_mode(rst, HAL_GPIO_OUTPUT);
  hal_gpio_write(rst, true);
  hal_delay_ms(1u);
  hal_gpio_write(rst, false);
  hal_delay_ms(10u);
  hal_gpio_write(rst, true);
  return true;
}

static bool set_panel_orientation(jh_ssd1306_t *dev,
                                  jh_ssd1306_orientation_t orientation) {
  if (dev == NULL) {
    return false;
  }
  const bool rotated = orientation == JH_SSD1306_ORIENTATION_ROTATED_180;
  bool ok = true;

  /* Native keeps historical JaszczurHAL SSD1306 orientation (A1 + C8). */
  ok = ok &&
       ssd1306_command(dev, (uint8_t)(SSD1306_SEGREMAP | (rotated ? 0u : 1u)));
  ok = ok &&
       ssd1306_command(dev, rotated ? SSD1306_COMSCANINC : SSD1306_COMSCANDEC);
  if (ok) {
    dev->orientation = orientation;
  }
  return ok;
}

static bool set_power_settings(jh_ssd1306_t *dev, uint8_t vcc) {
  bool ok = true;

  if (dev->controller == JH_SSD1306_CONTROLLER_SSD1306 ||
      dev->controller == JH_SSD1306_CONTROLLER_SSD1315) {
    ok = ok && ssd1306_command(dev, SSD1306_CHARGEPUMP);
    ok = ok &&
         ssd1306_command(dev, (vcc == JH_SSD1306_EXTERNALVCC) ? 0x10u : 0x14u);
    ok = ok && ssd1306_command(dev, SSD1306_SET_PUMP_VOLTAGE_90);
  } else if (dev->controller == JH_SSD1306_CONTROLLER_SH1106 ||
             dev->controller == JH_SSD1306_CONTROLLER_CH1115) {
    ok = ok && ssd1306_command(dev, SH1106_SETDCDC);
    ok = ok && ssd1306_command(dev, 0x8Bu);
  }

  if (dev->controller == JH_SSD1306_CONTROLLER_SSD1315) {
    ok = ok && ssd1306_command(dev, SSD1306_SETIREF);
    ok = ok && ssd1306_command(dev, dev->config.internal_iref ? 0x10u : 0x00u);
  } else if (dev->controller == JH_SSD1306_CONTROLLER_CH1115) {
    ok = ok && ssd1306_command(dev, CH1115_SETIREF);
    ok = ok && ssd1306_command(dev, dev->config.internal_iref ? 0x04u : 0x00u);
  }

  ok = ok && ssd1306_command(dev, SSD1306_SETVCOMDETECT);
  ok = ok && ssd1306_command(dev, vcom_level_for(dev));
  return ok;
}

bool jh_ssd1306_init(jh_ssd1306_t *dev, const jh_ssd1306_config_t *config) {
  if (dev == NULL || config == NULL || config->width == 0u ||
      config->height == 0u || !controller_valid(config->controller) ||
      !bus_type_valid(config->bus_type)) {
    return false;
  }

  memset(dev, 0, sizeof(*dev));
  dev->config = *config;
  dev->config.clock_hz = normalized_clock(config);
  dev->config.spi_mode = normalized_spi_mode(config);
  dev->width = config->width;
  dev->height = config->height;
  dev->controller = config->controller;
  dev->bus_type = config->bus_type;
  dev->i2c_addr =
      config->i2c_addr ? config->i2c_addr : default_address(config->height);
  dev->segment_offset = config->segment_offset;
  dev->page_offset = config->page_offset;
  dev->display_offset = config->display_offset;
  dev->orientation = config->orientation;

  if (!setup_bus_and_pins(dev) || !reset_panel(dev)) {
    return false;
  }

  const uint8_t vcc =
      dev->config.vccstate ? dev->config.vccstate : JH_SSD1306_SWITCHCAPVCC;
  dev->config.vccstate = vcc;

  const uint8_t com_pins = default_com_pins(dev);
  const uint8_t contrast = default_contrast(dev, vcc);

  bool ok = true;
  ok = ok && ssd1306_command(dev, SSD1306_DISPLAYOFF);
  ok = ok && ssd1306_command(dev, SSD1306_SETDISPLAYCLOCKDIV);
  ok = ok && ssd1306_command(dev, 0x80u);
  ok = ok && ssd1306_command(dev, SSD1306_SETMULTIPLEX);
  ok = ok && ssd1306_command(dev, (uint8_t)(dev->height - 1u));

  ok = ok && ssd1306_command(dev, SSD1306_SETDISPLAYOFFSET);
  ok = ok && ssd1306_command(dev, dev->display_offset);
  ok = ok && ssd1306_command(dev, SSD1306_SETSTARTLINE | 0x00u);
  ok = ok && set_power_settings(dev, vcc);

  if (!uses_page_addressing(dev->controller)) {
    ok = ok && ssd1306_command(dev, SSD1306_MEMORYMODE);
    ok = ok && ssd1306_command(dev, 0x00u);
  }

  ok = ok && set_panel_orientation(dev, dev->orientation);
  ok = ok && ssd1306_command(dev, SSD1306_SETCOMPINS);
  ok = ok && ssd1306_command(dev, com_pins);
  ok = ok && ssd1306_command(dev, SSD1306_SETCONTRAST);
  ok = ok && ssd1306_command(dev, contrast);
  dev->contrast = contrast;

  ok = ok && ssd1306_command(dev, SSD1306_SETPRECHARGE);
  ok = ok &&
       ssd1306_command(dev, (vcc == JH_SSD1306_EXTERNALVCC) ? 0x22u : 0xF1u);
  ok = ok && ssd1306_command(dev, SSD1306_DISPLAYALLON_RESUME);
  ok = ok && ssd1306_command(dev, SSD1306_NORMALDISPLAY);
  ok = ok && ssd1306_command(dev, SSD1306_DEACTIVATE_SCROLL);
  ok = ok && ssd1306_command(dev, SSD1306_DISPLAYON);

  dev->initialized = ok;
  dev->suspended = false;
  return ok;
}

static bool display_default(jh_ssd1306_t *dev, const uint8_t *buffer) {
  const uint16_t pages = (uint16_t)((dev->height + 7u) / 8u);
  const uint8_t page_start = dev->page_offset;
  const uint8_t page_end = (uint8_t)(page_start + pages - 1u);
  const uint8_t column_start =
      (uint8_t)(dev->segment_offset + (dev->width == 64u ? 0x20u : 0x00u));
  const uint8_t column_end = (uint8_t)(column_start + dev->width - 1u);

  bool ok = true;
  ok = ok && ssd1306_command(dev, SSD1306_PAGEADDR);
  ok = ok && ssd1306_command(dev, page_start);
  ok = ok && ssd1306_command(dev, page_end);
  ok = ok && ssd1306_command(dev, SSD1306_COLUMNADDR);
  ok = ok && ssd1306_command(dev, column_start);
  ok = ok && ssd1306_command(dev, column_end);
  if (!ok) {
    return false;
  }

  size_t remaining = jh_ssd1306_buffer_size(dev);
  const uint8_t *ptr = buffer;
  while (remaining > 0u) {
    const size_t chunk =
        remaining < SSD1306_I2C_CHUNK ? remaining : SSD1306_I2C_CHUNK;
    if (!ssd1306_data(dev, ptr, chunk)) {
      return false;
    }
    ptr += chunk;
    remaining -= chunk;
  }
  return true;
}

static bool display_page_addressed(jh_ssd1306_t *dev, const uint8_t *buffer) {
  const uint16_t pages = (uint16_t)((dev->height + 7u) / 8u);
  const uint8_t column_start = dev->segment_offset;

  for (uint16_t page = 0u; page < pages; ++page) {
    const uint8_t page_addr = (uint8_t)(dev->page_offset + page);
    bool ok = true;
    ok = ok && ssd1306_command(dev, (uint8_t)(SSD1306_SETLOWCOLUMN |
                                              (column_start & 0x0Fu)));
    ok = ok && ssd1306_command(dev, (uint8_t)(SSD1306_SETHIGHCOLUMN |
                                              ((column_start >> 4) & 0x0Fu)));
    ok = ok && ssd1306_command(
                   dev, (uint8_t)(SSD1306_SETPAGESTART | (page_addr & 0x07u)));
    if (!ok) {
      return false;
    }

    const uint8_t *ptr = &buffer[(size_t)page * dev->width];
    size_t remaining = dev->width;
    while (remaining > 0u) {
      const size_t chunk =
          remaining < SSD1306_I2C_CHUNK ? remaining : SSD1306_I2C_CHUNK;
      if (!ssd1306_data(dev, ptr, chunk)) {
        return false;
      }
      ptr += chunk;
      remaining -= chunk;
    }
  }
  return true;
}

bool jh_ssd1306_display(jh_ssd1306_t *dev, const uint8_t *buffer) {
  if (dev == NULL || !dev->initialized || dev->suspended || buffer == NULL) {
    return false;
  }

  if (uses_page_addressing(dev->controller)) {
    return display_page_addressed(dev, buffer);
  }
  return display_default(dev, buffer);
}

bool jh_ssd1306_invert(jh_ssd1306_t *dev, bool invert) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  return ssd1306_command(dev, invert ? SSD1306_INVERTDISPLAY
                                     : SSD1306_NORMALDISPLAY);
}

bool jh_ssd1306_set_contrast(jh_ssd1306_t *dev, uint8_t contrast) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  if (ssd1306_command(dev, SSD1306_SETCONTRAST) &&
      ssd1306_command(dev, contrast)) {
    dev->contrast = contrast;
    return true;
  }
  return false;
}

bool jh_ssd1306_set_orientation(jh_ssd1306_t *dev,
                                jh_ssd1306_orientation_t orientation) {
  if (dev == NULL || !dev->initialized ||
      orientation > JH_SSD1306_ORIENTATION_ROTATED_180) {
    return false;
  }
  return set_panel_orientation(dev, orientation);
}

bool jh_ssd1306_suspend(jh_ssd1306_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  if (ssd1306_command(dev, SSD1306_DISPLAYOFF)) {
    dev->suspended = true;
    return true;
  }
  return false;
}

bool jh_ssd1306_resume(jh_ssd1306_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  if (ssd1306_command(dev, SSD1306_DISPLAYON)) {
    dev->suspended = false;
    return true;
  }
  return false;
}

size_t jh_ssd1306_buffer_size(const jh_ssd1306_t *dev) {
  if (dev == NULL || dev->width == 0u || dev->height == 0u) {
    return 0u;
  }
  return (size_t)dev->width * (((size_t)dev->height + 7u) / 8u);
}

#endif /* HAL_ENABLE_DISPLAY && HAL_ENABLE_SSD1306 */
