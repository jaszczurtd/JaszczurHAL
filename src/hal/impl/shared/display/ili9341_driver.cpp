#include "ili9341_driver.h"

#include "../../../hal_config.h"

#if defined(HAL_ENABLE_DISPLAY) && defined(HAL_ENABLE_TFT) && defined(HAL_ENABLE_ILI9341)

#include "../../../hal_gpio.h"
#include "../../../hal_spi.h"
#include "../../../hal_system.h"

#include <string.h>

#define ILI9341_SWRESET 0x01u
#define ILI9341_SLPOUT  0x11u
#define ILI9341_INVOFF  0x20u
#define ILI9341_INVON   0x21u
#define ILI9341_DISPON  0x29u
#define ILI9341_CASET   0x2Au
#define ILI9341_PASET   0x2Bu
#define ILI9341_RAMWR   0x2Cu
#define ILI9341_MADCTL  0x36u
#define ILI9341_PIXFMT  0x3Au
#define ILI9341_FRMCTR1 0xB1u
#define ILI9341_DFUNCTR 0xB6u
#define ILI9341_PWCTR1  0xC0u
#define ILI9341_PWCTR2  0xC1u
#define ILI9341_VMCTR1  0xC5u
#define ILI9341_VMCTR2  0xC7u
#define ILI9341_GAMMASET 0x26u
#define ILI9341_GMCTRP1  0xE0u
#define ILI9341_GMCTRN1  0xE1u
#define ILI9341_VSCRSADD 0x37u

#define MADCTL_MY  0x80u
#define MADCTL_MX  0x40u
#define MADCTL_MV  0x20u
#define MADCTL_BGR 0x08u

static const uint8_t s_initcmd[] = {
    0xEFu, 3u, 0x03u, 0x80u, 0x02u,
    0xCFu, 3u, 0x00u, 0xC1u, 0x30u,
    0xEDu, 4u, 0x64u, 0x03u, 0x12u, 0x81u,
    0xE8u, 3u, 0x85u, 0x00u, 0x78u,
    0xCBu, 5u, 0x39u, 0x2Cu, 0x00u, 0x34u, 0x02u,
    0xF7u, 1u, 0x20u,
    0xEAu, 2u, 0x00u, 0x00u,
    ILI9341_PWCTR1,   1u, 0x23u,
    ILI9341_PWCTR2,   1u, 0x10u,
    ILI9341_VMCTR1,   2u, 0x3Eu, 0x28u,
    ILI9341_VMCTR2,   1u, 0x86u,
    ILI9341_MADCTL,   1u, 0x48u,
    ILI9341_VSCRSADD, 1u, 0x00u,
    ILI9341_PIXFMT,   1u, 0x55u,
    ILI9341_FRMCTR1,  2u, 0x00u, 0x18u,
    ILI9341_DFUNCTR,  3u, 0x08u, 0x82u, 0x27u,
    0xF2u,            1u, 0x00u,
    ILI9341_GAMMASET, 1u, 0x01u,
    ILI9341_GMCTRP1, 15u, 0x0Fu, 0x31u, 0x2Bu, 0x0Cu, 0x0Eu, 0x08u,
        0x4Eu, 0xF1u, 0x37u, 0x07u, 0x10u, 0x03u, 0x0Eu, 0x09u, 0x00u,
    ILI9341_GMCTRN1, 15u, 0x00u, 0x0Eu, 0x14u, 0x03u, 0x11u, 0x07u,
        0x31u, 0xC1u, 0x48u, 0x08u, 0x0Fu, 0x0Cu, 0x31u, 0x36u, 0x0Fu,
    ILI9341_SLPOUT, 0x80u,
    ILI9341_DISPON, 0x80u,
    0x00u
};

static bool pin_is_connected(int16_t pin) {
    return pin >= 0 && pin <= 255;
}

static uint8_t pin_to_u8(int16_t pin) {
    return (uint8_t)pin;
}

static uint32_t normalized_clock(const jh_ili9341_config_t *config) {
    return (config != NULL && config->clock_hz != 0u)
        ? config->clock_hz
        : JH_ILI9341_SPI_DEFAULT_HZ;
}

static hal_spi_settings_t spi_settings_for(const jh_ili9341_t *dev) {
    hal_spi_settings_t settings = {
        normalized_clock(&dev->config),
        HAL_SPI_MSBFIRST,
        HAL_SPI_MODE0
    };
    return settings;
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

static bool hal_write_command(void *ctx,
                              uint8_t command,
                              const uint8_t *data,
                              uint8_t data_len) {
    jh_ili9341_t *dev = (jh_ili9341_t *)ctx;
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

static bool write_command(jh_ili9341_t *dev,
                          uint8_t command,
                          const uint8_t *data,
                          uint8_t data_len) {
    return hal_write_command(dev, command, data, data_len);
}

bool jh_ili9341_init(jh_ili9341_t *dev, const jh_ili9341_config_t *config) {
    if (dev == NULL || config == NULL || !pin_is_connected(config->dc_pin)) {
        return false;
    }

    memset(dev, 0, sizeof(*dev));
    dev->config = *config;
    dev->config.clock_hz = normalized_clock(config);

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
    } else {
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

    const jh_ili9341_command_io_t io = {
        dev,
        hal_write_command,
        hal_delay_adapter
    };
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

static void put_u16_be(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

bool jh_ili9341_set_addr_window(jh_ili9341_t *dev,
                                uint16_t x,
                                uint16_t y,
                                uint16_t w,
                                uint16_t h) {
    if (dev == NULL || !dev->initialized || w == 0u || h == 0u) {
        return false;
    }

    const uint16_t x2 = (uint16_t)(x + w - 1u);
    const uint16_t y2 = (uint16_t)(y + h - 1u);
    uint8_t data[4];

    put_u16_be(&data[0], x);
    put_u16_be(&data[2], x2);
    if (!write_command(dev, ILI9341_CASET, data, sizeof(data))) {
        return false;
    }

    put_u16_be(&data[0], y);
    put_u16_be(&data[2], y2);
    if (!write_command(dev, ILI9341_PASET, data, sizeof(data))) {
        return false;
    }

    return write_command(dev, ILI9341_RAMWR, NULL, 0u);
}

bool jh_ili9341_write_pixels(jh_ili9341_t *dev,
                             const uint16_t *pixels,
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
        const size_t pixel_count = (count < (sizeof(chunk) / 2u))
            ? count
            : (sizeof(chunk) / 2u);
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

bool jh_ili9341_fill_rect(jh_ili9341_t *dev,
                          uint16_t x,
                          uint16_t y,
                          uint16_t w,
                          uint16_t h,
                          uint16_t color) {
    if (dev == NULL || !dev->initialized || w == 0u || h == 0u) {
        return false;
    }
    if (!jh_ili9341_set_addr_window(dev, x, y, w, h)) {
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
        const size_t pixels = remaining < (sizeof(chunk) / 2u)
            ? remaining
            : (sizeof(chunk) / 2u);
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

bool jh_ili9341_draw_rgb_bitmap(jh_ili9341_t *dev,
                                uint16_t x,
                                uint16_t y,
                                const uint16_t *pixels,
                                uint16_t w,
                                uint16_t h) {
    if (dev == NULL || pixels == NULL || w == 0u || h == 0u) {
        return false;
    }
    return jh_ili9341_set_addr_window(dev, x, y, w, h) &&
           jh_ili9341_write_pixels(dev, pixels, (size_t)w * (size_t)h);
}

#endif /* HAL_ENABLE_DISPLAY && HAL_ENABLE_TFT && HAL_ENABLE_ILI9341 */
