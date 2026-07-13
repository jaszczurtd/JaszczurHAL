#include "hal_display.h"

#ifdef HAL_ENABLE_DISPLAY

#include <stdarg.h>

/*
 * Backend-agnostic status adapter for the display HAL. Each wrapper validates
 * the arguments it can check locally (returning HAL_EINVAL before touching the
 * backend), then delegates to the legacy entry point and maps a residual
 * failure to the most representative status code:
 *
 *   - draw/text operations gate on a configured backend -> HAL_EUNINIT,
 *   - streaming operations depend on an open write window -> HAL_ESTATE,
 *   - initialisation/configuration backend failures       -> HAL_EIO.
 */

static hal_status_t display_check_size(int w, int h) {
  return (w > 0 && h > 0) ? HAL_OK : HAL_EINVAL;
}

#ifdef HAL_ENABLE_TFT
hal_status_t hal_display_init_ex(uint8_t cs, uint8_t dc, uint8_t rst) {
  hal_display_init(cs, dc, rst);
  return HAL_OK;
}
#endif /* HAL_ENABLE_TFT */

#ifdef HAL_ENABLE_SSD1306
hal_status_t
hal_display_init_ssd1306_i2c_status_ex(int width, int height, uint8_t i2c_bus,
                                       uint8_t i2c_addr, int8_t rst_pin,
                                       uint8_t switchvcc, bool periphBegin) {
  if (hal_status_is_error(display_check_size(width, height))) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(
      hal_display_init_ssd1306_i2c_ex(width, height, i2c_bus, i2c_addr, rst_pin,
                                      switchvcc, periphBegin),
      HAL_EIO);
}
#endif /* HAL_ENABLE_SSD1306 */

hal_status_t hal_display_configure_ex(int width, int height, uint8_t rotation,
                                      bool invert, bool bgr) {
  if (hal_status_is_error(display_check_size(width, height))) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(
      hal_display_configure(width, height, rotation, invert, bgr), HAL_EIO);
}

hal_status_t hal_display_soft_init_ex(int delay_ms) {
  hal_display_soft_init(delay_ms);
  return HAL_OK;
}

hal_status_t hal_display_set_rotation_ex(uint8_t r) {
  return hal_status_from_bool(hal_display_set_rotation(r), HAL_EUNINIT);
}

hal_status_t hal_display_invert_ex(bool invert) {
  return hal_status_from_bool(hal_display_invert(invert), HAL_EUNINIT);
}

hal_status_t hal_display_get_width_ex(int *out_width) {
  if (out_width == nullptr) {
    return HAL_EINVAL;
  }
  const int width = hal_display_get_width();
  *out_width = width;
  return width > 0 ? HAL_OK : HAL_EUNINIT;
}

hal_status_t hal_display_get_height_ex(int *out_height) {
  if (out_height == nullptr) {
    return HAL_EINVAL;
  }
  const int height = hal_display_get_height();
  *out_height = height;
  return height > 0 ? HAL_OK : HAL_EUNINIT;
}

hal_status_t hal_display_fill_screen_ex(uint16_t color) {
  return hal_status_from_bool(hal_display_fill_screen(color), HAL_EUNINIT);
}

hal_status_t hal_display_flush_ex(void) {
  return hal_status_from_bool(hal_display_flush(), HAL_EUNINIT);
}

hal_status_t hal_display_draw_image_ex(int x, int y, int w, int h,
                                       uint16_t background, uint16_t *data) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  if (hal_status_is_error(display_check_size(w, h))) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(
      hal_display_draw_image(x, y, w, h, background, data), HAL_EUNINIT);
}

hal_status_t hal_display_fill_rect_ex(int x, int y, int w, int h,
                                      uint16_t color) {
  if (hal_status_is_error(display_check_size(w, h))) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_fill_rect(x, y, w, h, color),
                              HAL_EUNINIT);
}

hal_status_t hal_display_draw_rect_ex(int x, int y, int w, int h,
                                      uint16_t color) {
  if (hal_status_is_error(display_check_size(w, h))) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_draw_rect(x, y, w, h, color),
                              HAL_EUNINIT);
}

hal_status_t hal_display_fill_circle_ex(int x, int y, int r, uint16_t color) {
  if (r < 0) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_fill_circle(x, y, r, color),
                              HAL_EUNINIT);
}

hal_status_t hal_display_draw_circle_ex(int x, int y, int r, uint16_t color) {
  if (r < 0) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_draw_circle(x, y, r, color),
                              HAL_EUNINIT);
}

hal_status_t hal_display_fill_round_rect_ex(int x, int y, int w, int h, int r,
                                            uint16_t color) {
  if (r < 0 || hal_status_is_error(display_check_size(w, h))) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_fill_round_rect(x, y, w, h, r, color),
                              HAL_EUNINIT);
}

hal_status_t hal_display_draw_line_ex(int x0, int y0, int x1, int y1,
                                      uint16_t color) {
  return hal_status_from_bool(hal_display_draw_line(x0, y0, x1, y1, color),
                              HAL_EUNINIT);
}

hal_status_t hal_display_draw_rgb_bitmap_ex(int x, int y, uint16_t *data, int w,
                                            int h) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  if (hal_status_is_error(display_check_size(w, h))) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_draw_rgb_bitmap(x, y, data, w, h),
                              HAL_EUNINIT);
}

hal_status_t hal_display_begin_write_ex(int x, int y, int w, int h) {
  if (hal_status_is_error(display_check_size(w, h))) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_begin_write(x, y, w, h), HAL_ESTATE);
}

hal_status_t hal_display_write_pixels_fast_ex(const uint16_t *pixels,
                                              size_t count) {
  if (pixels == nullptr && count > 0u) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_write_pixels_fast(pixels, count),
                              HAL_ESTATE);
}

hal_status_t hal_display_write_pixels_be_ex(const uint8_t *pixels_be,
                                            size_t byte_count) {
  if ((pixels_be == nullptr && byte_count > 0u) || (byte_count & 1u) != 0u) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(
      hal_display_write_pixels_be(pixels_be, byte_count), HAL_ESTATE);
}

hal_status_t
hal_display_write_pixels_dma_async_start_ex(const uint8_t *pixels_be,
                                            size_t byte_count) {
  if ((pixels_be == nullptr && byte_count > 0u) || (byte_count & 1u) != 0u) {
    return HAL_EINVAL;
  }
  if (hal_display_write_pixels_dma_async_busy()) {
    return HAL_EBUSY;
  }
  return hal_status_from_bool(
      hal_display_write_pixels_dma_async_start(pixels_be, byte_count),
      HAL_ESTATE);
}

hal_status_t hal_display_write_pixels_dma_async_wait_ex(void) {
  return hal_status_from_bool(hal_display_write_pixels_dma_async_wait(),
                              HAL_EIO);
}

hal_status_t hal_display_write_pixels_dma_ex(const uint8_t *pixels_be,
                                             size_t byte_count) {
  const hal_status_t status =
      hal_display_write_pixels_dma_async_start_ex(pixels_be, byte_count);
  if (hal_status_is_error(status)) {
    return status;
  }
  return hal_display_write_pixels_dma_async_wait_ex();
}

hal_status_t hal_display_end_write_ex(void) {
  return hal_status_from_bool(hal_display_end_write(), HAL_ESTATE);
}

hal_status_t hal_display_set_font_ex(hal_font_id_t font) {
  return hal_status_from_bool(hal_display_set_font(font), HAL_EUNINIT);
}

hal_status_t hal_display_set_text_color_ex(uint16_t color) {
  return hal_status_from_bool(hal_display_set_text_color(color), HAL_EUNINIT);
}

hal_status_t hal_display_set_text_size_ex(uint8_t size) {
  if (size == 0u) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_set_text_size(size), HAL_EUNINIT);
}

hal_status_t hal_display_set_cursor_ex(int x, int y) {
  return hal_status_from_bool(hal_display_set_cursor(x, y), HAL_EUNINIT);
}

hal_status_t hal_display_print_ex(const char *s) {
  if (s == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_print(s), HAL_EUNINIT);
}

hal_status_t hal_display_println_ex(const char *s) {
  if (s == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_println(s), HAL_EUNINIT);
}

hal_status_t hal_display_print_at_ex(int x, int y, const char *s) {
  if (s == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_print_at(x, y, s), HAL_EUNINIT);
}

hal_status_t hal_display_clear_text_line_ex(int line_index, int line_height,
                                            uint16_t bg_color) {
  if (line_index < 0 || line_height <= 0) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(
      hal_display_clear_text_line(line_index, line_height, bg_color),
      HAL_EUNINIT);
}

hal_status_t hal_display_print_line_ex(int line_index, int line_height,
                                       const char *text, bool clear_first,
                                       uint16_t fg_color, uint16_t bg_color) {
  if (text == nullptr) {
    return HAL_EINVAL;
  }
  if (line_index < 0 || line_height <= 0) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_print_line(line_index, line_height,
                                                     text, clear_first,
                                                     fg_color, bg_color),
                              HAL_EUNINIT);
}

hal_status_t hal_display_draw_text_centered_ex(const char *text,
                                               uint16_t fg_color,
                                               uint16_t bg_color,
                                               bool clear_first,
                                               bool flush_after) {
  if (text == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(
      hal_display_draw_text_centered(text, fg_color, bg_color, clear_first,
                                     flush_after),
      HAL_EUNINIT);
}

hal_status_t hal_display_get_text_bounds_ex(const char *s, int *w, int *h) {
  if (s == nullptr || (w == nullptr && h == nullptr)) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_get_text_bounds(s, w, h),
                              HAL_EUNINIT);
}

hal_status_t hal_display_text_width_ex(const char *text, int *out_width) {
  if (text == nullptr || out_width == nullptr) {
    return HAL_EINVAL;
  }
  int width = 0;
  const hal_status_t status =
      hal_display_get_text_bounds_ex(text, &width, nullptr);
  *out_width = hal_status_is_ok(status) ? width : 0;
  return status;
}

hal_status_t hal_display_text_height_ex(const char *text, int *out_height) {
  if (text == nullptr || out_height == nullptr) {
    return HAL_EINVAL;
  }
  int height = 0;
  const hal_status_t status =
      hal_display_get_text_bounds_ex(text, nullptr, &height);
  *out_height = hal_status_is_ok(status) ? height : 0;
  return status;
}

hal_status_t hal_display_println_prepared_text_ex(char *text) {
  if (text == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_display_println_prepared_text(text),
                              HAL_EUNINIT);
}

hal_status_t hal_display_set_default_font_ex(void) {
  return hal_status_from_bool(hal_display_set_default_font(), HAL_EUNINIT);
}

hal_status_t
hal_display_set_default_font_with_pos_and_color_ex(int x, int y,
                                                   uint16_t color) {
  return hal_status_from_bool(
      hal_display_set_default_font_with_pos_and_color(x, y, color),
      HAL_EUNINIT);
}

hal_status_t hal_display_set_text_size_one_with_color_ex(uint16_t color) {
  return hal_status_from_bool(hal_display_set_text_size_one_with_color(color),
                              HAL_EUNINIT);
}

hal_status_t hal_display_set_sans_bold_with_pos_and_color_ex(int x, int y,
                                                             uint16_t color) {
  return hal_status_from_bool(
      hal_display_set_sans_bold_with_pos_and_color(x, y, color), HAL_EUNINIT);
}

hal_status_t hal_display_set_serif9pt_with_color_ex(uint16_t color) {
  return hal_status_from_bool(hal_display_set_serif9pt_with_color(color),
                              HAL_EUNINIT);
}

hal_status_t hal_display_prepare_text_v_ex(char *display_txt,
                                           size_t display_txt_size,
                                           int *out_width, const char *format,
                                           va_list args) {
  if (display_txt == nullptr || format == nullptr || display_txt_size == 0u) {
    return HAL_EINVAL;
  }
  const int width =
      hal_display_prepare_text_v(display_txt, display_txt_size, format, args);
  if (out_width != nullptr) {
    *out_width = width;
  }
  return HAL_OK;
}

hal_status_t hal_display_prepare_text_ex(char *display_txt,
                                         size_t display_txt_size,
                                         int *out_width, const char *format,
                                         ...) {
  if (display_txt == nullptr || format == nullptr || display_txt_size == 0u) {
    return HAL_EINVAL;
  }
  va_list args;
  va_start(args, format);
  const hal_status_t status = hal_display_prepare_text_v_ex(
      display_txt, display_txt_size, out_width, format, args);
  va_end(args);
  return status;
}

#endif /* HAL_ENABLE_DISPLAY */
