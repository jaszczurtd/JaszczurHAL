#ifndef JH_DISPLAY_SPI_TRANSPORT_H
#define JH_DISPLAY_SPI_TRANSPORT_H

#include "hal/core/jh_endian.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/spi/hal_spi.h"
#include "hal/spi/hal_spi_device.h"
#include "hal/system/hal_system.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Check whether a signed display pin value identifies a GPIO.
 * @param pin Signed pin value; negative values mean not connected.
 * @return true when @p pin fits in `uint8_t`.
 */
static inline bool jh_display_pin_connected(int16_t pin) {
  return pin >= 0 && pin <= UINT8_MAX;
}

/**
 * @brief Convert a previously validated display pin to the HAL GPIO type.
 * @param pin Value accepted by jh_display_pin_connected().
 * @return GPIO pin number.
 */
static inline uint8_t jh_display_pin_u8(int16_t pin) { return (uint8_t)pin; }

static inline bool jh_display_spi_setup(hal_spi_device_t *device, uint8_t bus,
                                        int16_t cs_pin, int16_t dc_pin,
                                        int16_t reset_pin,
                                        const hal_spi_settings_t *settings,
                                        uint32_t reset_delay_ms,
                                        uint32_t reset_release_delay_ms) {
  const uint8_t cs = jh_display_pin_connected(cs_pin)
                         ? jh_display_pin_u8(cs_pin)
                         : HAL_SPI_DEVICE_CS_NONE;
  if (hal_status_is_error(hal_spi_device_init(device, bus, cs, settings))) {
    return false;
  }
  hal_gpio_set_mode(jh_display_pin_u8(dc_pin), HAL_GPIO_OUTPUT);
  hal_gpio_write(jh_display_pin_u8(dc_pin), true);
  if (jh_display_pin_connected(reset_pin)) {
    const uint8_t reset = jh_display_pin_u8(reset_pin);
    hal_gpio_set_mode(reset, HAL_GPIO_OUTPUT);
    hal_gpio_write(reset, true);
    hal_delay_ms(reset_delay_ms);
    hal_gpio_write(reset, false);
    hal_delay_ms(reset_delay_ms);
    hal_gpio_write(reset, true);
    hal_delay_ms(reset_release_delay_ms);
  }
  return true;
}

static inline bool jh_display_spi_write_command(hal_spi_device_t *device,
                                                int16_t dc_pin, uint8_t command,
                                                const uint8_t *data,
                                                uint8_t data_len) {
  if (device == NULL || !jh_display_pin_connected(dc_pin)) {
    return false;
  }
  hal_status_t status = hal_spi_device_acquire(device);
  if (hal_status_is_error(status)) {
    return false;
  }
  hal_gpio_write(jh_display_pin_u8(dc_pin), false);
  status = hal_spi_write(device->bus, &command, 1u);
  if (hal_status_is_ok(status)) {
    hal_gpio_write(jh_display_pin_u8(dc_pin), true);
    if (data != NULL && data_len > 0u) {
      status = hal_spi_write(device->bus, data, data_len);
    }
  }
  return hal_status_is_ok(hal_spi_device_finish(device, status));
}

typedef bool (*jh_display_write_command_fn)(void *ctx, uint8_t command,
                                            const uint8_t *data,
                                            uint8_t data_len);

/**
 * @brief Store a 16-bit display value in big-endian byte order.
 * @param out Pointer to two writable bytes.
 * @param value Host-order value.
 */
static inline void jh_display_put_u16_be(uint8_t *out, uint16_t value) {
  jh_store_be16(out, value);
}

static inline bool
jh_display_set_addr_window(void *ctx, jh_display_write_command_fn write_command,
                           uint8_t column_command, uint8_t row_command,
                           uint8_t write_command_code, uint16_t x, uint16_t y,
                           uint16_t width, uint16_t height) {
  uint8_t data[4];
  jh_display_put_u16_be(&data[0], x);
  jh_display_put_u16_be(&data[2], (uint16_t)(x + width - 1u));
  if (!write_command(ctx, column_command, data, sizeof(data))) {
    return false;
  }
  jh_display_put_u16_be(&data[0], y);
  jh_display_put_u16_be(&data[2], (uint16_t)(y + height - 1u));
  return write_command(ctx, row_command, data, sizeof(data)) &&
         write_command(ctx, write_command_code, NULL, 0u);
}

static inline bool jh_display_spi_write_dma_or_fallback(uint8_t bus,
                                                        const uint8_t *data,
                                                        size_t len) {
  if (len == 0u) {
    return true;
  }
  if (data == NULL) {
    return false;
  }
  return hal_spi_write_dma(bus, data, len) ||
         hal_status_is_ok(hal_spi_write(bus, data, len));
}

static inline bool jh_display_spi_write_pixels(hal_spi_device_t *device,
                                               int16_t dc_pin,
                                               const uint16_t *pixels,
                                               size_t count) {
  if (device == NULL || pixels == NULL) {
    return false;
  }
  if (count == 0u) {
    return true;
  }
  uint8_t chunk[64];
  hal_status_t status = hal_spi_device_acquire(device);
  if (hal_status_is_error(status)) {
    return false;
  }
  hal_gpio_write(jh_display_pin_u8(dc_pin), true);
  while (count > 0u && hal_status_is_ok(status)) {
    const size_t pixel_count =
        count < COUNTOF(chunk) / 2u ? count : COUNTOF(chunk) / 2u;
    for (size_t i = 0u; i < pixel_count; ++i) {
      jh_display_put_u16_be(&chunk[i * 2u], pixels[i]);
    }
    status = hal_spi_write(device->bus, chunk, pixel_count * 2u);
    pixels += pixel_count;
    count -= pixel_count;
  }
  return hal_status_is_ok(hal_spi_device_finish(device, status));
}

static inline bool jh_display_spi_abort_write(hal_spi_device_t *device,
                                              bool *write_active,
                                              hal_status_t status) {
  if (device != NULL && write_active != NULL && *write_active) {
    *write_active = false;
    (void)hal_spi_device_finish(device, status);
  }
  return false;
}

static inline bool
jh_display_spi_stream_write(hal_spi_device_t *device, bool initialized,
                            bool *write_active, const uint8_t *pixels_be,
                            size_t byte_count, bool dma, bool async) {
  if (device == NULL || !initialized || write_active == NULL ||
      !*write_active || (pixels_be == NULL && byte_count > 0u) ||
      (byte_count & 1u) != 0u) {
    return jh_display_spi_abort_write(device, write_active, HAL_EINVAL);
  }
  if (byte_count == 0u) {
    return true;
  }
  if (async) {
    return hal_spi_write_dma_async_start(device->bus, pixels_be, byte_count)
               ? true
               : jh_display_spi_abort_write(device, write_active, HAL_EIO);
  }
  if (dma) {
    return jh_display_spi_write_dma_or_fallback(device->bus, pixels_be,
                                                byte_count)
               ? true
               : jh_display_spi_abort_write(device, write_active, HAL_EIO);
  }
  const hal_status_t status = hal_spi_write(device->bus, pixels_be, byte_count);
  return hal_status_is_error(status)
             ? jh_display_spi_abort_write(device, write_active, status)
             : true;
}

static inline bool jh_display_spi_fill_color(hal_spi_device_t *device,
                                             uint16_t color,
                                             size_t pixel_count) {
  uint8_t chunk[128];
  for (size_t i = 0u; i < sizeof(chunk); i += 2u) {
    jh_display_put_u16_be(&chunk[i], color);
  }
  while (pixel_count > 0u) {
    const size_t count =
        pixel_count < COUNTOF(chunk) / 2u ? pixel_count : COUNTOF(chunk) / 2u;
    if (!jh_display_spi_write_dma_or_fallback(device->bus, chunk, count * 2u)) {
      return false;
    }
    pixel_count -= count;
  }
  return true;
}

#endif
