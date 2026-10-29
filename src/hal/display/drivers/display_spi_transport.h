#ifndef JH_DISPLAY_SPI_TRANSPORT_H
#define JH_DISPLAY_SPI_TRANSPORT_H

#include "hal/core/hal_config.h"
#include "hal/core/hal_layout_check.h"
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

static inline hal_status_t jh_display_spi_command_held(hal_spi_device_t *device,
                                                       int16_t dc_pin,
                                                       uint8_t command,
                                                       const uint8_t *data,
                                                       uint8_t data_len) {
  hal_gpio_write(jh_display_pin_u8(dc_pin), false);
  hal_status_t status = hal_spi_write(device->bus, &command, 1u);
  if (hal_status_is_ok(status)) {
    hal_gpio_write(jh_display_pin_u8(dc_pin), true);
    if (data != NULL && data_len > 0u) {
      status = hal_spi_write(device->bus, data, data_len);
    }
  }
  return status;
}

static inline bool jh_display_spi_write_command(hal_spi_device_t *device,
                                                int16_t dc_pin, uint8_t command,
                                                const uint8_t *data,
                                                uint8_t data_len) {
  if (device == NULL || !jh_display_pin_connected(dc_pin)) {
    return false;
  }
  const hal_status_t status = hal_spi_device_acquire(device);
  if (hal_status_is_error(status)) {
    return false;
  }
  return hal_status_is_ok(hal_spi_device_finish(
      device,
      jh_display_spi_command_held(device, dc_pin, command, data, data_len)));
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

/** @brief Owned SPI device and DC pin for one address-window setup. */
typedef struct {
  hal_spi_device_t *device; /**< Acquired display device. */
  int16_t dc_pin;           /**< Connected command/data GPIO. */
} jh_display_spi_window_t;

static inline bool jh_display_spi_window_command(void *ctx, uint8_t command,
                                                 const uint8_t *data,
                                                 uint8_t data_len) {
  jh_display_spi_window_t *window = (jh_display_spi_window_t *)ctx;
  return hal_status_is_ok(jh_display_spi_command_held(
      window->device, window->dc_pin, command, data, data_len));
}

/** @brief Acquire SPI and send the address window under the same lease as
 * its following pixels. A successful caller must finish the device. */
static inline bool
jh_display_spi_begin_window(hal_spi_device_t *device, int16_t dc_pin,
                            uint8_t column_command, uint8_t row_command,
                            uint8_t write_command, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h) {
  if (device == NULL || !jh_display_pin_connected(dc_pin) ||
      hal_status_is_error(hal_spi_device_acquire(device))) {
    return false;
  }
  jh_display_spi_window_t window = {device, dc_pin};
  if (!jh_display_set_addr_window(&window, jh_display_spi_window_command,
                                  column_command, row_command, write_command, x,
                                  y, w, h)) {
    (void)hal_spi_device_finish(device, HAL_EIO);
    return false;
  }
  hal_gpio_write(jh_display_pin_u8(dc_pin), true);
  return true;
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

/** @brief Driver operations used by the banded fill and bitmap helpers. */
typedef struct {
  void *dev;                    /**< Driver instance passed to each call. */
  hal_spi_device_t *spi_device; /**< SPI device of that instance. */
  bool (*begin_write)(void *dev, uint16_t x, uint16_t y, uint16_t w,
                      uint16_t h); /**< Set the window, acquire the device. */
  bool (*end_write)(void *dev);    /**< Release the device. */
  bool (*write_pixels)(void *dev, const uint16_t *pixels,
                       size_t count); /**< Native RGB565 words. */
} jh_display_spi_writer_t;

/**
 * @brief Rows of a @p width wide RGB565 area sent in one transaction.
 *
 * At least one row; write_area() splits rows wider than the byte limit.
 * With the default 0 the whole @p height goes at once.
 */
static inline uint32_t jh_display_spi_band_rows(uint16_t width,
                                                uint16_t height) {
#if HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES > 0u
  (void)height;
  const uint32_t rows =
      (uint32_t)(HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES / ((uint32_t)width * 2u));
  return (rows == 0u) ? 1u : rows;
#else
  (void)width;
  return height;
#endif
}

/**
 * @brief Fill or draw an area in bounded transactions. Split wide rows too,
 *        so other devices on the bus can run within the configured byte limit.
 * @param writer Driver operations.
 * @param pixels Native RGB565 bitmap of @p w x @p h, or NULL to fill.
 * @param color Fill color when @p pixels is NULL.
 * @return true when every band was sent; stops at the first failed band.
 */
static inline bool
jh_display_spi_write_area(const jh_display_spi_writer_t *writer, uint16_t x,
                          uint16_t y, uint16_t w, uint16_t h,
                          const uint16_t *pixels, uint16_t color) {
  if (writer == NULL || w == 0u || h == 0u) {
    return false;
  }
  const uint32_t band = jh_display_spi_band_rows(w, h);
#if HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES > 0u
  const uint32_t max_width = HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES / 2u;
#else
  const uint32_t max_width = w;
#endif
  for (uint32_t row = 0u; row < h; row += band) {
    const uint32_t left = (uint32_t)h - row;
    const uint16_t rows = (uint16_t)(left < band ? left : band);
    // A partial-width tile has one row, so bitmap data stays contiguous.
    for (uint32_t column = 0u; column < w; column += max_width) {
      const uint32_t remaining = (uint32_t)w - column;
      const uint16_t width =
          (uint16_t)(remaining < max_width ? remaining : max_width);
      if (!writer->begin_write(writer->dev, (uint16_t)(x + column),
                               (uint16_t)(y + row), width, rows)) {
        return false;
      }
      const size_t count = (size_t)width * (size_t)rows;
      const bool ok =
          (pixels == NULL)
              ? jh_display_spi_fill_color(writer->spi_device, color, count)
              : writer->write_pixels(writer->dev,
                                     &pixels[(size_t)row * w + column], count);
      if (!writer->end_write(writer->dev) || !ok) {
        return false;
      }
    }
  }
  return true;
}

#endif
