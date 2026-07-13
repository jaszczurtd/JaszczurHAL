#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_display.h"
#include "../../hal_serial.h"
#include "hal_mock.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int s_width = 0;
static int s_height = 0;
static hal_font_id_t s_font = HAL_FONT_DEFAULT;
static uint16_t s_text_color = 0;
static uint8_t s_text_size = 1;
static int s_cursor_x = 0;
static int s_cursor_y = 0;
static char s_last_print[256] = {};
static char s_last_println[256] = {};
static int s_last_fill_rect_x = 0;
static int s_last_fill_rect_y = 0;
static int s_last_fill_rect_w = 0;
static int s_last_fill_rect_h = 0;
static uint16_t s_last_fill_rect_color = 0;
static int s_last_bitmap_x = 0;
static int s_last_bitmap_y = 0;
static int s_last_bitmap_w = 0;
static int s_last_bitmap_h = 0;
static uint16_t *s_last_bitmap_data = NULL;
static bool s_stream_active = false;
static int s_stream_x = 0;
static int s_stream_y = 0;
static int s_stream_w = 0;
static int s_stream_h = 0;
static size_t s_stream_fast_pixels = 0;
static size_t s_stream_be_bytes = 0;
static size_t s_stream_dma_bytes = 0;
static bool s_fail_next_io = false;

static bool s_display_ready(void) {
  if (s_width > 0 && s_height > 0) {
    return true;
  }
  hal_derr("hal_display: backend is not initialized/configured");
  return false;
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
  if (dst_size == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

#ifdef HAL_ENABLE_TFT
hal_status_t hal_display_init(uint8_t cs, uint8_t dc, uint8_t rst) {
  (void)cs;
  (void)dc;
  (void)rst;
  return HAL_OK;
}
#endif /* HAL_ENABLE_TFT */
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

hal_status_t
hal_display_init_ssd1306_i2c_status_ex(int width, int height, uint8_t i2c_bus,
                                       uint8_t i2c_addr, int8_t rst_pin,
                                       uint8_t switchvcc, bool periph_begin) {
  if (width <= 0 || height <= 0) {
    hal_derr("hal_display_init_ssd1306_i2c: invalid size %dx%d", width, height);
    return HAL_EINVAL;
  }
  (void)i2c_bus;
  (void)i2c_addr;
  (void)rst_pin;
  (void)switchvcc;
  (void)periph_begin;
  s_width = width;
  s_height = height;
  return HAL_OK;
}
#endif /* HAL_ENABLE_SSD1306 */

hal_status_t hal_display_configure_ex(int w, int h, uint8_t r, bool inv,
                                      bool bgr) {
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_configure: invalid size %dx%d", w, h);
    return HAL_EINVAL;
  }
  s_width = w;
  s_height = h;
  (void)r;
  (void)inv;
  (void)bgr;
  return HAL_OK;
}

bool hal_display_configure(int w, int h, uint8_t r, bool inv, bool bgr) {
  return hal_status_to_bool(hal_display_configure_ex(w, h, r, inv, bgr));
}

hal_status_t hal_display_soft_init(int delay_ms) {
  (void)delay_ms;
  return HAL_OK;
}
hal_status_t hal_display_set_rotation_ex(uint8_t r) {
  (void)r;
  return s_display_ready() ? HAL_OK : HAL_EUNINIT;
}
bool hal_display_set_rotation(uint8_t r) {
  return hal_status_to_bool(hal_display_set_rotation_ex(r));
}

hal_status_t hal_display_invert_ex(bool invert) {
  (void)invert;
  return s_display_ready() ? HAL_OK : HAL_EUNINIT;
}
bool hal_display_invert(bool invert) {
  return hal_status_to_bool(hal_display_invert_ex(invert));
}

int hal_display_get_width(void) { return s_width; }
int hal_display_get_height(void) { return s_height; }

hal_status_t hal_display_get_width_ex(int *out_width) {
  if (out_width == NULL) {
    return HAL_EINVAL;
  }
  *out_width = s_width;
  return s_width > 0 ? HAL_OK : HAL_EUNINIT;
}

hal_status_t hal_display_get_height_ex(int *out_height) {
  if (out_height == NULL) {
    return HAL_EINVAL;
  }
  *out_height = s_height;
  return s_height > 0 ? HAL_OK : HAL_EUNINIT;
}

hal_status_t hal_display_fill_screen_ex(uint16_t color) {
  (void)color;
  return s_display_ready() ? HAL_OK : HAL_EUNINIT;
}
bool hal_display_fill_screen(uint16_t color) {
  return hal_status_to_bool(hal_display_fill_screen_ex(color));
}

hal_status_t hal_display_flush_ex(void) {
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  if (s_fail_next_io) {
    s_fail_next_io = false;
    return HAL_EIO;
  }
  return HAL_OK;
}
bool hal_display_flush(void) {
  return hal_status_to_bool(hal_display_flush_ex());
}

hal_status_t hal_display_draw_image_ex(int x, int y, int w, int h,
                                       uint16_t background, uint16_t *data) {
  if (!data) {
    hal_derr("hal_display_draw_image: data pointer is NULL");
    return HAL_EINVAL;
  }
  hal_status_t status = hal_display_fill_rect_ex(x, y, w, h, background);
  return hal_status_is_error(status)
             ? status
             : hal_display_draw_rgb_bitmap_ex(x, y, data, w, h);
}
bool hal_display_draw_image(int x, int y, int w, int h, uint16_t background,
                            uint16_t *data) {
  return hal_status_to_bool(
      hal_display_draw_image_ex(x, y, w, h, background, data));
}

hal_status_t hal_display_fill_rect_ex(int x, int y, int w, int h,
                                      uint16_t color) {
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_fill_rect: invalid size %dx%d", w, h);
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  s_last_fill_rect_x = x;
  s_last_fill_rect_y = y;
  s_last_fill_rect_w = w;
  s_last_fill_rect_h = h;
  s_last_fill_rect_color = color;
  return HAL_OK;
}
bool hal_display_fill_rect(int x, int y, int w, int h, uint16_t color) {
  return hal_status_to_bool(hal_display_fill_rect_ex(x, y, w, h, color));
}

hal_status_t hal_display_draw_rect_ex(int x, int y, int w, int h,
                                      uint16_t color) {
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_draw_rect: invalid size %dx%d", w, h);
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  (void)x;
  (void)y;
  (void)color;
  return HAL_OK;
}
bool hal_display_draw_rect(int x, int y, int w, int h, uint16_t color) {
  return hal_status_to_bool(hal_display_draw_rect_ex(x, y, w, h, color));
}

hal_status_t hal_display_fill_circle_ex(int x, int y, int r, uint16_t color) {
  if (r < 0) {
    hal_derr("hal_display_fill_circle: invalid radius %d", r);
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  (void)x;
  (void)y;
  (void)color;
  return HAL_OK;
}
bool hal_display_fill_circle(int x, int y, int r, uint16_t color) {
  return hal_status_to_bool(hal_display_fill_circle_ex(x, y, r, color));
}

hal_status_t hal_display_draw_circle_ex(int x, int y, int r, uint16_t color) {
  if (r < 0) {
    hal_derr("hal_display_draw_circle: invalid radius %d", r);
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  (void)x;
  (void)y;
  (void)color;
  return HAL_OK;
}
bool hal_display_draw_circle(int x, int y, int r, uint16_t color) {
  return hal_status_to_bool(hal_display_draw_circle_ex(x, y, r, color));
}

hal_status_t hal_display_fill_round_rect_ex(int x, int y, int w, int h, int r,
                                            uint16_t color) {
  if (w <= 0 || h <= 0 || r < 0) {
    hal_derr("hal_display_fill_round_rect: invalid size/radius w=%d h=%d r=%d",
             w, h, r);
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  (void)x;
  (void)y;
  (void)color;
  return HAL_OK;
}
bool hal_display_fill_round_rect(int x, int y, int w, int h, int r,
                                 uint16_t color) {
  return hal_status_to_bool(
      hal_display_fill_round_rect_ex(x, y, w, h, r, color));
}

hal_status_t hal_display_draw_line_ex(int x0, int y0, int x1, int y1,
                                      uint16_t color) {
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  (void)x0;
  (void)y0;
  (void)x1;
  (void)y1;
  (void)color;
  return HAL_OK;
}
bool hal_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
  return hal_status_to_bool(hal_display_draw_line_ex(x0, y0, x1, y1, color));
}

hal_status_t hal_display_draw_rgb_bitmap_ex(int x, int y, uint16_t *data, int w,
                                            int h) {
  if (!data) {
    hal_derr("hal_display_draw_rgb_bitmap: data pointer is NULL");
    return HAL_EINVAL;
  }
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_draw_rgb_bitmap: invalid size %dx%d", w, h);
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  s_last_bitmap_x = x;
  s_last_bitmap_y = y;
  s_last_bitmap_w = w;
  s_last_bitmap_h = h;
  s_last_bitmap_data = data;
  return HAL_OK;
}
bool hal_display_draw_rgb_bitmap(int x, int y, uint16_t *data, int w, int h) {
  return hal_status_to_bool(hal_display_draw_rgb_bitmap_ex(x, y, data, w, h));
}
hal_status_t hal_display_begin_write_ex(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_begin_write: invalid size %dx%d", w, h);
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  if (s_stream_active) {
    hal_derr("hal_display_begin_write: stream already active");
    return HAL_EBUSY;
  }
  if (x < 0 || y < 0 || x > s_width - w || y > s_height - h) {
    hal_derr(
        "hal_display_begin_write: out-of-bounds clipping is not supported");
    return HAL_EIO;
  }
  s_stream_active = true;
  s_stream_x = x;
  s_stream_y = y;
  s_stream_w = w;
  s_stream_h = h;
  s_stream_fast_pixels = 0;
  s_stream_be_bytes = 0;
  s_stream_dma_bytes = 0;
  return HAL_OK;
}
bool hal_display_begin_write(int x, int y, int w, int h) {
  return hal_status_to_bool(hal_display_begin_write_ex(x, y, w, h));
}

hal_status_t hal_display_write_pixels_fast_ex(const uint16_t *pixels,
                                              size_t count) {
  if (pixels == NULL && count > 0u) {
    hal_derr("hal_display_write_pixels_fast: invalid stream write");
    return HAL_EINVAL;
  }
  if (!s_stream_active) {
    return HAL_ESTATE;
  }
  s_stream_fast_pixels += count;
  return HAL_OK;
}
bool hal_display_write_pixels_fast(const uint16_t *pixels, size_t count) {
  return hal_status_to_bool(hal_display_write_pixels_fast_ex(pixels, count));
}

hal_status_t hal_display_write_pixels_be_ex(const uint8_t *pixels_be,
                                            size_t byte_count) {
  if ((pixels_be == NULL && byte_count > 0u) || (byte_count & 1u) != 0u) {
    hal_derr("hal_display_write_pixels_be: invalid stream write");
    return HAL_EINVAL;
  }
  if (!s_stream_active) {
    return HAL_ESTATE;
  }
  s_stream_be_bytes += byte_count;
  return HAL_OK;
}
bool hal_display_write_pixels_be(const uint8_t *pixels_be, size_t byte_count) {
  return hal_status_to_bool(
      hal_display_write_pixels_be_ex(pixels_be, byte_count));
}

hal_status_t
hal_display_write_pixels_dma_async_start_ex(const uint8_t *pixels_be,
                                            size_t byte_count) {
  if ((pixels_be == NULL && byte_count > 0u) || (byte_count & 1u) != 0u) {
    hal_derr("hal_display_write_pixels_dma_async_start: invalid stream write");
    return HAL_EINVAL;
  }
  if (!s_stream_active) {
    return HAL_ESTATE;
  }
  s_stream_dma_bytes += byte_count;
  return HAL_OK;
}
bool hal_display_write_pixels_dma_async_start(const uint8_t *pixels_be,
                                              size_t byte_count) {
  return hal_status_to_bool(
      hal_display_write_pixels_dma_async_start_ex(pixels_be, byte_count));
}
bool hal_display_write_pixels_dma_async_busy(void) { return false; }

hal_status_t hal_display_write_pixels_dma_async_wait_ex(void) { return HAL_OK; }
bool hal_display_write_pixels_dma_async_wait(void) {
  return hal_status_to_bool(hal_display_write_pixels_dma_async_wait_ex());
}

hal_status_t hal_display_write_pixels_dma_ex(const uint8_t *pixels_be,
                                             size_t byte_count) {
  hal_status_t status =
      hal_display_write_pixels_dma_async_start_ex(pixels_be, byte_count);
  if (hal_status_is_error(status)) {
    return status;
  }
  return hal_display_write_pixels_dma_async_wait_ex();
}
bool hal_display_write_pixels_dma(const uint8_t *pixels_be, size_t byte_count) {
  return hal_status_to_bool(
      hal_display_write_pixels_dma_ex(pixels_be, byte_count));
}

hal_status_t hal_display_end_write_ex(void) {
  if (!s_stream_active) {
    hal_derr("hal_display_end_write: no active stream");
    return HAL_ESTATE;
  }
  s_stream_active = false;
  return HAL_OK;
}
bool hal_display_end_write(void) {
  return hal_status_to_bool(hal_display_end_write_ex());
}
hal_status_t hal_display_set_font_ex(hal_font_id_t font) {
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  s_font = font;
  return HAL_OK;
}
bool hal_display_set_font(hal_font_id_t font) {
  return hal_status_to_bool(hal_display_set_font_ex(font));
}

hal_status_t hal_display_set_text_color_ex(uint16_t color) {
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  s_text_color = color;
  return HAL_OK;
}
bool hal_display_set_text_color(uint16_t color) {
  return hal_status_to_bool(hal_display_set_text_color_ex(color));
}

hal_status_t hal_display_set_text_size_ex(uint8_t size) {
  if (size == 0) {
    hal_derr("hal_display_set_text_size: size must be > 0");
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  s_text_size = size;
  return HAL_OK;
}
bool hal_display_set_text_size(uint8_t size) {
  return hal_status_to_bool(hal_display_set_text_size_ex(size));
}

hal_status_t hal_display_set_cursor_ex(int x, int y) {
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  s_cursor_x = x;
  s_cursor_y = y;
  return HAL_OK;
}
bool hal_display_set_cursor(int x, int y) {
  return hal_status_to_bool(hal_display_set_cursor_ex(x, y));
}

hal_status_t hal_display_print_ex(const char *s) {
  if (!s) {
    hal_derr("hal_display_print: text pointer is NULL");
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  copy_text(s_last_print, sizeof(s_last_print), s);
  return HAL_OK;
}
bool hal_display_print(const char *s) {
  return hal_status_to_bool(hal_display_print_ex(s));
}

hal_status_t hal_display_println_ex(const char *s) {
  if (!s) {
    hal_derr("hal_display_println: text pointer is NULL");
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  copy_text(s_last_println, sizeof(s_last_println), s);
  return HAL_OK;
}
bool hal_display_println(const char *s) {
  return hal_status_to_bool(hal_display_println_ex(s));
}

hal_status_t hal_display_print_at_ex(int x, int y, const char *s) {
  if (!s) {
    hal_derr("hal_display_print_at: text pointer is NULL");
    return HAL_EINVAL;
  }
  hal_status_t status = hal_display_set_cursor_ex(x, y);
  return hal_status_is_error(status) ? status : hal_display_print_ex(s);
}
bool hal_display_print_at(int x, int y, const char *s) {
  return hal_status_to_bool(hal_display_print_at_ex(x, y, s));
}

hal_status_t hal_display_clear_text_line_ex(int line_index, int line_height,
                                            uint16_t bg_color) {
  if (line_index < 0 || line_height <= 0) {
    hal_derr(
        "hal_display_clear_text_line: invalid line/index line=%d height=%d",
        line_index, line_height);
    return HAL_EINVAL;
  }
  return hal_display_fill_rect_ex(0, line_index * line_height,
                                  hal_display_get_width(), line_height,
                                  bg_color);
}
bool hal_display_clear_text_line(int line_index, int line_height,
                                 uint16_t bg_color) {
  return hal_status_to_bool(
      hal_display_clear_text_line_ex(line_index, line_height, bg_color));
}

hal_status_t hal_display_print_line_ex(int line_index, int line_height,
                                       const char *text, bool clear_first,
                                       uint16_t fg_color, uint16_t bg_color) {
  if (!text) {
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

hal_status_t hal_display_draw_text_centered_ex(const char *text,
                                               uint16_t fg_color,
                                               uint16_t bg_color,
                                               bool clear_first,
                                               bool flush_after) {
  if (!text) {
    hal_derr("hal_display_draw_text_centered: text pointer is NULL");
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  hal_status_t status = HAL_OK;
  if (clear_first) {
    status = hal_display_fill_screen_ex(bg_color);
  }
  int w = 0;
  int h = 0;
  if (hal_status_is_ok(status)) {
    status = hal_display_get_text_bounds_ex(text, &w, &h);
  }
  const int x = (hal_display_get_width() - w) / 2;
  const int y = (hal_display_get_height() - h) / 2;
  if (hal_status_is_ok(status)) {
    status = hal_display_set_text_color_ex(fg_color);
  }
  if (hal_status_is_ok(status)) {
    status = hal_display_print_at_ex(x, y, text);
  }
  return hal_status_is_ok(status) && flush_after ? hal_display_flush_ex()
                                                 : status;
}
bool hal_display_draw_text_centered(const char *text, uint16_t fg_color,
                                    uint16_t bg_color, bool clear_first,
                                    bool flush_after) {
  return hal_status_to_bool(hal_display_draw_text_centered_ex(
      text, fg_color, bg_color, clear_first, flush_after));
}

hal_status_t hal_display_get_text_bounds_ex(const char *s, int *w, int *h) {
  int dummy_w = 0;
  int dummy_h = 0;
  int *out_w = w ? w : &dummy_w;
  int *out_h = h ? h : &dummy_h;

  if (!w && !h) {
    hal_derr("hal_display_get_text_bounds: both output pointers are NULL");
    return HAL_EINVAL;
  }
  if (!s) {
    hal_derr("hal_display_get_text_bounds: text pointer is NULL");
    *out_w = 0;
    *out_h = 0;
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    *out_w = 0;
    *out_h = 0;
    return HAL_EUNINIT;
  }
  int text_w = 0;
  text_w = (int)strlen(s) * 6;
  *out_w = text_w;
  *out_h = 8;
  return HAL_OK;
}
bool hal_display_get_text_bounds(const char *s, int *w, int *h) {
  return hal_status_to_bool(hal_display_get_text_bounds_ex(s, w, h));
}
int hal_display_text_width(const char *text) {
  int w = 0;
  (void)hal_display_text_width_ex(text, &w);
  return w;
}
int hal_display_text_height(const char *text) {
  int h = 0;
  (void)hal_display_text_height_ex(text, &h);
  return h;
}
hal_status_t hal_display_text_width_ex(const char *text, int *out_width) {
  return out_width == NULL
             ? HAL_EINVAL
             : hal_display_get_text_bounds_ex(text, out_width, NULL);
}
hal_status_t hal_display_text_height_ex(const char *text, int *out_height) {
  return out_height == NULL
             ? HAL_EINVAL
             : hal_display_get_text_bounds_ex(text, NULL, out_height);
}
bool hal_display_println_prepared_text(char *text) {
  return hal_status_to_bool(hal_display_println_prepared_text_ex(text));
}
hal_status_t hal_display_println_prepared_text_ex(char *text) {
  return hal_display_println_ex(text);
}

hal_status_t hal_display_set_default_font_ex(void) {
  hal_status_t status = hal_display_set_font_ex(HAL_FONT_DEFAULT);
  return hal_status_is_error(status) ? status : hal_display_set_text_size_ex(1);
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
  hal_status_t status = hal_display_set_text_size_ex(1);
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
  hal_status_t status = hal_display_set_font_ex(HAL_FONT_SERIF_9PT);
  return hal_status_is_error(status)
             ? status
             : hal_display_set_text_size_one_with_color_ex(color);
}
bool hal_display_set_serif9pt_with_color(uint16_t color) {
  return hal_status_to_bool(hal_display_set_serif9pt_with_color_ex(color));
}
int hal_display_prepare_text_v(char *display_txt, size_t display_txt_size,
                               const char *format, va_list args) {
  if (!display_txt) {
    hal_derr("hal_display_prepare_text_v: output buffer is NULL");
    return 0;
  }
  if (!format) {
    hal_derr("hal_display_prepare_text_v: format string is NULL");
    return 0;
  }
  if (display_txt_size == 0) {
    hal_derr("hal_display_prepare_text_v: output buffer size is 0");
    return 0;
  }
  memset(display_txt, 0, display_txt_size);
  vsnprintf(display_txt, display_txt_size - 1, format, args);
  return hal_display_text_width(display_txt);
}
int hal_display_prepare_text(char *display_txt, size_t display_txt_size,
                             const char *format, ...) {
  va_list args;
  va_start(args, format);
  int w =
      hal_display_prepare_text_v(display_txt, display_txt_size, format, args);
  va_end(args);
  return w;
}

hal_status_t hal_display_prepare_text_v_ex(char *display_txt,
                                           size_t display_txt_size,
                                           int *out_width, const char *format,
                                           va_list args) {
  if (display_txt == NULL || display_txt_size == 0u || format == NULL) {
    return HAL_EINVAL;
  }
  const int written = vsnprintf(display_txt, display_txt_size, format, args);
  if (written < 0) {
    display_txt[0] = '\0';
    return HAL_EIO;
  }
  display_txt[display_txt_size - 1u] = '\0';
  if ((size_t)written >= display_txt_size) {
    if (out_width != NULL) {
      *out_width = 0;
    }
    return HAL_EOVERFLOW;
  }
  return out_width == NULL ? HAL_OK
                           : hal_display_text_width_ex(display_txt, out_width);
}

hal_status_t hal_display_prepare_text_ex(char *display_txt,
                                         size_t display_txt_size,
                                         int *out_width, const char *format,
                                         ...) {
  if (display_txt == NULL || display_txt_size == 0u || format == NULL) {
    return HAL_EINVAL;
  }
  va_list args;
  va_start(args, format);
  const hal_status_t status = hal_display_prepare_text_v_ex(
      display_txt, display_txt_size, out_width, format, args);
  va_end(args);
  return status;
}

void hal_mock_display_reset(void) {
  s_width = 0;
  s_height = 0;
  s_font = HAL_FONT_DEFAULT;
  s_text_color = 0;
  s_text_size = 1;
  s_cursor_x = 0;
  s_cursor_y = 0;
  s_last_print[0] = '\0';
  s_last_println[0] = '\0';
  s_last_fill_rect_x = 0;
  s_last_fill_rect_y = 0;
  s_last_fill_rect_w = 0;
  s_last_fill_rect_h = 0;
  s_last_fill_rect_color = 0;
  s_last_bitmap_x = 0;
  s_last_bitmap_y = 0;
  s_last_bitmap_w = 0;
  s_last_bitmap_h = 0;
  s_last_bitmap_data = NULL;
  s_stream_active = false;
  s_stream_x = 0;
  s_stream_y = 0;
  s_stream_w = 0;
  s_stream_h = 0;
  s_stream_fast_pixels = 0;
  s_stream_be_bytes = 0;
  s_stream_dma_bytes = 0;
  s_fail_next_io = false;
}

void hal_mock_display_fail_next_io(void) { s_fail_next_io = true; }

const char *hal_mock_display_last_print(void) { return s_last_print; }

const char *hal_mock_display_last_println(void) { return s_last_println; }

hal_font_id_t hal_mock_display_get_font(void) { return s_font; }

uint16_t hal_mock_display_get_text_color(void) { return s_text_color; }

uint8_t hal_mock_display_get_text_size(void) { return s_text_size; }

void hal_mock_display_get_cursor(int *x, int *y) {
  if (x)
    *x = s_cursor_x;
  if (y)
    *y = s_cursor_y;
}

void hal_mock_display_get_last_fill_rect(int *x, int *y, int *w, int *h,
                                         uint16_t *color) {
  if (x)
    *x = s_last_fill_rect_x;
  if (y)
    *y = s_last_fill_rect_y;
  if (w)
    *w = s_last_fill_rect_w;
  if (h)
    *h = s_last_fill_rect_h;
  if (color)
    *color = s_last_fill_rect_color;
}

void hal_mock_display_get_last_bitmap(int *x, int *y, uint16_t **data, int *w,
                                      int *h) {
  if (x)
    *x = s_last_bitmap_x;
  if (y)
    *y = s_last_bitmap_y;
  if (data)
    *data = s_last_bitmap_data;
  if (w)
    *w = s_last_bitmap_w;
  if (h)
    *h = s_last_bitmap_h;
}

bool hal_mock_display_stream_active(void) { return s_stream_active; }

void hal_mock_display_get_last_stream_window(int *x, int *y, int *w, int *h) {
  if (x)
    *x = s_stream_x;
  if (y)
    *y = s_stream_y;
  if (w)
    *w = s_stream_w;
  if (h)
    *h = s_stream_h;
}

size_t hal_mock_display_get_stream_fast_pixels(void) {
  return s_stream_fast_pixels;
}

size_t hal_mock_display_get_stream_be_bytes(void) { return s_stream_be_bytes; }

size_t hal_mock_display_get_stream_dma_bytes(void) {
  return s_stream_dma_bytes;
}
#endif // HAL_TARGET_IS_MOCK
