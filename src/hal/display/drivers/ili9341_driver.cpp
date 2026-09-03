#include "ili9341_driver.h"
#include "display_spi_transport.h"

#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_DISPLAY) && defined(HAL_ENABLE_TFT) &&                  \
    defined(HAL_ENABLE_ILI9341)

#include "hal/gpio/hal_gpio.h"
#include "hal/spi/hal_spi.h"
#include "hal/spi/hal_spi_device.h"
#include "hal/system/hal_system.h"

#include <string.h>

#define ILI9341_SWRESET 0x01u
#define ILI9341_SLPOUT 0x11u
#define ILI9341_INVOFF 0x20u
#define ILI9341_INVON 0x21u
#define ILI9341_DISPON 0x29u
#define ILI9341_CASET 0x2Au
#define ILI9341_PASET 0x2Bu
#define ILI9341_RAMWR 0x2Cu
#define ILI9341_MADCTL 0x36u
#define ILI9341_PIXFMT 0x3Au
#define ILI9341_FRMCTR1 0xB1u
#define ILI9341_DFUNCTR 0xB6u
#define ILI9341_PWCTR1 0xC0u
#define ILI9341_PWCTR2 0xC1u
#define ILI9341_VMCTR1 0xC5u
#define ILI9341_VMCTR2 0xC7u
#define ILI9341_GAMMASET 0x26u
#define ILI9341_GMCTRP1 0xE0u
#define ILI9341_GMCTRN1 0xE1u
#define ILI9341_VSCRSADD 0x37u

#define MADCTL_MY 0x80u
#define MADCTL_MX 0x40u
#define MADCTL_MV 0x20u
#define MADCTL_BGR 0x08u

#define ILI9341_PIXEL_CHUNK_BYTES 1024u

static const uint8_t s_initcmd[] = {0xEFu,
                                    3u,
                                    0x03u,
                                    0x80u,
                                    0x02u,
                                    0xCFu,
                                    3u,
                                    0x00u,
                                    0xC1u,
                                    0x30u,
                                    0xEDu,
                                    4u,
                                    0x64u,
                                    0x03u,
                                    0x12u,
                                    0x81u,
                                    0xE8u,
                                    3u,
                                    0x85u,
                                    0x00u,
                                    0x78u,
                                    0xCBu,
                                    5u,
                                    0x39u,
                                    0x2Cu,
                                    0x00u,
                                    0x34u,
                                    0x02u,
                                    0xF7u,
                                    1u,
                                    0x20u,
                                    0xEAu,
                                    2u,
                                    0x00u,
                                    0x00u,
                                    ILI9341_PWCTR1,
                                    1u,
                                    0x23u,
                                    ILI9341_PWCTR2,
                                    1u,
                                    0x10u,
                                    ILI9341_VMCTR1,
                                    2u,
                                    0x3Eu,
                                    0x28u,
                                    ILI9341_VMCTR2,
                                    1u,
                                    0x86u,
                                    ILI9341_MADCTL,
                                    1u,
                                    0x48u,
                                    ILI9341_VSCRSADD,
                                    1u,
                                    0x00u,
                                    ILI9341_PIXFMT,
                                    1u,
                                    0x55u,
                                    ILI9341_FRMCTR1,
                                    2u,
                                    0x00u,
                                    0x18u,
                                    ILI9341_DFUNCTR,
                                    3u,
                                    0x08u,
                                    0x82u,
                                    0x27u,
                                    0xF2u,
                                    1u,
                                    0x00u,
                                    ILI9341_GAMMASET,
                                    1u,
                                    0x01u,
                                    ILI9341_GMCTRP1,
                                    15u,
                                    0x0Fu,
                                    0x31u,
                                    0x2Bu,
                                    0x0Cu,
                                    0x0Eu,
                                    0x08u,
                                    0x4Eu,
                                    0xF1u,
                                    0x37u,
                                    0x07u,
                                    0x10u,
                                    0x03u,
                                    0x0Eu,
                                    0x09u,
                                    0x00u,
                                    ILI9341_GMCTRN1,
                                    15u,
                                    0x00u,
                                    0x0Eu,
                                    0x14u,
                                    0x03u,
                                    0x11u,
                                    0x07u,
                                    0x31u,
                                    0xC1u,
                                    0x48u,
                                    0x08u,
                                    0x0Fu,
                                    0x0Cu,
                                    0x31u,
                                    0x36u,
                                    0x0Fu,
                                    ILI9341_SLPOUT,
                                    0x80u,
                                    ILI9341_DISPON,
                                    0x80u,
                                    0x00u};

static uint32_t normalized_clock(const jh_ili9341_config_t *config) {
  return (config != NULL && config->clock_hz != 0u) ? config->clock_hz
                                                    : JH_ILI9341_SPI_DEFAULT_HZ;
}

bool jh_ili9341_run_init_sequence(const jh_ili9341_command_io_t *io,
                                  uint32_t delay_ms) {
  if (io == NULL || io->write_command == NULL) {
    return false;
  }

  const uint8_t *addr = s_initcmd;
  uint8_t cmd = 0u;
  while ((cmd = *addr++) != 0u) {
    const uint8_t x = *addr++;
    const uint8_t num_args = x & 0x7Fu;
    if (!io->write_command(io->ctx, cmd, addr, num_args)) {
      return false;
    }
    addr += num_args;
    if ((x & 0x80u) != 0u && io->delay_ms != NULL) {
      io->delay_ms(io->ctx, delay_ms);
    }
  }

  return true;
}

static void hal_delay_adapter(void *ctx, uint32_t delay_ms) {
  (void)ctx;
  hal_delay_ms(delay_ms);
}

static bool hal_write_command(void *ctx, uint8_t command, const uint8_t *data,
                              uint8_t data_len) {
  jh_ili9341_t *dev = (jh_ili9341_t *)ctx;
  return dev != NULL &&
         jh_display_spi_write_command(&dev->spi_device, dev->config.dc_pin,
                                      command, data, data_len);
}

static bool write_command(jh_ili9341_t *dev, uint8_t command,
                          const uint8_t *data, uint8_t data_len) {
  return hal_write_command(dev, command, data, data_len);
}

bool jh_ili9341_init(jh_ili9341_t *dev, const jh_ili9341_config_t *config) {
  if (dev == NULL || config == NULL ||
      !jh_display_pin_connected(config->dc_pin)) {
    return false;
  }

  memset(dev, 0, sizeof(*dev));
  dev->config = *config;
  dev->config.clock_hz = normalized_clock(config);

  const hal_spi_settings_t settings = {dev->config.clock_hz, HAL_SPI_MSBFIRST,
                                       HAL_SPI_MODE0};
  if (!jh_display_spi_setup(&dev->spi_device, dev->config.bus,
                            dev->config.cs_pin, dev->config.dc_pin,
                            dev->config.rst_pin, &settings, 100u, 200u)) {
    return false;
  }
  if (!jh_display_pin_connected(dev->config.rst_pin)) {
    if (!write_command(dev, ILI9341_SWRESET, NULL, 0u)) {
      return false;
    }
    hal_delay_ms(150u);
  }

  dev->initialized = true;
  dev->width = JH_ILI9341_TFTWIDTH;
  dev->height = JH_ILI9341_TFTHEIGHT;
  return jh_ili9341_soft_init(dev, 150u);
}

bool jh_ili9341_soft_init(jh_ili9341_t *dev, uint32_t delay_ms) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }

  const jh_ili9341_command_io_t io = {dev, hal_write_command,
                                      hal_delay_adapter};
  return jh_ili9341_run_init_sequence(&io, delay_ms);
}

bool jh_ili9341_set_rotation(jh_ili9341_t *dev, uint8_t rotation) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }

  uint8_t madctl = MADCTL_BGR;
  dev->rotation = rotation & 0x03u;
  switch (dev->rotation) {
  case 0:
    madctl = MADCTL_MX | MADCTL_BGR;
    dev->width = JH_ILI9341_TFTWIDTH;
    dev->height = JH_ILI9341_TFTHEIGHT;
    break;
  case 1:
    madctl = MADCTL_MV | MADCTL_BGR;
    dev->width = JH_ILI9341_TFTHEIGHT;
    dev->height = JH_ILI9341_TFTWIDTH;
    break;
  case 2:
    madctl = MADCTL_MY | MADCTL_BGR;
    dev->width = JH_ILI9341_TFTWIDTH;
    dev->height = JH_ILI9341_TFTHEIGHT;
    break;
  default:
    madctl = MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR;
    dev->width = JH_ILI9341_TFTHEIGHT;
    dev->height = JH_ILI9341_TFTWIDTH;
    break;
  }

  return write_command(dev, ILI9341_MADCTL, &madctl, 1u);
}

bool jh_ili9341_invert(jh_ili9341_t *dev, bool invert) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  dev->inverted = invert;
  return write_command(dev, invert ? ILI9341_INVON : ILI9341_INVOFF, NULL, 0u);
}

static bool abort_write(jh_ili9341_t *dev, hal_status_t status) {
  return jh_display_spi_abort_write(dev != NULL ? &dev->spi_device : NULL,
                                    dev != NULL ? &dev->write_active : NULL,
                                    status);
}

bool jh_ili9341_set_addr_window(jh_ili9341_t *dev, uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h) {
  if (dev == NULL || !dev->initialized || w == 0u || h == 0u) {
    return false;
  }

  return jh_display_set_addr_window(dev, hal_write_command, ILI9341_CASET,
                                    ILI9341_PASET, ILI9341_RAMWR, x, y, w, h);
}

bool jh_ili9341_write_pixels(jh_ili9341_t *dev, const uint16_t *pixels,
                             size_t count) {
  return dev != NULL && dev->initialized &&
         jh_display_spi_write_pixels(&dev->spi_device, dev->config.dc_pin,
                                     pixels, count);
}

bool jh_ili9341_begin_write(jh_ili9341_t *dev, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h) {
  if (dev == NULL || !dev->initialized || w == 0u || h == 0u ||
      dev->write_active) {
    return false;
  }
  if (!jh_ili9341_set_addr_window(dev, x, y, w, h)) {
    return false;
  }

  if (hal_status_is_error(hal_spi_device_acquire(&dev->spi_device))) {
    return false;
  }
  hal_gpio_write(jh_display_pin_u8(dev->config.dc_pin), true);
  dev->write_active = true;
  return true;
}

bool jh_ili9341_write_pixels_be(jh_ili9341_t *dev, const uint8_t *pixels_be,
                                size_t byte_count) {
  return jh_display_spi_stream_write(dev != NULL ? &dev->spi_device : NULL,
                                     dev != NULL && dev->initialized,
                                     dev != NULL ? &dev->write_active : NULL,
                                     pixels_be, byte_count, false, false);
}

bool jh_ili9341_write_pixels_dma(jh_ili9341_t *dev, const uint8_t *pixels_be,
                                 size_t byte_count) {
  return jh_display_spi_stream_write(dev != NULL ? &dev->spi_device : NULL,
                                     dev != NULL && dev->initialized,
                                     dev != NULL ? &dev->write_active : NULL,
                                     pixels_be, byte_count, true, false);
}

bool jh_ili9341_write_pixels_dma_async_start(jh_ili9341_t *dev,
                                             const uint8_t *pixels_be,
                                             size_t byte_count) {
  return jh_display_spi_stream_write(dev != NULL ? &dev->spi_device : NULL,
                                     dev != NULL && dev->initialized,
                                     dev != NULL ? &dev->write_active : NULL,
                                     pixels_be, byte_count, true, true);
}

bool jh_ili9341_write_pixels_dma_async_busy(jh_ili9341_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  return hal_spi_write_dma_async_busy(dev->spi_device.bus);
}

bool jh_ili9341_write_pixels_dma_async_wait(jh_ili9341_t *dev) {
  if (dev == NULL || !dev->initialized) {
    return false;
  }
  if (!hal_spi_write_dma_async_wait(dev->spi_device.bus)) {
    return abort_write(dev, HAL_EIO);
  }
  return true;
}

bool jh_ili9341_write_pixels_fast(jh_ili9341_t *dev, const uint16_t *pixels,
                                  size_t count) {
  if (dev == NULL || !dev->initialized || !dev->write_active ||
      (pixels == NULL && count > 0u)) {
    return abort_write(dev, HAL_EINVAL);
  }
  if (count == 0u) {
    return true;
  }

  uint8_t chunk[ILI9341_PIXEL_CHUNK_BYTES];
  while (count > 0u) {
    const size_t pixel_count =
        (count < (COUNTOF(chunk) / 2u)) ? count : (COUNTOF(chunk) / 2u);
    for (size_t i = 0u; i < pixel_count; ++i) {
      jh_display_put_u16_be(&chunk[i * 2u], pixels[i]);
    }
    if (!jh_ili9341_write_pixels_dma(dev, chunk, pixel_count * 2u)) {
      return false;
    }
    pixels += pixel_count;
    count -= pixel_count;
  }
  return true;
}

bool jh_ili9341_end_write(jh_ili9341_t *dev) {
  if (dev == NULL || !dev->initialized || !dev->write_active) {
    return false;
  }

  dev->write_active = false;
  return hal_status_is_ok(hal_spi_device_release(&dev->spi_device));
}

bool jh_ili9341_fill_rect(jh_ili9341_t *dev, uint16_t x, uint16_t y, uint16_t w,
                          uint16_t h, uint16_t color) {
  if (dev == NULL || !dev->initialized || w == 0u || h == 0u) {
    return false;
  }
  if (!jh_ili9341_begin_write(dev, x, y, w, h)) {
    return false;
  }

  const bool ok =
      jh_display_spi_fill_color(&dev->spi_device, color, (size_t)w * (size_t)h);
  return jh_ili9341_end_write(dev) && ok;
}

bool jh_ili9341_draw_rgb_bitmap(jh_ili9341_t *dev, uint16_t x, uint16_t y,
                                const uint16_t *pixels, uint16_t w,
                                uint16_t h) {
  if (dev == NULL || pixels == NULL || w == 0u || h == 0u) {
    return false;
  }
  if (!jh_ili9341_begin_write(dev, x, y, w, h)) {
    return false;
  }
  const bool ok =
      jh_ili9341_write_pixels_fast(dev, pixels, (size_t)w * (size_t)h);
  return jh_ili9341_end_write(dev) && ok;
}

#endif /* HAL_ENABLE_DISPLAY && HAL_ENABLE_TFT && HAL_ENABLE_ILI9341 */
