#include "hal/core/hal_target.h"

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_DISPLAY

#include "hal/display/drivers/display_spi_transport.h"
#include "hal/display/hal_display.h"
#include "hal/display/hal_display_internal.h"
#include "hal/serial/hal_serial.h"

#include <stdarg.h>
#include <stdio.h>

hal_status_t jh_display_mono_text(int x, int y, const char *text,
                                  const hal_display_mono_font_t *font,
                                  uint8_t scale, uint16_t foreground,
                                  uint16_t background, int width, int height,
                                  jh_display_mono_page_fn write_page,
                                  void *context) {
  if (!text || !font || !font->data || font->first > font->last ||
      font->width == 0u || font->width > 16u || scale == 0u || scale > 8u ||
      font->data_size < (size_t)(font->last - font->first + 1u) * font->width ||
      x < 0 || x > width || y < 0 || y > height || 8 * scale > height - y) {
    return HAL_EINVAL;
  }
  for (const char *p = text; *p; ++p) {
    if (*p == '\r' || *p == '\n') {
      return HAL_EINVAL;
    }
  }
  const uint16_t cell_width = (uint16_t)(font->width * scale);
  uint8_t columns[128]; /* At most one enlarged glyph page, never a frame. */
  for (; *text && cell_width <= width - x; ++text, x += cell_width) {
    const uint8_t code = (uint8_t)*text;
    const uint8_t *glyph =
        code >= font->first && code <= font->last
            ? font->data + (size_t)(code - font->first) * font->width
            : nullptr;
    for (uint8_t page = 0u; page < scale; ++page) {
      for (uint16_t col = 0u; col < cell_width; ++col) {
        const uint8_t source = glyph ? glyph[col / scale] : 0u;
        uint8_t bits = 0u;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
          if (source & (1u << ((page * 8u + bit) / scale))) {
            bits |= (uint8_t)(1u << bit);
          }
        }
        columns[col] = bits;
      }
      const hal_status_t status =
          write_page(context, x, y + page * 8, columns, cell_width, foreground,
                     background);
      if (hal_status_is_error(status)) {
        return status;
      }
    }
  }
  return HAL_OK;
}

#ifdef HAL_ENABLE_SSD1306
hal_status_t jh_display_validate_oled_config(
    const hal_display_ssd1306_family_config_t *config) {
  if (!config || config->width <= 0 || config->width > INT16_MAX ||
      config->height <= 0 || config->height > INT16_MAX ||
      config->controller < HAL_DISPLAY_OLED_CONTROLLER_SSD1306 ||
      config->controller > HAL_DISPLAY_OLED_CONTROLLER_CH1115 ||
      config->bus_type < HAL_DISPLAY_OLED_BUS_I2C ||
      config->bus_type > HAL_DISPLAY_OLED_BUS_SPI ||
      config->orientation < HAL_DISPLAY_OLED_ORIENTATION_NATIVE ||
      config->orientation > HAL_DISPLAY_OLED_ORIENTATION_ROTATED_180 ||
      config->buffer_mode < HAL_DISPLAY_BUFFERED ||
      config->buffer_mode > HAL_DISPLAY_UNBUFFERED) {
    return HAL_EINVAL;
  }
  const uint8_t pump = config->panel.pump_voltage_command;
  if (config->override_panel && pump != 0u && (pump < 0x30u || pump > 0x33u)) {
    return HAL_EINVAL;
  }
  if (config->buffer_mode == HAL_DISPLAY_UNBUFFERED &&
      (config->height % 8 != 0 ||
       config->height / 8 + config->page_offset > 8 ||
       config->width + config->segment_offset +
               (config->width == 64 &&
                        config->controller < HAL_DISPLAY_OLED_CONTROLLER_SH1106
                    ? 32
                    : 0) >
           (config->controller >= HAL_DISPLAY_OLED_CONTROLLER_SH1106 ? 132
                                                                     : 128))) {
    return HAL_EINVAL;
  }
#ifndef HAL_ENABLE_SPI
  if (config->bus_type == HAL_DISPLAY_OLED_BUS_SPI) {
    return HAL_EUNSUPPORTED;
  }
#endif
  return HAL_OK;
}

bool hal_display_init_ssd1306_i2c(int width, int height, uint8_t i2c_addr,
                                  int8_t rst_pin, uint8_t switchvcc,
                                  bool periph_begin) {
  return hal_status_to_bool(hal_display_init_ssd1306_i2c_status_ex(
      width, height, 0u, i2c_addr, rst_pin, switchvcc, periph_begin));
}

bool hal_display_init_ssd1306_i2c_ex(int width, int height, uint8_t i2c_bus,
                                     uint8_t i2c_addr, int8_t rst_pin,
                                     uint8_t switchvcc, bool periph_begin) {
  return hal_status_to_bool(hal_display_init_ssd1306_i2c_status_ex(
      width, height, i2c_bus, i2c_addr, rst_pin, switchvcc, periph_begin));
}
#endif

#ifdef HAL_ENABLE_TFT
static bool ili9341_pin_valid(int16_t pin) {
  return pin == -1 || jh_display_pin_connected(pin);
}

hal_status_t
hal_display_init_ili9341_ex(const hal_display_ili9341_config_t *config) {
  if (config == nullptr || config->bus > 1u ||
      !jh_display_pin_connected(config->dc_pin) ||
      !ili9341_pin_valid(config->cs_pin) ||
      !ili9341_pin_valid(config->rst_pin)) {
    return HAL_EINVAL;
  }
  return jh_hal_display_init_ili9341(config);
}
#endif

bool hal_display_set_rotation(uint8_t rotation) {
  return hal_status_to_bool(hal_display_set_rotation_ex(rotation));
}

bool hal_display_invert(bool invert) {
  return hal_status_to_bool(hal_display_invert_ex(invert));
}

int hal_display_get_width(void) {
  int width = 0;
  jh_hal_display_get_dimensions(&width, nullptr);
  return width;
}

int hal_display_get_height(void) {
  int height = 0;
  jh_hal_display_get_dimensions(nullptr, &height);
  return height;
}

hal_status_t hal_display_get_width_ex(int *out_width) {
  if (out_width == nullptr) {
    return HAL_EINVAL;
  }
  jh_hal_display_get_dimensions(out_width, nullptr);
  return *out_width > 0 ? HAL_OK : HAL_EUNINIT;
}

hal_status_t hal_display_get_height_ex(int *out_height) {
  if (out_height == nullptr) {
    return HAL_EINVAL;
  }
  jh_hal_display_get_dimensions(nullptr, out_height);
  return *out_height > 0 ? HAL_OK : HAL_EUNINIT;
}

hal_status_t hal_display_draw_image_ex(int x, int y, int w, int h,
                                       uint16_t background, uint16_t *data) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t status = hal_display_fill_rect_ex(x, y, w, h, background);
  return hal_status_is_error(status)
             ? status
             : hal_display_draw_rgb_bitmap_ex(x, y, data, w, h);
}

bool hal_display_draw_image(int x, int y, int w, int h, uint16_t background,
                            uint16_t *data) {
  return hal_status_to_bool(
      hal_display_draw_image_ex(x, y, w, h, background, data));
}

hal_status_t hal_display_print_at_ex(int x, int y, const char *text) {
  if (text == nullptr) {
    hal_derr("hal_display_print_at: text pointer is NULL");
    return HAL_EINVAL;
  }
  const hal_status_t status = hal_display_set_cursor_ex(x, y);
  return hal_status_is_error(status) ? status : hal_display_print_ex(text);
}

bool hal_display_print_at(int x, int y, const char *text) {
  return hal_status_to_bool(hal_display_print_at_ex(x, y, text));
}

hal_status_t hal_display_clear_text_line_ex(int line_index, int line_height,
                                            uint16_t bg_color) {
  if (line_index < 0 || line_height <= 0) {
    hal_derr(
        "hal_display_clear_text_line: invalid line/index line=%d height=%d",
        line_index, line_height);
    return HAL_EINVAL;
  }
  const int width = hal_display_get_width();
  if (width <= 0) {
    hal_derr("hal_display_clear_text_line: display width is not configured");
    return HAL_EUNINIT;
  }
  return hal_display_fill_rect_ex(0, line_index * line_height, width,
                                  line_height, bg_color);
}

bool hal_display_clear_text_line(int line_index, int line_height,
                                 uint16_t bg_color) {
  return hal_status_to_bool(
      hal_display_clear_text_line_ex(line_index, line_height, bg_color));
}

hal_status_t hal_display_print_line_ex(int line_index, int line_height,
                                       const char *text, bool clear_first,
                                       uint16_t fg_color, uint16_t bg_color) {
  if (text == nullptr) {
    hal_derr("hal_display_print_line: text pointer is NULL");
    return HAL_EINVAL;
  }
  if (line_index < 0 || line_height <= 0) {
    hal_derr("hal_display_print_line: invalid line/index line=%d height=%d",
             line_index, line_height);
    return HAL_EINVAL;
  }
  hal_status_t status = HAL_OK;
  if (clear_first) {
    status = hal_display_clear_text_line_ex(line_index, line_height, bg_color);
  }
  if (hal_status_is_ok(status)) {
    status = hal_display_set_text_color_ex(fg_color);
  }
  return hal_status_is_error(status)
             ? status
             : hal_display_print_at_ex(0, line_index * line_height, text);
}

bool hal_display_print_line(int line_index, int line_height, const char *text,
                            bool clear_first, uint16_t fg_color,
                            uint16_t bg_color) {
  return hal_status_to_bool(hal_display_print_line_ex(
      line_index, line_height, text, clear_first, fg_color, bg_color));
}

bool hal_display_get_text_bounds(const char *text, int *width, int *height) {
  return hal_status_to_bool(
      hal_display_get_text_bounds_ex(text, width, height));
}

int hal_display_text_width(const char *text) {
  int width = 0;
  (void)hal_display_text_width_ex(text, &width);
  return width;
}

int hal_display_text_height(const char *text) {
  int height = 0;
  (void)hal_display_text_height_ex(text, &height);
  return height;
}

hal_status_t hal_display_text_width_ex(const char *text, int *out_width) {
  return out_width == nullptr
             ? HAL_EINVAL
             : hal_display_get_text_bounds_ex(text, out_width, nullptr);
}

hal_status_t hal_display_text_height_ex(const char *text, int *out_height) {
  return out_height == nullptr
             ? HAL_EINVAL
             : hal_display_get_text_bounds_ex(text, nullptr, out_height);
}

bool hal_display_println_prepared_text(char *text) {
  return hal_status_to_bool(hal_display_println_prepared_text_ex(text));
}

hal_status_t hal_display_println_prepared_text_ex(char *text) {
  return hal_display_println_ex(text);
}

hal_status_t hal_display_set_default_font_ex(void) {
  const hal_status_t status = hal_display_set_font_ex(HAL_FONT_DEFAULT);
  return hal_status_is_error(status) ? status
                                     : hal_display_set_text_size_ex(1u);
}

bool hal_display_set_default_font(void) {
  return hal_status_to_bool(hal_display_set_default_font_ex());
}

hal_status_t
hal_display_set_default_font_with_pos_and_color_ex(int x, int y,
                                                   uint16_t color) {
  hal_status_t status = hal_display_set_default_font_ex();
  if (hal_status_is_ok(status)) {
    status = hal_display_set_text_color_ex(color);
  }
  return hal_status_is_error(status) ? status : hal_display_set_cursor_ex(x, y);
}

bool hal_display_set_default_font_with_pos_and_color(int x, int y,
                                                     uint16_t color) {
  return hal_status_to_bool(
      hal_display_set_default_font_with_pos_and_color_ex(x, y, color));
}

hal_status_t hal_display_set_text_size_one_with_color_ex(uint16_t color) {
  const hal_status_t status = hal_display_set_text_size_ex(1u);
  return hal_status_is_error(status) ? status
                                     : hal_display_set_text_color_ex(color);
}

bool hal_display_set_text_size_one_with_color(uint16_t color) {
  return hal_status_to_bool(hal_display_set_text_size_one_with_color_ex(color));
}

hal_status_t hal_display_set_sans_bold_with_pos_and_color_ex(int x, int y,
                                                             uint16_t color) {
  hal_status_t status = hal_display_set_font_ex(HAL_FONT_SANS_BOLD_9PT);
  if (hal_status_is_ok(status)) {
    status = hal_display_set_cursor_ex(x, y);
  }
  return hal_status_is_error(status)
             ? status
             : hal_display_set_text_size_one_with_color_ex(color);
}

bool hal_display_set_sans_bold_with_pos_and_color(int x, int y,
                                                  uint16_t color) {
  return hal_status_to_bool(
      hal_display_set_sans_bold_with_pos_and_color_ex(x, y, color));
}

hal_status_t hal_display_set_serif9pt_with_color_ex(uint16_t color) {
  const hal_status_t status = hal_display_set_font_ex(HAL_FONT_SERIF_9PT);
  return hal_status_is_error(status)
             ? status
             : hal_display_set_text_size_one_with_color_ex(color);
}

bool hal_display_set_serif9pt_with_color(uint16_t color) {
  return hal_status_to_bool(hal_display_set_serif9pt_with_color_ex(color));
}

int hal_display_prepare_text_v(char *display_txt, size_t display_txt_size,
                               const char *format, va_list args) {
  if (display_txt == nullptr || display_txt_size == 0u || format == nullptr) {
    return 0;
  }
  const int written = vsnprintf(display_txt, display_txt_size, format, args);
  if (written < 0) {
    display_txt[0] = '\0';
    return 0;
  }
  display_txt[display_txt_size - 1u] = '\0';
  return hal_display_text_width(display_txt);
}

int hal_display_prepare_text(char *display_txt, size_t display_txt_size,
                             const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int width =
      hal_display_prepare_text_v(display_txt, display_txt_size, format, args);
  va_end(args);
  return width;
}

hal_status_t hal_display_prepare_text_v_ex(char *display_txt,
                                           size_t display_txt_size,
                                           int *out_width, const char *format,
                                           va_list args) {
  if (display_txt == nullptr || display_txt_size == 0u || format == nullptr) {
    return HAL_EINVAL;
  }
  const int written = vsnprintf(display_txt, display_txt_size, format, args);
  if (written < 0) {
    display_txt[0] = '\0';
    return HAL_EIO;
  }
  display_txt[display_txt_size - 1u] = '\0';
  if ((size_t)written >= display_txt_size) {
    if (out_width != nullptr) {
      *out_width = 0;
    }
    return HAL_EOVERFLOW;
  }
  return out_width == nullptr
             ? HAL_OK
             : hal_display_text_width_ex(display_txt, out_width);
}

hal_status_t hal_display_prepare_text_ex(char *display_txt,
                                         size_t display_txt_size,
                                         int *out_width, const char *format,
                                         ...) {
  if (display_txt == nullptr || display_txt_size == 0u || format == nullptr) {
    return HAL_EINVAL;
  }
  va_list args;
  va_start(args, format);
  const hal_status_t status = hal_display_prepare_text_v_ex(
      display_txt, display_txt_size, out_width, format, args);
  va_end(args);
  return status;
}

#endif
