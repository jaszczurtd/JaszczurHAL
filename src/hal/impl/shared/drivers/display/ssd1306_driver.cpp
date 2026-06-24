#include "ssd1306_driver.h"

#include "hal/hal_config.h"

#if defined(HAL_ENABLE_DISPLAY) && defined(HAL_ENABLE_SSD1306)

#include "hal/hal_gpio.h"
#include "hal/hal_i2c.h"
#include "hal/hal_system.h"

#include <string.h>

/* SSD1306 command set (datasheet). */
#define SSD1306_MEMORYMODE 0x20u
#define SSD1306_COLUMNADDR 0x21u
#define SSD1306_PAGEADDR 0x22u
#define SSD1306_SETCONTRAST 0x81u
#define SSD1306_CHARGEPUMP 0x8Du
#define SSD1306_SEGREMAP 0xA0u
#define SSD1306_DISPLAYALLON_RESUME 0xA4u
#define SSD1306_NORMALDISPLAY 0xA6u
#define SSD1306_INVERTDISPLAY 0xA7u
#define SSD1306_SETMULTIPLEX 0xA8u
#define SSD1306_DISPLAYOFF 0xAEu
#define SSD1306_DISPLAYON 0xAFu
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

static uint32_t normalized_clock(const jh_ssd1306_config_t *config) {
  return (config != NULL && config->clock_hz != 0u) ? config->clock_hz
                                                    : JH_SSD1306_DEFAULT_I2C_HZ;
}

static uint8_t default_address(uint16_t height) {
  /* Match the Adafruit default: 0x3C for 32px-tall panels, 0x3D otherwise. */
  return (height == 32u) ? 0x3Cu : 0x3Du;
}

static bool ssd1306_command(const jh_ssd1306_t *dev, uint8_t command) {
  hal_i2c_begin_transmission_bus(dev->config.bus, dev->i2c_addr);
  hal_i2c_write_bus(dev->config.bus, SSD1306_CTRL_COMMAND);
  hal_i2c_write_bus(dev->config.bus, command);
  return hal_i2c_end_transmission_bus(dev->config.bus) == 0u;
}

bool jh_ssd1306_init(jh_ssd1306_t *dev, const jh_ssd1306_config_t *config) {
  if (dev == NULL || config == NULL || config->width == 0u ||
      config->height == 0u) {
    return false;
  }

  memset(dev, 0, sizeof(*dev));
  dev->config = *config;
  dev->config.clock_hz = normalized_clock(config);
  dev->width = config->width;
  dev->height = config->height;
  dev->i2c_addr =
      config->i2c_addr ? config->i2c_addr : default_address(config->height);

  hal_i2c_set_clock_bus(dev->config.bus, dev->config.clock_hz);

  /* Optional hardware reset, identical timing to the Adafruit sequence. */
  if (pin_is_connected(dev->config.rst_pin)) {
    const uint8_t rst = (uint8_t)dev->config.rst_pin;
    hal_gpio_set_mode(rst, HAL_GPIO_OUTPUT);
    hal_gpio_write(rst, true);
    hal_delay_ms(1u);
    hal_gpio_write(rst, false);
    hal_delay_ms(10u);
    hal_gpio_write(rst, true);
  }

  const uint8_t vcc =
      dev->config.vccstate ? dev->config.vccstate : JH_SSD1306_SWITCHCAPVCC;
  dev->config.vccstate = vcc;

  bool ok = true;
  ok = ok && ssd1306_command(dev, SSD1306_DISPLAYOFF);
  ok = ok && ssd1306_command(dev, SSD1306_SETDISPLAYCLOCKDIV);
  ok = ok && ssd1306_command(dev, 0x80u);
  ok = ok && ssd1306_command(dev, SSD1306_SETMULTIPLEX);
  ok = ok && ssd1306_command(dev, (uint8_t)(dev->height - 1u));

  ok = ok && ssd1306_command(dev, SSD1306_SETDISPLAYOFFSET);
  ok = ok && ssd1306_command(dev, 0x00u);
  ok = ok && ssd1306_command(dev, SSD1306_SETSTARTLINE | 0x00u);
  ok = ok && ssd1306_command(dev, SSD1306_CHARGEPUMP);
  ok = ok &&
       ssd1306_command(dev, (vcc == JH_SSD1306_EXTERNALVCC) ? 0x10u : 0x14u);

  ok = ok && ssd1306_command(dev, SSD1306_MEMORYMODE);
  ok = ok && ssd1306_command(dev, 0x00u);
  ok = ok && ssd1306_command(dev, SSD1306_SEGREMAP | 0x01u);
  ok = ok && ssd1306_command(dev, SSD1306_COMSCANDEC);

  /* COM pin and contrast defaults follow the panel geometry. */
  uint8_t com_pins = 0x02u;
  uint8_t contrast = 0x8Fu;
  if (dev->width == 128u && dev->height == 32u) {
    com_pins = 0x02u;
    contrast = 0x8Fu;
  } else if (dev->width == 128u && dev->height == 64u) {
    com_pins = 0x12u;
    contrast = (vcc == JH_SSD1306_EXTERNALVCC) ? 0x9Fu : 0xCFu;
  } else if (dev->width == 96u && dev->height == 16u) {
    com_pins = 0x02u;
    contrast = (vcc == JH_SSD1306_EXTERNALVCC) ? 0x10u : 0xAFu;
  } else if (dev->width == 64u && dev->height == 32u) {
    com_pins = 0x12u;
    contrast = (vcc == JH_SSD1306_EXTERNALVCC) ? 0x10u : 0xCFu;
  }

  ok = ok && ssd1306_command(dev, SSD1306_SETCOMPINS);
  ok = ok && ssd1306_command(dev, com_pins);
  ok = ok && ssd1306_command(dev, SSD1306_SETCONTRAST);
  ok = ok && ssd1306_command(dev, contrast);
  dev->contrast = contrast;

  ok = ok && ssd1306_command(dev, SSD1306_SETPRECHARGE);
  ok = ok &&
       ssd1306_command(dev, (vcc == JH_SSD1306_EXTERNALVCC) ? 0x22u : 0xF1u);
  ok = ok && ssd1306_command(dev, SSD1306_SETVCOMDETECT);
  ok = ok && ssd1306_command(dev, 0x40u);
  ok = ok && ssd1306_command(dev, SSD1306_DISPLAYALLON_RESUME);
  ok = ok && ssd1306_command(dev, SSD1306_NORMALDISPLAY);
  ok = ok && ssd1306_command(dev, SSD1306_DEACTIVATE_SCROLL);
  ok = ok && ssd1306_command(dev, SSD1306_DISPLAYON);

  dev->initialized = ok;
  return ok;
}

bool jh_ssd1306_display(jh_ssd1306_t *dev, const uint8_t *buffer) {
  if (dev == NULL || !dev->initialized || buffer == NULL) {
    return false;
  }

  bool ok = true;
  ok = ok && ssd1306_command(dev, SSD1306_PAGEADDR);
  ok = ok && ssd1306_command(dev, 0x00u);
  ok = ok && ssd1306_command(dev, 0xFFu);
  ok = ok && ssd1306_command(dev, SSD1306_COLUMNADDR);
  if (dev->width == 64u) {
    ok = ok && ssd1306_command(dev, 0x20u);
    ok = ok && ssd1306_command(dev, (uint8_t)(0x20u + dev->width - 1u));
  } else {
    ok = ok && ssd1306_command(dev, 0x00u);
    ok = ok && ssd1306_command(dev, (uint8_t)(dev->width - 1u));
  }
  if (!ok) {
    return false;
  }

  size_t remaining = jh_ssd1306_buffer_size(dev);
  const uint8_t *ptr = buffer;
  while (remaining > 0u) {
    size_t chunk =
        remaining < SSD1306_I2C_CHUNK ? remaining : SSD1306_I2C_CHUNK;
    hal_i2c_begin_transmission_bus(dev->config.bus, dev->i2c_addr);
    hal_i2c_write_bus(dev->config.bus, SSD1306_CTRL_DATA);
    for (size_t i = 0u; i < chunk; ++i) {
      hal_i2c_write_bus(dev->config.bus, *ptr++);
    }
    if (hal_i2c_end_transmission_bus(dev->config.bus) != 0u) {
      return false;
    }
    remaining -= chunk;
  }
  return true;
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

size_t jh_ssd1306_buffer_size(const jh_ssd1306_t *dev) {
  if (dev == NULL || dev->width == 0u || dev->height == 0u) {
    return 0u;
  }
  return (size_t)dev->width * (((size_t)dev->height + 7u) / 8u);
}

#endif /* HAL_ENABLE_DISPLAY && HAL_ENABLE_SSD1306 */
