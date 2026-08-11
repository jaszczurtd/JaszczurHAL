#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/display/hal_display.h"
#include "hal/display/hal_display_internal.h"
#include "hal/serial/hal_serial.h"
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
static hal_display_pixel_format_t s_pixel_format =
    HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE;
static uint8_t s_rotation = HAL_DISPLAY_ROTATION_0;

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

hal_status_t hal_display_init_ssd1306_family_ex(
    const hal_display_ssd1306_family_config_t *config) {
  if (config == NULL || config->width <= 0 || config->height <= 0 ||
      config->controller > HAL_DISPLAY_OLED_CONTROLLER_CH1115 ||
      config->bus_type > HAL_DISPLAY_OLED_BUS_SPI ||
      config->orientation > HAL_DISPLAY_OLED_ORIENTATION_ROTATED_180) {
    hal_derr("hal_display_init_ssd1306_family: invalid config");
    return HAL_EINVAL;
  }
#ifndef HAL_ENABLE_SPI
  if (config->bus_type == HAL_DISPLAY_OLED_BUS_SPI) {
    return HAL_EUNSUPPORTED;
  }
#endif
  s_width = config->width;
  s_height = config->height;
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
  s_rotation = r & 0x03u;
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
hal_status_t hal_display_suspend_ex(void) {
  return s_display_ready() ? HAL_OK : HAL_EUNINIT;
}
hal_status_t hal_display_resume_ex(void) {
  return s_display_ready() ? HAL_OK : HAL_EUNINIT;
}
hal_status_t hal_display_set_rotation_ex(uint8_t r) {
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  s_rotation = r & 0x03u;
  return HAL_OK;
}
hal_status_t hal_display_invert_ex(bool invert) {
  (void)invert;
  return s_display_ready() ? HAL_OK : HAL_EUNINIT;
}
void jh_hal_display_get_dimensions(int *out_width, int *out_height) {
  if (out_width != NULL) {
    *out_width = s_width;
  }
  if (out_height != NULL) {
    *out_height = s_height;
  }
}

hal_status_t hal_display_get_capabilities_ex(hal_display_capabilities_t *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }
  memset(out, 0, sizeof(*out));
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  out->width = (uint16_t)s_width;
  out->height = (uint16_t)s_height;
  out->supported_pixel_formats = HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE |
                                 HAL_DISPLAY_PIXEL_FORMAT_RGB565_NATIVE;
  out->current_pixel_format = s_pixel_format;
  out->current_rotation = s_rotation;
  out->supported_rotations = HAL_DISPLAY_ROTATION_MASK_ALL;
  out->x_alignment = 1u;
  out->y_alignment = 1u;
  out->width_alignment = 1u;
  out->height_alignment = 1u;
  out->flags = HAL_DISPLAY_CAP_RAW_WRITE | HAL_DISPLAY_CAP_STREAM_WRITE |
               HAL_DISPLAY_CAP_DMA_WRITE | HAL_DISPLAY_CAP_ASYNC_DMA_WRITE |
               HAL_DISPLAY_CAP_LEGACY_GFX;
  return HAL_OK;
}

hal_status_t
hal_display_set_pixel_format_ex(hal_display_pixel_format_t pixel_format) {
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  if (pixel_format != HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE &&
      pixel_format != HAL_DISPLAY_PIXEL_FORMAT_RGB565_NATIVE) {
    return HAL_EUNSUPPORTED;
  }
  s_pixel_format = pixel_format;
  return HAL_OK;
}

hal_status_t hal_display_write_raw_ex(uint16_t x, uint16_t y,
                                      const hal_display_buffer_desc_t *desc,
                                      const void *buffer) {
  if (desc == NULL || buffer == NULL || desc->width == 0u ||
      desc->height == 0u || desc->pitch < desc->width) {
    return HAL_EINVAL;
  }
  if (!s_display_ready()) {
    return HAL_EUNINIT;
  }
  if ((uint32_t)x + desc->width > (uint32_t)s_width ||
      (uint32_t)y + desc->height > (uint32_t)s_height) {
    return HAL_EINVAL;
  }
  if (desc->pitch != desc->width) {
    return HAL_EUNSUPPORTED;
  }
  if (desc->pixel_format != HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE &&
      desc->pixel_format != HAL_DISPLAY_PIXEL_FORMAT_RGB565_NATIVE) {
    return HAL_EUNSUPPORTED;
  }
  if (desc->buf_size < (size_t)desc->width * desc->height * 2u) {
    return HAL_EINVAL;
  }
  if (s_fail_next_io) {
    s_fail_next_io = false;
    return HAL_EIO;
  }
  s_stream_x = x;
  s_stream_y = y;
  s_stream_w = desc->width;
  s_stream_h = desc->height;
  return HAL_OK;
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
  s_pixel_format = HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE;
  s_rotation = HAL_DISPLAY_ROTATION_0;
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
