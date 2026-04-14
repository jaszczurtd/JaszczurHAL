#include "../../hal_display.h"
#include "../../hal_serial.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "hal_mock.h"

static int         s_width = 0;
static int         s_height = 0;
static hal_font_id_t s_font = HAL_FONT_DEFAULT;
static uint16_t    s_text_color = 0;
static uint8_t     s_text_size = 1;
static int         s_cursor_x = 0;
static int         s_cursor_y = 0;
static char        s_last_print[256] = {};
static char        s_last_println[256] = {};
static int         s_last_fill_rect_x = 0;
static int         s_last_fill_rect_y = 0;
static int         s_last_fill_rect_w = 0;
static int         s_last_fill_rect_h = 0;
static uint16_t    s_last_fill_rect_color = 0;
static int         s_last_bitmap_x = 0;
static int         s_last_bitmap_y = 0;
static int         s_last_bitmap_w = 0;
static int         s_last_bitmap_h = 0;
static uint16_t   *s_last_bitmap_data = NULL;

static bool s_display_ready(void) {
	if (s_width > 0 && s_height > 0) {
		return true;
	}
	hal_derr("hal_display: backend is not initialized/configured");
	return false;
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
	if (dst_size == 0) return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, dst_size - 1);
	dst[dst_size - 1] = '\0';
}

#ifndef HAL_DISABLE_TFT
void hal_display_init(uint8_t cs, uint8_t dc, uint8_t rst)                    { (void)cs; (void)dc; (void)rst; }
#endif /* !HAL_DISABLE_TFT */
#ifndef HAL_DISABLE_SSD1306
bool hal_display_init_ssd1306_i2c(int width, int height, uint8_t i2c_addr,
                                  int8_t rst_pin, uint8_t switchvcc,
                                  bool periphBegin) {
	return hal_display_init_ssd1306_i2c_ex(width, height, 0, i2c_addr, rst_pin,
	                                       switchvcc, periphBegin);
}

bool hal_display_init_ssd1306_i2c_ex(int width, int height, uint8_t i2c_bus,
                                     uint8_t i2c_addr, int8_t rst_pin,
                                     uint8_t switchvcc, bool periphBegin) {
	if (width <= 0 || height <= 0) {
		hal_derr("hal_display_init_ssd1306_i2c: invalid size %dx%d", width, height);
		return false;
	}
	(void)i2c_bus;
	(void)i2c_addr;
	(void)rst_pin;
	(void)switchvcc;
	(void)periphBegin;
	s_width = width;
	s_height = height;
	return true;
}
#endif /* !HAL_DISABLE_SSD1306 */
bool hal_display_configure(int w, int h, uint8_t r, bool inv, bool bgr)        {
	if (w <= 0 || h <= 0) {
		hal_derr("hal_display_configure: invalid size %dx%d", w, h);
		return false;
	}
	s_width = w;
	s_height = h;
	(void)r;
	(void)inv;
	(void)bgr;
	return true;
}
void hal_display_soft_init(int delay_ms)                                        { (void)delay_ms; }
bool hal_display_set_rotation(uint8_t r)                                        { (void)r; return s_display_ready(); }
bool hal_display_invert(bool invert)                                            { (void)invert; return s_display_ready(); }
int  hal_display_get_width(void)                                                { return s_width; }
int  hal_display_get_height(void)                                               { return s_height; }
bool hal_display_fill_screen(uint16_t color)                                    { (void)color; return s_display_ready(); }
bool hal_display_flush(void)                                                     { return s_display_ready(); }
bool hal_display_draw_image(int x, int y, int w, int h, uint16_t background, uint16_t *data) {
	if (!data) {
		hal_derr("hal_display_draw_image: data pointer is NULL");
		return false;
	}
	bool ok = hal_display_fill_rect(x, y, w, h, background);
	ok &= hal_display_draw_rgb_bitmap(x, y, data, w, h);
	return ok;
}
bool hal_display_fill_rect(int x, int y, int w, int h, uint16_t color)         {
	if (!s_display_ready()) return false;
	if (w <= 0 || h <= 0) {
		hal_derr("hal_display_fill_rect: invalid size %dx%d", w, h);
		return false;
	}
	s_last_fill_rect_x = x;
	s_last_fill_rect_y = y;
	s_last_fill_rect_w = w;
	s_last_fill_rect_h = h;
	s_last_fill_rect_color = color;
	return true;
}
bool hal_display_draw_rect(int x, int y, int w, int h, uint16_t color)         {
	if (!s_display_ready()) return false;
	if (w <= 0 || h <= 0) {
		hal_derr("hal_display_draw_rect: invalid size %dx%d", w, h);
		return false;
	}
	(void)x;(void)y;(void)color;
	return true;
}
bool hal_display_fill_circle(int x, int y, int r, uint16_t color)              {
	if (!s_display_ready()) return false;
	if (r < 0) {
		hal_derr("hal_display_fill_circle: invalid radius %d", r);
		return false;
	}
	(void)x;(void)y;(void)color;
	return true;
}
bool hal_display_draw_circle(int x, int y, int r, uint16_t color)              {
	if (!s_display_ready()) return false;
	if (r < 0) {
		hal_derr("hal_display_draw_circle: invalid radius %d", r);
		return false;
	}
	(void)x;(void)y;(void)color;
	return true;
}
bool hal_display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
	if (!s_display_ready()) return false;
	if (w <= 0 || h <= 0 || r < 0) {
		hal_derr("hal_display_fill_round_rect: invalid size/radius w=%d h=%d r=%d", w, h, r);
		return false;
	}
	(void)x;(void)y;(void)color;
	return true;
}
bool hal_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color)    {
	if (!s_display_ready()) return false;
	(void)x0;(void)y0;(void)x1;(void)y1;(void)color;
	return true;
}
bool hal_display_draw_rgb_bitmap(int x, int y, uint16_t *data, int w, int h)  {
	if (!s_display_ready()) return false;
	if (!data) {
		hal_derr("hal_display_draw_rgb_bitmap: data pointer is NULL");
		return false;
	}
	if (w <= 0 || h <= 0) {
		hal_derr("hal_display_draw_rgb_bitmap: invalid size %dx%d", w, h);
		return false;
	}
	s_last_bitmap_x = x;
	s_last_bitmap_y = y;
	s_last_bitmap_w = w;
	s_last_bitmap_h = h;
	s_last_bitmap_data = data;
	return true;
}
bool hal_display_set_font(hal_font_id_t font)                                   {
	if (!s_display_ready()) return false;
	s_font = font;
	return true;
}
bool hal_display_set_text_color(uint16_t color)                                 {
	if (!s_display_ready()) return false;
	s_text_color = color;
	return true;
}
bool hal_display_set_text_size(uint8_t size)                                    {
	if (!s_display_ready()) return false;
	if (size == 0) {
		hal_derr("hal_display_set_text_size: size must be > 0");
		return false;
	}
	s_text_size = size;
	return true;
}
bool hal_display_set_cursor(int x, int y)                                       {
	if (!s_display_ready()) return false;
	s_cursor_x = x;
	s_cursor_y = y;
	return true;
}
bool hal_display_print(const char *s)                                           {
	if (!s_display_ready()) return false;
	if (!s) {
		hal_derr("hal_display_print: text pointer is NULL");
		return false;
	}
	copy_text(s_last_print, sizeof(s_last_print), s);
	return true;
}
bool hal_display_println(const char *s)                                         {
	if (!s_display_ready()) return false;
	if (!s) {
		hal_derr("hal_display_println: text pointer is NULL");
		return false;
	}
	copy_text(s_last_println, sizeof(s_last_println), s);
	return true;
}
bool hal_display_print_at(int x, int y, const char *s)                          {
	if (!s) {
		hal_derr("hal_display_print_at: text pointer is NULL");
		return false;
	}
	return hal_display_set_cursor(x, y) && hal_display_print(s);
}
bool hal_display_clear_text_line(int line_index, int line_height, uint16_t bg_color) {
	if (line_index < 0 || line_height <= 0) {
		hal_derr("hal_display_clear_text_line: invalid line/index line=%d height=%d", line_index, line_height);
		return false;
	}
	return hal_display_fill_rect(0, line_index * line_height, hal_display_get_width(), line_height, bg_color);
}
bool hal_display_print_line(int line_index, int line_height, const char *text,
									bool clear_first, uint16_t fg_color, uint16_t bg_color) {
	if (!text) {
		hal_derr("hal_display_print_line: text pointer is NULL");
		return false;
	}
	if (line_index < 0 || line_height <= 0) {
		hal_derr("hal_display_print_line: invalid line/index line=%d height=%d", line_index, line_height);
		return false;
	}
	bool ok = true;
	if (clear_first) {
		ok &= hal_display_clear_text_line(line_index, line_height, bg_color);
	}
	ok &= hal_display_set_text_color(fg_color);
	ok &= hal_display_print_at(0, line_index * line_height, text);
	return ok;
}
bool hal_display_draw_text_centered(const char *text, uint16_t fg_color,
									uint16_t bg_color, bool clear_first,
									bool flush_after) {
	if (!text) {
		hal_derr("hal_display_draw_text_centered: text pointer is NULL");
		return false;
	}
	if (!s_display_ready()) return false;
	bool ok = true;
	if (clear_first) {
		ok &= hal_display_fill_screen(bg_color);
	}
	int w = 0;
	int h = 0;
	ok &= hal_display_get_text_bounds(text, &w, &h);
	const int x = (hal_display_get_width() - w) / 2;
	const int y = (hal_display_get_height() - h) / 2;
	ok &= hal_display_set_text_color(fg_color);
	ok &= hal_display_print_at(x, y, text);
	if (flush_after) {
		ok &= hal_display_flush();
	}
	return ok;
}
bool hal_display_get_text_bounds(const char *s, int *w, int *h)                {
	int dummy_w = 0;
	int dummy_h = 0;
	int *out_w = w ? w : &dummy_w;
	int *out_h = h ? h : &dummy_h;

	if (!w && !h) {
		hal_derr("hal_display_get_text_bounds: both output pointers are NULL");
		return false;
	}
	if (!s) {
		hal_derr("hal_display_get_text_bounds: text pointer is NULL");
		*out_w = 0;
		*out_h = 0;
		return false;
	}
	if (!s_display_ready()) {
		*out_w = 0;
		*out_h = 0;
		return false;
	}
	int text_w = 0;
	text_w = (int)strlen(s) * 6;
	*out_w = text_w;
	*out_h = 8;
	return true;
}
int hal_display_text_width(const char *text)                                    {
	int w = 0;
	hal_display_get_text_bounds(text, &w, NULL);
	return w;
}
int hal_display_text_height(const char *text)                                   {
	int h = 0;
	hal_display_get_text_bounds(text, NULL, &h);
	return h;
}
bool hal_display_println_prepared_text(char *text)                              { return hal_display_println(text); }
bool hal_display_set_default_font(void)                                         {
	bool ok = true;
	ok &= hal_display_set_font(HAL_FONT_DEFAULT);
	ok &= hal_display_set_text_size(1);
	return ok;
}
bool hal_display_set_default_font_with_pos_and_color(int x, int y, uint16_t color) {
	bool ok = true;
	ok &= hal_display_set_default_font();
	ok &= hal_display_set_text_color(color);
	ok &= hal_display_set_cursor(x, y);
	return ok;
}
bool hal_display_set_text_size_one_with_color(uint16_t color)                   {
	bool ok = true;
	ok &= hal_display_set_text_size(1);
	ok &= hal_display_set_text_color(color);
	return ok;
}
bool hal_display_set_sans_bold_with_pos_and_color(int x, int y, uint16_t color){
	bool ok = true;
	ok &= hal_display_set_font(HAL_FONT_SANS_BOLD_9PT);
	ok &= hal_display_set_cursor(x, y);
	ok &= hal_display_set_text_size_one_with_color(color);
	return ok;
}
bool hal_display_set_serif9pt_with_color(uint16_t color)                        {
	bool ok = true;
	ok &= hal_display_set_font(HAL_FONT_SERIF_9PT);
	ok &= hal_display_set_text_size_one_with_color(color);
	return ok;
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
	int w = hal_display_prepare_text_v(display_txt, display_txt_size, format, args);
	va_end(args);
	return w;
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
}

const char *hal_mock_display_last_print(void) { return s_last_print; }

const char *hal_mock_display_last_println(void) { return s_last_println; }

hal_font_id_t hal_mock_display_get_font(void) { return s_font; }

uint16_t hal_mock_display_get_text_color(void) { return s_text_color; }

uint8_t hal_mock_display_get_text_size(void) { return s_text_size; }

void hal_mock_display_get_cursor(int *x, int *y) {
	if (x) *x = s_cursor_x;
	if (y) *y = s_cursor_y;
}

void hal_mock_display_get_last_fill_rect(int *x, int *y, int *w, int *h, uint16_t *color) {
	if (x) *x = s_last_fill_rect_x;
	if (y) *y = s_last_fill_rect_y;
	if (w) *w = s_last_fill_rect_w;
	if (h) *h = s_last_fill_rect_h;
	if (color) *color = s_last_fill_rect_color;
}

void hal_mock_display_get_last_bitmap(int *x, int *y, uint16_t **data, int *w, int *h) {
	if (x) *x = s_last_bitmap_x;
	if (y) *y = s_last_bitmap_y;
	if (data) *data = s_last_bitmap_data;
	if (w) *w = s_last_bitmap_w;
	if (h) *h = s_last_bitmap_h;
}
