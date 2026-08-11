#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK || HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_DISPLAY

#include "hal/display/hal_display.h"
#include "hal/display/hal_display_internal.h"
#include "hal/serial/hal_serial.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef HAL_ENABLE_SSD1306
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
#endif
