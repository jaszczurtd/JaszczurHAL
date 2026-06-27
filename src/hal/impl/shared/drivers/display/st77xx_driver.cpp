#include "st77xx_driver.h"

#include "hal/hal_config.h"

#if defined(HAL_ENABLE_DISPLAY) && defined(HAL_ENABLE_TFT) &&                  \
    (defined(HAL_ENABLE_ST7735) || defined(HAL_ENABLE_ST7789) ||               \
     defined(HAL_ENABLE_ST7796S))

#include "hal/hal_gpio.h"
#include "hal/hal_spi.h"
#include "hal/hal_system.h"

#include <string.h>

#define ST_CMD_DELAY 0x80u

#define ST77XX_SWRESET 0x01u
#define ST77XX_SLPOUT 0x11u
#define ST77XX_NORON 0x13u
#define ST77XX_INVOFF 0x20u
#define ST77XX_INVON 0x21u
#define ST77XX_DISPON 0x29u
#define ST77XX_CASET 0x2Au
#define ST77XX_RASET 0x2Bu
#define ST77XX_RAMWR 0x2Cu
#define ST77XX_MADCTL 0x36u
#define ST77XX_COLMOD 0x3Au

#define ST77XX_MADCTL_MY 0x80u
#define ST77XX_MADCTL_MX 0x40u
#define ST77XX_MADCTL_MV 0x20u
#define ST77XX_MADCTL_RGB 0x00u

#define ST7735_MADCTL_BGR 0x08u

#define ST7735_FRMCTR1 0xB1u
#define ST7735_FRMCTR2 0xB2u
#define ST7735_FRMCTR3 0xB3u
#define ST7735_INVCTR 0xB4u
#define ST7735_DISSET5 0xB6u
#define ST7735_PWCTR1 0xC0u
#define ST7735_PWCTR2 0xC1u
#define ST7735_PWCTR3 0xC2u
#define ST7735_PWCTR4 0xC3u
#define ST7735_PWCTR5 0xC4u
#define ST7735_VMCTR1 0xC5u
#define ST7735_PWCTR6 0xFCu
#define ST7735_GMCTRP1 0xE0u
#define ST7735_GMCTRN1 0xE1u

#define ST7796S_BGR 0x08u

#define ST77XX_PIXEL_CHUNK_BYTES 256u

static const uint8_t s_st7735_bcmd[] = {18u,
                                        ST77XX_SWRESET,
                                        ST_CMD_DELAY,
                                        50u,
                                        ST77XX_SLPOUT,
                                        ST_CMD_DELAY,
                                        255u,
                                        ST77XX_COLMOD,
                                        1u + ST_CMD_DELAY,
                                        0x05u,
                                        10u,
                                        ST7735_FRMCTR1,
                                        3u + ST_CMD_DELAY,
                                        0x00u,
                                        0x06u,
                                        0x03u,
                                        10u,
                                        ST77XX_MADCTL,
                                        1u,
                                        0x08u,
                                        ST7735_DISSET5,
                                        2u,
                                        0x15u,
                                        0x02u,
                                        ST7735_INVCTR,
                                        1u,
                                        0x00u,
                                        ST7735_PWCTR1,
                                        2u + ST_CMD_DELAY,
                                        0x02u,
                                        0x70u,
                                        10u,
                                        ST7735_PWCTR2,
                                        1u,
                                        0x05u,
                                        ST7735_PWCTR3,
                                        2u,
                                        0x01u,
                                        0x02u,
                                        ST7735_VMCTR1,
                                        2u + ST_CMD_DELAY,
                                        0x3Cu,
                                        0x38u,
                                        10u,
                                        ST7735_PWCTR6,
                                        2u,
                                        0x11u,
                                        0x15u,
                                        ST7735_GMCTRP1,
                                        16u,
                                        0x09u,
                                        0x16u,
                                        0x09u,
                                        0x20u,
                                        0x21u,
                                        0x1Bu,
                                        0x13u,
                                        0x19u,
                                        0x17u,
                                        0x15u,
                                        0x1Eu,
                                        0x2Bu,
                                        0x04u,
                                        0x05u,
                                        0x02u,
                                        0x0Eu,
                                        ST7735_GMCTRN1,
                                        16u + ST_CMD_DELAY,
                                        0x0Bu,
                                        0x14u,
                                        0x08u,
                                        0x1Eu,
                                        0x22u,
                                        0x1Du,
                                        0x18u,
                                        0x1Eu,
                                        0x1Bu,
                                        0x1Au,
                                        0x24u,
                                        0x2Bu,
                                        0x06u,
                                        0x06u,
                                        0x02u,
                                        0x0Fu,
                                        10u,
                                        ST77XX_CASET,
                                        4u,
                                        0x00u,
                                        0x02u,
                                        0x00u,
                                        0x81u,
                                        ST77XX_RASET,
                                        4u,
                                        0x00u,
                                        0x02u,
                                        0x00u,
                                        0x81u,
                                        ST77XX_NORON,
                                        ST_CMD_DELAY,
                                        10u,
                                        ST77XX_DISPON,
                                        ST_CMD_DELAY,
                                        255u};

static const uint8_t s_st7735_rcmd1[] = {15u,
                                         ST77XX_SWRESET,
                                         ST_CMD_DELAY,
                                         150u,
                                         ST77XX_SLPOUT,
                                         ST_CMD_DELAY,
                                         255u,
                                         ST7735_FRMCTR1,
                                         3u,
                                         0x01u,
                                         0x2Cu,
                                         0x2Du,
                                         ST7735_FRMCTR2,
                                         3u,
                                         0x01u,
                                         0x2Cu,
                                         0x2Du,
                                         ST7735_FRMCTR3,
                                         6u,
                                         0x01u,
                                         0x2Cu,
                                         0x2Du,
                                         0x01u,
                                         0x2Cu,
                                         0x2Du,
                                         ST7735_INVCTR,
                                         1u,
                                         0x07u,
                                         ST7735_PWCTR1,
                                         3u,
                                         0xA2u,
                                         0x02u,
                                         0x84u,
                                         ST7735_PWCTR2,
                                         1u,
                                         0xC5u,
                                         ST7735_PWCTR3,
                                         2u,
                                         0x0Au,
                                         0x00u,
                                         ST7735_PWCTR4,
                                         2u,
                                         0x8Au,
                                         0x2Au,
                                         ST7735_PWCTR5,
                                         2u,
                                         0x8Au,
                                         0xEEu,
                                         ST7735_VMCTR1,
                                         1u,
                                         0x0Eu,
                                         ST77XX_INVOFF,
                                         0u,
                                         ST77XX_MADCTL,
                                         1u,
                                         0xC8u,
                                         ST77XX_COLMOD,
                                         1u,
                                         0x05u};

static const uint8_t s_st7735_rcmd2_green[] = {
    2u,           ST77XX_CASET, 4u,    0x00u, 0x02u, 0x00u, 0x81u,
    ST77XX_RASET, 4u,           0x00u, 0x01u, 0x00u, 0xA0u};

static const uint8_t s_st7735_rcmd2_red[] = {
    2u,           ST77XX_CASET, 4u,    0x00u, 0x00u, 0x00u, 0x7Fu,
    ST77XX_RASET, 4u,           0x00u, 0x00u, 0x00u, 0x9Fu};

static const uint8_t s_st7735_rcmd2_green144[] = {
    2u,           ST77XX_CASET, 4u,    0x00u, 0x00u, 0x00u, 0x7Fu,
    ST77XX_RASET, 4u,           0x00u, 0x00u, 0x00u, 0x7Fu};

static const uint8_t s_st7735_rcmd2_green160x80[] = {
    2u,           ST77XX_CASET, 4u,    0x00u, 0x00u, 0x00u, 0x4Fu,
    ST77XX_RASET, 4u,           0x00u, 0x00u, 0x00u, 0x9Fu};

static const uint8_t s_st7735_rcmd2_green160x80_plugin[] = {
    3u,    ST77XX_INVON, 0u, ST77XX_CASET, 4u,    0x00u, 0x00u, 0x00u,
    0x4Fu, ST77XX_RASET, 4u, 0x00u,        0x00u, 0x00u, 0x9Fu};

static const uint8_t s_st7735_rcmd3[] = {
    4u,    ST7735_GMCTRP1, 16u,          0x02u, 0x1Cu,         0x07u,
    0x12u, 0x37u,          0x32u,        0x29u, 0x2Du,         0x29u,
    0x25u, 0x2Bu,          0x39u,        0x00u, 0x01u,         0x03u,
    0x10u, ST7735_GMCTRN1, 16u,          0x03u, 0x1Du,         0x07u,
    0x06u, 0x2Eu,          0x2Cu,        0x29u, 0x2Du,         0x2Eu,
    0x2Eu, 0x37u,          0x3Fu,        0x00u, 0x00u,         0x02u,
    0x10u, ST77XX_NORON,   ST_CMD_DELAY, 10u,   ST77XX_DISPON, ST_CMD_DELAY,
    100u};

static const uint8_t s_st7789_init[] = {9u,
                                        ST77XX_SWRESET,
                                        ST_CMD_DELAY,
                                        150u,
                                        ST77XX_SLPOUT,
                                        ST_CMD_DELAY,
                                        10u,
                                        ST77XX_COLMOD,
                                        1u + ST_CMD_DELAY,
                                        0x55u,
                                        10u,
                                        ST77XX_MADCTL,
                                        1u,
                                        0x08u,
                                        ST77XX_CASET,
                                        4u,
                                        0x00u,
                                        0x00u,
                                        0x00u,
                                        0xF0u,
                                        ST77XX_RASET,
                                        4u,
                                        0x00u,
                                        0x00u,
                                        0x01u,
                                        0x40u,
                                        ST77XX_INVON,
                                        ST_CMD_DELAY,
                                        10u,
                                        ST77XX_NORON,
                                        ST_CMD_DELAY,
                                        10u,
                                        ST77XX_DISPON,
                                        ST_CMD_DELAY,
                                        10u};

static const uint8_t s_st7796s_init[] = {14u,
                                         ST77XX_SWRESET,
                                         ST_CMD_DELAY,
                                         150u,
                                         0xF0u,
                                         1u,
                                         0xC3u,
                                         0xF0u,
                                         1u,
                                         0x96u,
                                         0xC5u,
                                         1u,
                                         0x1Cu,
                                         ST77XX_MADCTL,
                                         1u,
                                         0x48u,
                                         ST77XX_COLMOD,
                                         1u,
                                         0x55u,
                                         0xB0u,
                                         1u,
                                         0x80u,
                                         0xB4u,
                                         1u,
                                         0x00u,
                                         0xB6u,
                                         3u,
                                         0x80u,
                                         0x02u,
                                         0x3Bu,
                                         0xB7u,
                                         1u,
                                         0xC6u,
                                         0xF0u,
                                         1u,
                                         0x69u,
                                         0xF0u,
                                         1u,
                                         0x3Cu,
                                         ST77XX_SLPOUT,
                                         ST_CMD_DELAY,
                                         150u,
                                         ST77XX_DISPON,
                                         ST_CMD_DELAY,
                                         150u};

static bool pin_is_connected(int16_t pin) { return pin >= 0 && pin <= 255; }

static uint8_t pin_to_u8(int16_t pin) { return (uint8_t)pin; }

static uint32_t normalized_clock(const jh_st77xx_config_t *config) {
  return (config != NULL && config->clock_hz != 0u) ? config->clock_hz
                                                    : JH_ST77XX_SPI_DEFAULT_HZ;
}

static uint8_t normalized_spi_mode(const jh_st77xx_config_t *config) {
  return (config != NULL && config->spi_mode <= HAL_SPI_MODE3)
             ? config->spi_mode
             : HAL_SPI_MODE0;
}

static hal_spi_settings_t spi_settings_for(const jh_st77xx_t *dev) {
  hal_spi_settings_t settings = {normalized_clock(&dev->config),
                                 HAL_SPI_MSBFIRST,
                                 normalized_spi_mode(&dev->config)};
  return settings;
}

static void hal_delay_adapter(void *ctx, uint32_t delay_ms) {
  (void)ctx;
  hal_delay_ms(delay_ms);
}

static bool hal_write_command(void *ctx, uint8_t command, const uint8_t *data,
                              uint8_t data_len) {
  jh_st77xx_t *dev = (jh_st77xx_t *)ctx;
  if (dev == NULL || !pin_is_connected(dev->config.dc_pin)) {
    return false;
  }

  const uint8_t bus = dev->config.bus;
  const hal_spi_settings_t settings = spi_settings_for(dev);

  hal_spi_lock(bus);
  hal_spi_begin_transaction(bus, &settings);

  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), false);
  }

  hal_gpio_write(pin_to_u8(dev->config.dc_pin), false);
  hal_spi_write(bus, &command, 1u);
  hal_gpio_write(pin_to_u8(dev->config.dc_pin), true);
  if (data != NULL && data_len > 0u) {
    hal_spi_write(bus, data, data_len);
  }

  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), true);
  }

  hal_spi_end_transaction(bus);
  hal_spi_unlock(bus);
  return true;
}

static bool write_command(jh_st77xx_t *dev, uint8_t command,
                          const uint8_t *data, uint8_t data_len) {
  return hal_write_command(dev, command, data, data_len);
}

bool jh_st77xx_run_sequence(const jh_st77xx_command_io_t *io,
                            const uint8_t *sequence) {
  if (io == NULL || io->write_command == NULL || sequence == NULL) {
    return false;
  }

  uint8_t command_count = *sequence++;
  while (command_count-- > 0u) {
    const uint8_t command = *sequence++;
    uint8_t arg_count = *sequence++;
    const bool has_delay = (arg_count & ST_CMD_DELAY) != 0u;
    arg_count &= (uint8_t)~ST_CMD_DELAY;

    if (!io->write_command(io->ctx, command, sequence, arg_count)) {
      return false;
    }
    sequence += arg_count;

    if (has_delay) {
      uint32_t delay_ms = *sequence++;
      if (delay_ms == 255u) {
        delay_ms = 500u;
      }
      if (io->delay_ms != NULL) {
        io->delay_ms(io->ctx, delay_ms);
      }
    }
  }

  return true;
}

static const uint8_t *st7735_part2_for_tab(uint8_t tab) {
  if (tab == JH_ST7735_TAB_GREENTAB) {
    return s_st7735_rcmd2_green;
  }
  if (tab == JH_ST7735_TAB_144GREENTAB || tab == JH_ST7735_TAB_HALLOWING) {
    return s_st7735_rcmd2_green144;
  }
  if (tab == JH_ST7735_TAB_MINI160X80) {
    return s_st7735_rcmd2_green160x80;
  }
  if (tab == JH_ST7735_TAB_MINI160X80_PLUGIN) {
    return s_st7735_rcmd2_green160x80_plugin;
  }
  return s_st7735_rcmd2_red;
}

bool jh_st77xx_run_st7735_init_sequence(const jh_st77xx_command_io_t *io,
                                        uint8_t tab) {
  if (tab == JH_ST7735_TAB_B) {
    return jh_st77xx_run_sequence(io, s_st7735_bcmd);
  }

  if (!jh_st77xx_run_sequence(io, s_st7735_rcmd1)) {
    return false;
  }
  if (!jh_st77xx_run_sequence(io, st7735_part2_for_tab(tab))) {
    return false;
  }
  return jh_st77xx_run_sequence(io, s_st7735_rcmd3);
}

bool jh_st77xx_run_st7789_init_sequence(const jh_st77xx_command_io_t *io) {
  return jh_st77xx_run_sequence(io, s_st7789_init);
}

bool jh_st77xx_run_st7796s_init_sequence(const jh_st77xx_command_io_t *io) {
  return jh_st77xx_run_sequence(io, s_st7796s_init);
}

static bool run_init_sequence_for_config(jh_st77xx_t *dev) {
  const jh_st77xx_command_io_t io = {dev, hal_write_command, hal_delay_adapter};

  switch (dev->config.chip) {
  case JH_ST77XX_CHIP_ST7735:
    return jh_st77xx_run_st7735_init_sequence(&io, dev->config.st7735_tab);
  case JH_ST77XX_CHIP_ST7789:
    return jh_st77xx_run_st7789_init_sequence(&io);
  case JH_ST77XX_CHIP_ST7796S:
    return jh_st77xx_run_st7796s_init_sequence(&io);
  default:
    return false;
  }
}

static void set_default_geometry(jh_st77xx_t *dev) {
  dev->invert_on_command = ST77XX_INVON;
  dev->invert_off_command = ST77XX_INVOFF;
  dev->color_order = dev->config.bgr ? ST7796S_BGR : ST77XX_MADCTL_RGB;

  switch (dev->config.chip) {
  case JH_ST77XX_CHIP_ST7735:
    dev->window_width =
        dev->config.width ? dev->config.width : JH_ST7735_TFTWIDTH_128;
    dev->window_height =
        dev->config.height ? dev->config.height : JH_ST7735_TFTHEIGHT_160;
    dev->width = dev->window_width;
    dev->height = dev->window_height;
    break;
  case JH_ST77XX_CHIP_ST7789:
    dev->window_width = dev->config.width ? dev->config.width : 240u;
    dev->window_height = dev->config.height ? dev->config.height : 320u;
    dev->width = dev->window_width;
    dev->height = dev->window_height;
    break;
  case JH_ST77XX_CHIP_ST7796S:
    dev->window_width =
        dev->config.width ? dev->config.width : JH_ST7796S_TFTWIDTH;
    dev->window_height =
        dev->config.height ? dev->config.height : JH_ST7796S_TFTHEIGHT;
    dev->width = dev->window_width;
    dev->height = dev->window_height;
    dev->row_start = dev->config.row_offset;
    dev->col_start = dev->config.col_offset;
    dev->invert_on_command = ST77XX_INVOFF;
    dev->invert_off_command = ST77XX_INVON;
    break;
  default:
    break;
  }
}

static void configure_st7735_after_sequence(jh_st77xx_t *dev) {
  const uint8_t tab = dev->config.st7735_tab;

  if (tab == JH_ST7735_TAB_B) {
    dev->col_start = 2u;
    dev->row_start = 2u;
    dev->window_width =
        dev->config.width ? dev->config.width : JH_ST7735_TFTWIDTH_128;
    dev->window_height =
        dev->config.height ? dev->config.height : JH_ST7735_TFTHEIGHT_160;
    return;
  }

  if (tab == JH_ST7735_TAB_GREENTAB) {
    dev->col_start = 2u;
    dev->row_start = 1u;
  } else if (tab == JH_ST7735_TAB_144GREENTAB ||
             tab == JH_ST7735_TAB_HALLOWING) {
    dev->window_width =
        dev->config.width ? dev->config.width : JH_ST7735_TFTWIDTH_128;
    dev->window_height =
        dev->config.height ? dev->config.height : JH_ST7735_TFTHEIGHT_128;
    dev->col_start = 2u;
    dev->row_start = 3u;
  } else if (tab == JH_ST7735_TAB_MINI160X80) {
    dev->window_width =
        dev->config.width ? dev->config.width : JH_ST7735_TFTWIDTH_80;
    dev->window_height =
        dev->config.height ? dev->config.height : JH_ST7735_TFTHEIGHT_160;
    dev->col_start = 24u;
    dev->row_start = 0u;
  } else if (tab == JH_ST7735_TAB_MINI160X80_PLUGIN) {
    dev->window_width =
        dev->config.width ? dev->config.width : JH_ST7735_TFTWIDTH_80;
    dev->window_height =
        dev->config.height ? dev->config.height : JH_ST7735_TFTHEIGHT_160;
    dev->col_start = 26u;
    dev->row_start = 1u;
    dev->invert_on_command = ST77XX_INVOFF;
    dev->invert_off_command = ST77XX_INVON;
  }

  if (tab == JH_ST7735_TAB_HALLOWING) {
    dev->config.st7735_tab = JH_ST7735_TAB_144GREENTAB;
  }
}

static void configure_st7789_offsets(jh_st77xx_t *dev) {
  const uint16_t width = dev->window_width;
  const uint16_t height = dev->window_height;

  if (width == 240u && height == 240u) {
    dev->row_start = (uint8_t)(320u - height);
    dev->row_start2 = 0u;
    dev->col_start = (uint8_t)(240u - width);
    dev->col_start2 = dev->col_start;
  } else if (width == 135u && height == 240u) {
    dev->row_start = (uint8_t)((320u - height) / 2u);
    dev->row_start2 = dev->row_start;
    dev->col_start = (uint8_t)((240u - width + 1u) / 2u);
    dev->col_start2 = (uint8_t)((240u - width) / 2u);
  } else {
    dev->row_start = (uint8_t)((320u - height) / 2u);
    dev->row_start2 = dev->row_start;
    dev->col_start = (uint8_t)((240u - width) / 2u);
    dev->col_start2 = dev->col_start;
  }
}

static bool setup_pins_and_reset(jh_st77xx_t *dev) {
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
    hal_delay_ms(100u);
    hal_gpio_write(rst, false);
    hal_delay_ms(100u);
    hal_gpio_write(rst, true);
    hal_delay_ms(200u);
  }
  return true;
}

bool jh_st77xx_init(jh_st77xx_t *dev, const jh_st77xx_config_t *config) {
  if (dev == NULL || config == NULL || !pin_is_connected(config->dc_pin)) {
    return false;
  }

  memset(dev, 0, sizeof(*dev));
  dev->config = *config;
  dev->config.clock_hz = normalized_clock(config);
  dev->config.spi_mode = normalized_spi_mode(config);

  set_default_geometry(dev);
  if (!setup_pins_and_reset(dev)) {
    return false;
  }
  if (!run_init_sequence_for_config(dev)) {
    return false;
  }

  const bool st7735_hallowing =
      dev->config.chip == JH_ST77XX_CHIP_ST7735 &&
      dev->config.st7735_tab == JH_ST7735_TAB_HALLOWING;
  if (dev->config.chip == JH_ST77XX_CHIP_ST7735) {
    configure_st7735_after_sequence(dev);
  } else if (dev->config.chip == JH_ST77XX_CHIP_ST7789) {
    configure_st7789_offsets(dev);
  }

  dev->initialized = true;
  if (!jh_st77xx_set_rotation(dev, st7735_hallowing ? 2u : 0u)) {
    return false;
  }
  if (dev->config.chip == JH_ST77XX_CHIP_ST7796S) {
    return jh_st77xx_invert(dev, false);
  }
  return true;
}

bool jh_st77xx_soft_init(jh_st77xx_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  return run_init_sequence_for_config(dev);
}

bool jh_st77xx_set_rotation(jh_st77xx_t *dev, uint8_t rotation) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }

  uint8_t madctl = 0u;
  dev->rotation = rotation & 0x03u;

  if (dev->config.chip == JH_ST77XX_CHIP_ST7735) {
    const uint8_t tab = dev->config.st7735_tab;
    const bool rgb_tab =
        tab == JH_ST7735_TAB_BLACKTAB || tab == JH_ST7735_TAB_MINI160X80;
    const uint8_t color_order = rgb_tab ? ST77XX_MADCTL_RGB : ST7735_MADCTL_BGR;

    if (tab == JH_ST7735_TAB_144GREENTAB || tab == JH_ST7735_TAB_HALLOWING) {
      dev->row_start = dev->rotation < 2u ? 3u : 1u;
    }

    switch (dev->rotation) {
    case 0:
      madctl = ST77XX_MADCTL_MX | ST77XX_MADCTL_MY | color_order;
      break;
    case 1:
      madctl = ST77XX_MADCTL_MY | ST77XX_MADCTL_MV | color_order;
      break;
    case 2:
      madctl = color_order;
      break;
    default:
      madctl = ST77XX_MADCTL_MX | ST77XX_MADCTL_MV | color_order;
      break;
    }

    const bool swapped = (dev->rotation & 1u) != 0u;
    dev->width = swapped ? dev->window_height : dev->window_width;
    dev->height = swapped ? dev->window_width : dev->window_height;
    if (swapped) {
      dev->y_start = dev->col_start;
      dev->x_start = dev->row_start;
    } else {
      dev->x_start = dev->col_start;
      dev->y_start = dev->row_start;
    }
  } else if (dev->config.chip == JH_ST77XX_CHIP_ST7789) {
    switch (dev->rotation) {
    case 0:
      madctl = ST77XX_MADCTL_MX | ST77XX_MADCTL_MY | ST77XX_MADCTL_RGB;
      dev->x_start = dev->col_start;
      dev->y_start = dev->row_start;
      dev->width = dev->window_width;
      dev->height = dev->window_height;
      break;
    case 1:
      madctl = ST77XX_MADCTL_MY | ST77XX_MADCTL_MV | ST77XX_MADCTL_RGB;
      dev->x_start = dev->row_start;
      dev->y_start = dev->col_start2;
      dev->width = dev->window_height;
      dev->height = dev->window_width;
      break;
    case 2:
      madctl = ST77XX_MADCTL_RGB;
      dev->x_start = dev->col_start2;
      dev->y_start = dev->row_start2;
      dev->width = dev->window_width;
      dev->height = dev->window_height;
      break;
    default:
      madctl = ST77XX_MADCTL_MX | ST77XX_MADCTL_MV | ST77XX_MADCTL_RGB;
      dev->x_start = dev->row_start2;
      dev->y_start = dev->col_start;
      dev->width = dev->window_height;
      dev->height = dev->window_width;
      break;
    }
  } else {
    switch (dev->rotation) {
    case 0:
      madctl = ST77XX_MADCTL_MX | ST77XX_MADCTL_RGB | dev->color_order;
      dev->x_start = dev->col_start;
      dev->y_start = dev->row_start;
      dev->width = dev->window_width;
      dev->height = dev->window_height;
      break;
    case 1:
      madctl = ST77XX_MADCTL_MV | ST77XX_MADCTL_RGB | dev->color_order;
      dev->x_start = dev->row_start;
      dev->y_start = dev->col_start;
      dev->width = dev->window_height;
      dev->height = dev->window_width;
      break;
    case 2:
      madctl = ST77XX_MADCTL_MY | ST77XX_MADCTL_RGB | dev->color_order;
      dev->x_start = dev->col_start;
      dev->y_start = dev->row_start;
      dev->width = dev->window_width;
      dev->height = dev->window_height;
      break;
    default:
      madctl = ST77XX_MADCTL_MY | ST77XX_MADCTL_MX | ST77XX_MADCTL_MV |
               ST77XX_MADCTL_RGB | dev->color_order;
      dev->x_start = dev->row_start;
      dev->y_start = dev->col_start;
      dev->width = dev->window_height;
      dev->height = dev->window_width;
      break;
    }
  }

  return write_command(dev, ST77XX_MADCTL, &madctl, 1u);
}

bool jh_st77xx_invert(jh_st77xx_t *dev, bool invert) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  dev->inverted = invert;
  return write_command(
      dev, invert ? dev->invert_on_command : dev->invert_off_command, NULL, 0u);
}

static void put_u16_be(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
}

bool jh_st77xx_set_addr_window(jh_st77xx_t *dev, uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h) {
  if (dev == NULL || !dev->initialized || w == 0u || h == 0u) {
    return false;
  }

  x = (uint16_t)(x + dev->x_start);
  y = (uint16_t)(y + dev->y_start);
  const uint16_t x2 = (uint16_t)(x + w - 1u);
  const uint16_t y2 = (uint16_t)(y + h - 1u);
  uint8_t data[4];

  put_u16_be(&data[0], x);
  put_u16_be(&data[2], x2);
  if (!write_command(dev, ST77XX_CASET, data, sizeof(data))) {
    return false;
  }

  put_u16_be(&data[0], y);
  put_u16_be(&data[2], y2);
  if (!write_command(dev, ST77XX_RASET, data, sizeof(data))) {
    return false;
  }

  return write_command(dev, ST77XX_RAMWR, NULL, 0u);
}

bool jh_st77xx_write_pixels(jh_st77xx_t *dev, const uint16_t *pixels,
                            size_t count) {
  if (dev == NULL || !dev->initialized || pixels == NULL) {
    return false;
  }
  if (count == 0u) {
    return true;
  }

  const uint8_t bus = dev->config.bus;
  const hal_spi_settings_t settings = spi_settings_for(dev);
  uint8_t chunk[64];

  hal_spi_lock(bus);
  hal_spi_begin_transaction(bus, &settings);
  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), false);
  }
  hal_gpio_write(pin_to_u8(dev->config.dc_pin), true);

  while (count > 0u) {
    const size_t pixel_count =
        (count < (sizeof(chunk) / 2u)) ? count : (sizeof(chunk) / 2u);
    for (size_t i = 0u; i < pixel_count; ++i) {
      put_u16_be(&chunk[i * 2u], pixels[i]);
    }
    hal_spi_write(bus, chunk, pixel_count * 2u);
    pixels += pixel_count;
    count -= pixel_count;
  }

  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), true);
  }
  hal_spi_end_transaction(bus);
  hal_spi_unlock(bus);
  return true;
}

bool jh_st77xx_begin_write(jh_st77xx_t *dev, uint16_t x, uint16_t y, uint16_t w,
                           uint16_t h) {
  if (dev == NULL || !dev->initialized || w == 0u || h == 0u ||
      dev->write_active) {
    return false;
  }
  if (!jh_st77xx_set_addr_window(dev, x, y, w, h)) {
    return false;
  }

  const uint8_t bus = dev->config.bus;
  const hal_spi_settings_t settings = spi_settings_for(dev);

  hal_spi_lock(bus);
  hal_spi_begin_transaction(bus, &settings);
  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), false);
  }
  hal_gpio_write(pin_to_u8(dev->config.dc_pin), true);
  dev->write_active = true;
  return true;
}

bool jh_st77xx_write_pixels_be(jh_st77xx_t *dev, const uint8_t *pixels_be,
                               size_t byte_count) {
  if (dev == NULL || !dev->initialized || !dev->write_active ||
      (pixels_be == NULL && byte_count > 0u) || (byte_count & 1u) != 0u) {
    return false;
  }
  if (byte_count == 0u) {
    return true;
  }

  hal_spi_write(dev->config.bus, pixels_be, byte_count);
  return true;
}

bool jh_st77xx_write_pixels_dma(jh_st77xx_t *dev, const uint8_t *pixels_be,
                                size_t byte_count) {
  if (dev == NULL || !dev->initialized || !dev->write_active ||
      (pixels_be == NULL && byte_count > 0u) || (byte_count & 1u) != 0u) {
    return false;
  }
  if (byte_count == 0u) {
    return true;
  }

  return hal_spi_write_dma(dev->config.bus, pixels_be, byte_count);
}

bool jh_st77xx_write_pixels_fast(jh_st77xx_t *dev, const uint16_t *pixels,
                                 size_t count) {
  if (dev == NULL || !dev->initialized || !dev->write_active ||
      (pixels == NULL && count > 0u)) {
    return false;
  }
  if (count == 0u) {
    return true;
  }

  uint8_t chunk[ST77XX_PIXEL_CHUNK_BYTES];
  while (count > 0u) {
    const size_t pixel_count =
        (count < (sizeof(chunk) / 2u)) ? count : (sizeof(chunk) / 2u);
    for (size_t i = 0u; i < pixel_count; ++i) {
      put_u16_be(&chunk[i * 2u], pixels[i]);
    }
    if (!jh_st77xx_write_pixels_be(dev, chunk, pixel_count * 2u)) {
      return false;
    }
    pixels += pixel_count;
    count -= pixel_count;
  }
  return true;
}

bool jh_st77xx_end_write(jh_st77xx_t *dev) {
  if (dev == NULL || !dev->initialized || !dev->write_active) {
    return false;
  }

  const uint8_t bus = dev->config.bus;
  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), true);
  }
  hal_spi_end_transaction(bus);
  hal_spi_unlock(bus);
  dev->write_active = false;
  return true;
}

bool jh_st77xx_fill_rect(jh_st77xx_t *dev, uint16_t x, uint16_t y, uint16_t w,
                         uint16_t h, uint16_t color) {
  if (dev == NULL || !dev->initialized || w == 0u || h == 0u) {
    return false;
  }
  if (!jh_st77xx_set_addr_window(dev, x, y, w, h)) {
    return false;
  }

  const uint8_t bus = dev->config.bus;
  const hal_spi_settings_t settings = spi_settings_for(dev);
  uint8_t chunk[64];
  for (size_t i = 0u; i < sizeof(chunk); i += 2u) {
    put_u16_be(&chunk[i], color);
  }

  size_t remaining = (size_t)w * (size_t)h;
  hal_spi_lock(bus);
  hal_spi_begin_transaction(bus, &settings);
  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), false);
  }
  hal_gpio_write(pin_to_u8(dev->config.dc_pin), true);

  while (remaining > 0u) {
    const size_t pixels =
        remaining < (sizeof(chunk) / 2u) ? remaining : (sizeof(chunk) / 2u);
    hal_spi_write(bus, chunk, pixels * 2u);
    remaining -= pixels;
  }

  if (pin_is_connected(dev->config.cs_pin)) {
    hal_gpio_write(pin_to_u8(dev->config.cs_pin), true);
  }
  hal_spi_end_transaction(bus);
  hal_spi_unlock(bus);
  return true;
}

bool jh_st77xx_draw_rgb_bitmap(jh_st77xx_t *dev, uint16_t x, uint16_t y,
                               const uint16_t *pixels, uint16_t w, uint16_t h) {
  if (dev == NULL || pixels == NULL || w == 0u || h == 0u) {
    return false;
  }
  if (!jh_st77xx_begin_write(dev, x, y, w, h)) {
    return false;
  }
  const bool ok =
      jh_st77xx_write_pixels_fast(dev, pixels, (size_t)w * (size_t)h);
  return jh_st77xx_end_write(dev) && ok;
}

#endif /* HAL_ENABLE_DISPLAY && HAL_ENABLE_TFT && any ST77xx backend enabled   \
        */
