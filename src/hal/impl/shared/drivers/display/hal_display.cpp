#include "hal/hal_target.h"
#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474

#include "hal/hal_config.h"
#ifdef HAL_ENABLE_DISPLAY

/*
 * Shared hal_display backend.
 *
 * A single implementation serving every register-level target (RP2040 and
 * STM32G474): all rendering goes through the portable GFX engine (jh_gfx) and
 * the shared panel drivers, which in turn talk to the panels exclusively over
 * the JaszczurHAL SPI / I2C / GPIO buses.  TFT panels (ILI9341, ST7735,
 * ST7789, ST7796S) are driven in immediate mode; the SSD1306 OLED is rendered
 * into an in-RAM framebuffer that hal_display_flush() pushes to the panel.
 */

#include "hal/hal_display.h"
#include "hal/hal_serial.h"
#include "hal/hal_sync.h"
#include "hal/hal_system.h"
#include "hal/impl/shared/hal_mutex_once.h"

#include "Fonts/FreeSansBold9pt7b.h"
#include "Fonts/FreeSerif9pt7b.h"
#include "jh_gfx.h"

#ifdef HAL_ENABLE_TFT
#include "hal/hal_spi.h"
#if defined(HAL_DISPLAY_ILI9341)
#include "ili9341_driver.h"
#elif defined(HAL_DISPLAY_ST7735) || defined(HAL_DISPLAY_ST7789) ||            \
    defined(HAL_DISPLAY_ST7796S)
#include "st77xx_driver.h"
#else
#error                                                                         \
    "hal_display requires a supported HAL_DISPLAY_* TFT backend when HAL_ENABLE_TFT is set."
#endif
#endif /* HAL_ENABLE_TFT */

#ifdef HAL_ENABLE_SSD1306
#include "hal/hal_i2c.h"
#include "ssd1306_driver.h"
#include <stdlib.h>
#endif /* HAL_ENABLE_SSD1306 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- Backend selection --------------------------------------------------- */

typedef enum {
  DISPLAY_BACKEND_NONE = 0,
  DISPLAY_BACKEND_TFT,
  DISPLAY_BACKEND_SSD1306,
} display_backend_t;

static display_backend_t s_backend = DISPLAY_BACKEND_NONE;
static int s_width = 0;
static int s_height = 0;

/* ---- TFT state ----------------------------------------------------------- */

#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
static jh_ili9341_t s_tft = {};
#elif defined(HAL_DISPLAY_ST7735) || defined(HAL_DISPLAY_ST7789) ||            \
    defined(HAL_DISPLAY_ST7796S)
static jh_st77xx_t s_tft = {};
static jh_st77xx_config_t s_tft_config = {};
static bool s_tft_pins_configured = false;
#endif
static bool s_tft_ready = false;
static bool s_tft_stream_active = false;

static bool draw_pixel_unlocked(int x, int y, uint16_t color);
static bool fill_rect_unlocked(int x, int y, int w, int h, uint16_t color);

/* GFX engine subclass that delegates pixel/rect output to the TFT driver. */
class TftGfx : public JHGfx {
public:
  TftGfx() : JHGfx(1, 1) {}

  void configure(int16_t w, int16_t h) {
    WIDTH = w;
    HEIGHT = h;
    _width = w;
    _height = h;
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    draw_pixel_unlocked(x, y, color);
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override {
    fill_rect_unlocked(x, y, w, h, color);
  }

  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                     uint16_t color) override {
    fill_rect_unlocked(x, y, w, h, color);
  }
};

static TftGfx s_tft_gfx;
#endif /* HAL_ENABLE_TFT */

/* ---- SSD1306 state ------------------------------------------------------- */

#ifdef HAL_ENABLE_SSD1306
static jh_ssd1306_t s_oled = {};

/* GFX engine subclass backed by an in-RAM monochrome framebuffer. */
class Ssd1306Gfx : public JHGfx {
public:
  Ssd1306Gfx() : JHGfx(1, 1), buffer(NULL), buffer_bytes(0u) {}

  bool allocate(int16_t w, int16_t h) {
    release();
    WIDTH = w;
    HEIGHT = h;
    _width = w;
    _height = h;
    rotation = 0u;
    buffer_bytes = (size_t)w * (((size_t)h + 7u) / 8u);
    buffer = (uint8_t *)malloc(buffer_bytes);
    if (buffer == NULL) {
      buffer_bytes = 0u;
      return false;
    }
    memset(buffer, 0, buffer_bytes);
    return true;
  }

  void release() {
    if (buffer != NULL) {
      free(buffer);
      buffer = NULL;
    }
    buffer_bytes = 0u;
  }

  uint8_t *data() const { return buffer; }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (buffer == NULL || x < 0 || x >= _width || y < 0 || y >= _height) {
      return;
    }
    /* Map the rotated logical coordinate back into the raw framebuffer. */
    switch (rotation) {
    case 1: {
      int16_t t = x;
      x = y;
      y = t;
      x = WIDTH - x - 1;
      break;
    }
    case 2:
      x = WIDTH - x - 1;
      y = HEIGHT - y - 1;
      break;
    case 3: {
      int16_t t = x;
      x = y;
      y = t;
      y = HEIGHT - y - 1;
      break;
    }
    default:
      break;
    }
    uint8_t *p = &buffer[x + (y / 8) * WIDTH];
    switch (color) {
    case JH_SSD1306_WHITE:
      *p |= (uint8_t)(1u << (y & 7));
      break;
    case JH_SSD1306_BLACK:
      *p &= (uint8_t) ~(1u << (y & 7));
      break;
    case JH_SSD1306_INVERSE:
      *p ^= (uint8_t)(1u << (y & 7));
      break;
    default:
      break;
    }
  }

  void fillScreen(uint16_t color) override {
    if (buffer == NULL) {
      return;
    }
    if (color == JH_SSD1306_WHITE || color == JH_SSD1306_BLACK) {
      memset(buffer, color == JH_SSD1306_WHITE ? 0xFF : 0x00, buffer_bytes);
      return;
    }
    JHGfx::fillScreen(color);
  }

private:
  uint8_t *buffer;
  size_t buffer_bytes;
};

static Ssd1306Gfx s_oled_gfx;

static inline uint16_t oled_color(uint16_t color) {
  return (color == HAL_COLOR_BLACK) ? JH_SSD1306_BLACK : JH_SSD1306_WHITE;
}
#endif /* HAL_ENABLE_SSD1306 */

/* ---- Shared text state --------------------------------------------------- */

static hal_font_id_t s_font = HAL_FONT_DEFAULT;
static uint16_t s_text_color = HAL_COLOR_WHITE;
static uint8_t s_text_size = 1u;
static int s_cursor_x = 0;
static int s_cursor_y = 0;

/* ---- Multicore-safe display mutex ---------------------------------------- */

static hal_mutex_t s_display_mutex = NULL;

static void display_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_display_mutex);
}

struct DisplayLock {
  DisplayLock() {
    display_ensure_mutex();
    hal_mutex_lock(s_display_mutex);
  }
  ~DisplayLock() { hal_mutex_unlock(s_display_mutex); }
};

/* ---- Backend predicates -------------------------------------------------- */

static inline bool using_tft(void) {
#ifdef HAL_ENABLE_TFT
  return s_backend == DISPLAY_BACKEND_TFT && s_tft_ready;
#else
  return false;
#endif
}

static inline bool using_oled(void) {
#ifdef HAL_ENABLE_SSD1306
  return s_backend == DISPLAY_BACKEND_SSD1306 && s_oled.initialized;
#else
  return false;
#endif
}

static inline bool has_active_display(void) {
  return using_oled() || using_tft();
}

static bool ensure_display_ready(const char *fn) {
  if (has_active_display()) {
    return true;
  }
  hal_derr("%s: display backend is not initialized", fn);
  return false;
}

static JHGfx *active_gfx(void) {
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    return &s_oled_gfx;
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    return &s_tft_gfx;
  }
#endif
  return NULL;
}

/* ---- TFT primitives (immediate mode) ------------------------------------- */

#ifdef HAL_ENABLE_TFT
static int iabs_int(int v) { return v < 0 ? -v : v; }

static bool tft_fill_rect_driver(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                 uint16_t color) {
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_fill_rect(&s_tft, x, y, w, h, color);
#else
  return jh_st77xx_fill_rect(&s_tft, x, y, w, h, color);
#endif
}

static bool tft_draw_rgb_bitmap_driver(uint16_t x, uint16_t y,
                                       const uint16_t *pixels, uint16_t w,
                                       uint16_t h) {
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_draw_rgb_bitmap(&s_tft, x, y, pixels, w, h);
#else
  return jh_st77xx_draw_rgb_bitmap(&s_tft, x, y, pixels, w, h);
#endif
}

static bool tft_begin_write_driver(uint16_t x, uint16_t y, uint16_t w,
                                   uint16_t h) {
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_begin_write(&s_tft, x, y, w, h);
#else
  return jh_st77xx_begin_write(&s_tft, x, y, w, h);
#endif
}

static bool tft_write_pixels_fast_driver(const uint16_t *pixels, size_t count) {
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_write_pixels_fast(&s_tft, pixels, count);
#else
  return jh_st77xx_write_pixels_fast(&s_tft, pixels, count);
#endif
}

static bool tft_write_pixels_be_driver(const uint8_t *pixels_be,
                                       size_t byte_count) {
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_write_pixels_be(&s_tft, pixels_be, byte_count);
#else
  return jh_st77xx_write_pixels_be(&s_tft, pixels_be, byte_count);
#endif
}

static bool tft_write_pixels_dma_driver(const uint8_t *pixels_be,
                                        size_t byte_count) {
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_write_pixels_dma(&s_tft, pixels_be, byte_count);
#else
  return jh_st77xx_write_pixels_dma(&s_tft, pixels_be, byte_count);
#endif
}

static bool tft_end_write_driver(void) {
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_end_write(&s_tft);
#else
  return jh_st77xx_end_write(&s_tft);
#endif
}

static bool tft_set_rotation_driver(uint8_t rotation) {
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_set_rotation(&s_tft, rotation);
#else
  return jh_st77xx_set_rotation(&s_tft, rotation);
#endif
}

static bool tft_invert_driver(bool invert) {
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_invert(&s_tft, invert);
#else
  return jh_st77xx_invert(&s_tft, invert);
#endif
}

static bool tft_soft_init_driver(uint32_t delay_ms) {
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_soft_init(&s_tft, delay_ms);
#else
  (void)delay_ms;
  return jh_st77xx_soft_init(&s_tft);
#endif
}

static uint16_t tft_native_width(void) { return s_tft.width; }
static uint16_t tft_native_height(void) { return s_tft.height; }

static bool draw_pixel_unlocked(int x, int y, uint16_t color) {
  if (x < 0 || y < 0 || x >= s_width || y >= s_height) {
    return true;
  }
  return tft_fill_rect_driver((uint16_t)x, (uint16_t)y, 1u, 1u, color);
}

static bool fill_rect_unlocked(int x, int y, int w, int h, uint16_t color) {
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x >= s_width || y >= s_height || w <= 0 || h <= 0) {
    return true;
  }
  if (x + w > s_width) {
    w = s_width - x;
  }
  if (y + h > s_height) {
    h = s_height - y;
  }
  if (w <= 0 || h <= 0) {
    return true;
  }
  return tft_fill_rect_driver((uint16_t)x, (uint16_t)y, (uint16_t)w,
                              (uint16_t)h, color);
}

static bool draw_line_unlocked(int x0, int y0, int x1, int y1, uint16_t color) {
  const int dx = iabs_int(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -iabs_int(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  bool ok = true;

  for (;;) {
    ok &= draw_pixel_unlocked(x0, y0, color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
  return ok;
}

static bool draw_circle_unlocked(int x0, int y0, int r, uint16_t color) {
  int f = 1 - r;
  int ddx = 1;
  int ddy = -2 * r;
  int x = 0;
  int y = r;
  bool ok = true;

  ok &= draw_pixel_unlocked(x0, y0 + r, color);
  ok &= draw_pixel_unlocked(x0, y0 - r, color);
  ok &= draw_pixel_unlocked(x0 + r, y0, color);
  ok &= draw_pixel_unlocked(x0 - r, y0, color);

  while (x < y) {
    if (f >= 0) {
      y--;
      ddy += 2;
      f += ddy;
    }
    x++;
    ddx += 2;
    f += ddx;

    ok &= draw_pixel_unlocked(x0 + x, y0 + y, color);
    ok &= draw_pixel_unlocked(x0 - x, y0 + y, color);
    ok &= draw_pixel_unlocked(x0 + x, y0 - y, color);
    ok &= draw_pixel_unlocked(x0 - x, y0 - y, color);
    ok &= draw_pixel_unlocked(x0 + y, y0 + x, color);
    ok &= draw_pixel_unlocked(x0 - y, y0 + x, color);
    ok &= draw_pixel_unlocked(x0 + y, y0 - x, color);
    ok &= draw_pixel_unlocked(x0 - y, y0 - x, color);
  }
  return ok;
}

static bool fill_circle_unlocked(int x0, int y0, int r, uint16_t color) {
  const int rr = r * r;
  bool ok = true;
  for (int yy = -r; yy <= r; ++yy) {
    int xx = r;
    while (xx > 0 && (xx * xx + yy * yy) > rr) {
      --xx;
    }
    ok &= fill_rect_unlocked(x0 - xx, y0 + yy, xx * 2 + 1, 1, color);
  }
  return ok;
}

static bool fill_round_rect_unlocked(int x, int y, int w, int h, int r,
                                     uint16_t color) {
  if (r <= 0) {
    return fill_rect_unlocked(x, y, w, h, color);
  }
  const int max_r = (w < h ? w : h) / 2;
  if (r > max_r) {
    r = max_r;
  }
  const int rr = r * r;
  bool ok = true;
  for (int row = 0; row < h; ++row) {
    int inset = 0;
    if (row < r) {
      const int dy = r - row;
      int dx = r;
      while (dx > 0 && (dx * dx + dy * dy) > rr) {
        --dx;
      }
      inset = r - dx;
    } else if (row >= h - r) {
      const int dy = row - (h - r - 1);
      int dx = r;
      while (dx > 0 && (dx * dx + dy * dy) > rr) {
        --dx;
      }
      inset = r - dx;
    }
    ok &= fill_rect_unlocked(x + inset, y + row, w - inset * 2, 1, color);
  }
  return ok;
}
#endif /* HAL_ENABLE_TFT */

/* ---- Init / control ------------------------------------------------------ */

#ifdef HAL_ENABLE_TFT
void hal_display_init(uint8_t cs, uint8_t dc, uint8_t rst) {
  DisplayLock guard;
  s_backend = DISPLAY_BACKEND_TFT;
#if defined(HAL_DISPLAY_ILI9341)
  jh_ili9341_config_t config = {};
  config.bus = 0u;
  config.cs_pin = cs == 0xFFu ? -1 : (int16_t)cs;
  config.dc_pin = dc == 0xFFu ? -1 : (int16_t)dc;
  config.rst_pin = rst == 0xFFu ? -1 : (int16_t)rst;
  config.clock_hz = JH_ILI9341_SPI_DEFAULT_HZ;
  s_tft_ready = jh_ili9341_init(&s_tft, &config);
  if (!s_tft_ready) {
    hal_derr("hal_display_init: ILI9341 init failed");
  }
#else
  memset(&s_tft, 0, sizeof(s_tft));
  memset(&s_tft_config, 0, sizeof(s_tft_config));
  s_tft_config.bus = 0u;
  s_tft_config.cs_pin = cs == 0xFFu ? -1 : (int16_t)cs;
  s_tft_config.dc_pin = dc == 0xFFu ? -1 : (int16_t)dc;
  s_tft_config.rst_pin = rst == 0xFFu ? -1 : (int16_t)rst;
  s_tft_config.clock_hz = JH_ST77XX_SPI_DEFAULT_HZ;
  s_tft_config.spi_mode = HAL_SPI_MODE0;
  s_tft_config.st7735_tab = JH_ST7735_TAB_BLACKTAB;
  s_tft_pins_configured = true;
  s_tft_ready = false;
#endif
  s_width = 0;
  s_height = 0;
}
#endif /* HAL_ENABLE_TFT */

#ifdef HAL_ENABLE_SSD1306
bool hal_display_init_ssd1306_i2c(int width, int height, uint8_t i2c_addr,
                                  int8_t rst_pin, uint8_t switchvcc,
                                  bool periphBegin) {
  return hal_display_init_ssd1306_i2c_ex(width, height, 0u, i2c_addr, rst_pin,
                                         switchvcc, periphBegin);
}

bool hal_display_init_ssd1306_i2c_ex(int width, int height, uint8_t i2c_bus,
                                     uint8_t i2c_addr, int8_t rst_pin,
                                     uint8_t switchvcc, bool periphBegin) {
  DisplayLock guard;
  /* The HAL I2C bus is initialised lazily on the first transaction, so the
   * Arduino-era periphBegin flag has no direct equivalent here. */
  (void)periphBegin;

  if (width <= 0 || height <= 0) {
    hal_derr("hal_display_init_ssd1306_i2c: invalid size %dx%d", width, height);
    return false;
  }
  if (!s_oled_gfx.allocate((int16_t)width, (int16_t)height)) {
    hal_derr("hal_display_init_ssd1306_i2c: framebuffer allocation failed");
    return false;
  }

  jh_ssd1306_config_t config = {};
  config.bus = i2c_bus;
  config.i2c_addr = i2c_addr;
  config.width = (uint16_t)width;
  config.height = (uint16_t)height;
  // rst_pin is intentionally signed (int8_t, -1 means "not connected").
  // NOLINTNEXTLINE(bugprone-signed-char-misuse,cert-str34-c)
  config.rst_pin = rst_pin;
  config.vccstate = switchvcc;
  config.clock_hz = JH_SSD1306_DEFAULT_I2C_HZ;

  if (!jh_ssd1306_init(&s_oled, &config)) {
    s_oled_gfx.release();
    s_backend = DISPLAY_BACKEND_NONE;
    s_width = 0;
    s_height = 0;
    hal_derr("hal_display_init_ssd1306_i2c: SSD1306 init failed");
    return false;
  }

  s_backend = DISPLAY_BACKEND_SSD1306;
  s_oled_gfx.setRotation(0u);
  s_oled_gfx.fillScreen(JH_SSD1306_BLACK);
  s_width = s_oled_gfx.width();
  s_height = s_oled_gfx.height();
  return jh_ssd1306_display(&s_oled, s_oled_gfx.data());
}
#endif /* HAL_ENABLE_SSD1306 */

bool hal_display_configure(int width, int height, uint8_t rotation, bool invert,
                           bool bgr) {
  DisplayLock guard;
  if (width <= 0 || height <= 0) {
    hal_derr("hal_display_configure: invalid size %dx%d", width, height);
    return false;
  }

#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.setRotation(rotation);
    if (!jh_ssd1306_invert(&s_oled, invert)) {
      return false;
    }
    (void)bgr;
    s_width = s_oled_gfx.width();
    s_height = s_oled_gfx.height();
    return true;
  }
#endif

#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  if (!using_tft()) {
    hal_derr("hal_display_configure: TFT backend is not initialized");
    return false;
  }
  (void)bgr;
  if (!tft_invert_driver(invert) || !tft_set_rotation_driver(rotation)) {
    return false;
  }
#else
  if (!s_tft_pins_configured) {
    hal_derr("hal_display_configure: TFT pins are not initialized");
    return false;
  }
  s_tft_config.width = (uint16_t)width;
  s_tft_config.height = (uint16_t)height;
  s_tft_config.bgr = bgr;
#if defined(HAL_DISPLAY_ST7735)
  s_tft_config.chip = JH_ST77XX_CHIP_ST7735;
#ifdef HAL_DISPLAY_ST7735_TAB
  s_tft_config.st7735_tab = HAL_DISPLAY_ST7735_TAB;
#else
  s_tft_config.st7735_tab = JH_ST7735_TAB_BLACKTAB;
#endif
#elif defined(HAL_DISPLAY_ST7789)
  s_tft_config.chip = JH_ST77XX_CHIP_ST7789;
#elif defined(HAL_DISPLAY_ST7796S)
  s_tft_config.chip = JH_ST77XX_CHIP_ST7796S;
#endif
  s_tft_ready = jh_st77xx_init(&s_tft, &s_tft_config);
  if (!s_tft_ready) {
    hal_derr("hal_display_configure: ST77xx init failed");
    return false;
  }
  if (!tft_invert_driver(invert) || !tft_set_rotation_driver(rotation)) {
    s_tft_ready = false;
    return false;
  }
#endif
  s_width = (int)tft_native_width();
  s_height = (int)tft_native_height();
  s_tft_gfx.configure((int16_t)s_width, (int16_t)s_height);
  return true;
#else
  (void)rotation;
  (void)invert;
  (void)bgr;
  hal_derr("hal_display_configure: no active display backend");
  return false;
#endif /* HAL_ENABLE_TFT */
}

void hal_display_soft_init(int delay_ms) {
  DisplayLock guard;
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    (void)tft_soft_init_driver(delay_ms > 0 ? (uint32_t)delay_ms : 0u);
    return;
  }
#endif
  (void)delay_ms;
}

bool hal_display_set_rotation(uint8_t r) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_set_rotation")) {
    return false;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.setRotation(r);
    s_width = s_oled_gfx.width();
    s_height = s_oled_gfx.height();
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    if (!tft_set_rotation_driver(r)) {
      return false;
    }
    s_width = (int)tft_native_width();
    s_height = (int)tft_native_height();
    s_tft_gfx.configure((int16_t)s_width, (int16_t)s_height);
    return true;
  }
#endif
  return false;
}

bool hal_display_invert(bool invert) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_invert")) {
    return false;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    return jh_ssd1306_invert(&s_oled, invert);
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    return tft_invert_driver(invert);
  }
#endif
  return false;
}

int hal_display_get_width(void) { return s_width; }
int hal_display_get_height(void) { return s_height; }

/* ---- Screen -------------------------------------------------------------- */

bool hal_display_fill_screen(uint16_t color) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_fill_screen")) {
    return false;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.fillScreen(oled_color(color));
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    if (s_width <= 0 || s_height <= 0) {
      hal_derr("hal_display_fill_screen: display is not configured");
      return false;
    }
    return fill_rect_unlocked(0, 0, s_width, s_height, color);
  }
#endif
  return false;
}

bool hal_display_flush(void) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_flush")) {
    return false;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    return jh_ssd1306_display(&s_oled, s_oled_gfx.data());
  }
#endif
  return true;
}

bool hal_display_draw_image(int x, int y, int w, int h, uint16_t background,
                            uint16_t *data) {
  bool ok = hal_display_fill_rect(x, y, w, h, background);
  ok &= hal_display_draw_rgb_bitmap(x, y, data, w, h);
  return ok;
}

/* ---- Geometry ------------------------------------------------------------ */

bool hal_display_fill_rect(int x, int y, int w, int h, uint16_t color) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_fill_rect")) {
    return false;
  }
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_fill_rect: invalid size %dx%d", w, h);
    return false;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.fillRect(x, y, w, h, oled_color(color));
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    return fill_rect_unlocked(x, y, w, h, color);
  }
#endif
  return false;
}

bool hal_display_draw_rect(int x, int y, int w, int h, uint16_t color) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_draw_rect")) {
    return false;
  }
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_draw_rect: invalid size %dx%d", w, h);
    return false;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.drawRect(x, y, w, h, oled_color(color));
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    bool ok = true;
    ok &= fill_rect_unlocked(x, y, w, 1, color);
    ok &= fill_rect_unlocked(x, y + h - 1, w, 1, color);
    ok &= fill_rect_unlocked(x, y, 1, h, color);
    ok &= fill_rect_unlocked(x + w - 1, y, 1, h, color);
    return ok;
  }
#endif
  return false;
}

bool hal_display_fill_circle(int x, int y, int r, uint16_t color) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_fill_circle")) {
    return false;
  }
  if (r < 0) {
    hal_derr("hal_display_fill_circle: invalid radius %d", r);
    return false;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.fillCircle(x, y, r, oled_color(color));
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    return fill_circle_unlocked(x, y, r, color);
  }
#endif
  return false;
}

bool hal_display_draw_circle(int x, int y, int r, uint16_t color) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_draw_circle")) {
    return false;
  }
  if (r < 0) {
    hal_derr("hal_display_draw_circle: invalid radius %d", r);
    return false;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.drawCircle(x, y, r, oled_color(color));
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    return draw_circle_unlocked(x, y, r, color);
  }
#endif
  return false;
}

bool hal_display_fill_round_rect(int x, int y, int w, int h, int r,
                                 uint16_t color) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_fill_round_rect")) {
    return false;
  }
  if (w <= 0 || h <= 0 || r < 0) {
    hal_derr("hal_display_fill_round_rect: invalid size/radius w=%d h=%d r=%d",
             w, h, r);
    return false;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.fillRoundRect(x, y, w, h, r, oled_color(color));
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    return fill_round_rect_unlocked(x, y, w, h, r, color);
  }
#endif
  return false;
}

bool hal_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_draw_line")) {
    return false;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.drawLine(x0, y0, x1, y1, oled_color(color));
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    return draw_line_unlocked(x0, y0, x1, y1, color);
  }
#endif
  return false;
}

/* ---- Bitmap -------------------------------------------------------------- */

bool hal_display_draw_rgb_bitmap(int x, int y, uint16_t *data, int w, int h) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_draw_rgb_bitmap")) {
    return false;
  }
  if (!data) {
    hal_derr("hal_display_draw_rgb_bitmap: data pointer is NULL");
    return false;
  }
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_draw_rgb_bitmap: invalid size %dx%d", w, h);
    return false;
  }
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    if (x < 0 || y < 0 || x + w > s_width || y + h > s_height) {
      hal_derr("hal_display_draw_rgb_bitmap: out-of-bounds clipping is not "
               "supported");
      return false;
    }
    return tft_draw_rgb_bitmap_driver((uint16_t)x, (uint16_t)y, data,
                                      (uint16_t)w, (uint16_t)h);
  }
#else
  (void)x;
  (void)y;
#endif
  hal_derr("hal_display_draw_rgb_bitmap: RGB bitmaps are unsupported on "
           "current backend");
  return false;
}

bool hal_display_begin_write(int x, int y, int w, int h) {
#ifdef HAL_ENABLE_TFT
  display_ensure_mutex();
  hal_mutex_lock(s_display_mutex);

  if (s_tft_stream_active) {
    hal_derr("hal_display_begin_write: stream already active");
    hal_mutex_unlock(s_display_mutex);
    return false;
  }
  if (!ensure_display_ready("hal_display_begin_write")) {
    hal_mutex_unlock(s_display_mutex);
    return false;
  }
  if (!using_tft()) {
    hal_derr("hal_display_begin_write: supported only by TFT backends");
    hal_mutex_unlock(s_display_mutex);
    return false;
  }
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_begin_write: invalid size %dx%d", w, h);
    hal_mutex_unlock(s_display_mutex);
    return false;
  }
  if (x < 0 || y < 0 || x > s_width - w || y > s_height - h) {
    hal_derr("hal_display_begin_write: out-of-bounds clipping is not "
             "supported");
    hal_mutex_unlock(s_display_mutex);
    return false;
  }

  if (!tft_begin_write_driver((uint16_t)x, (uint16_t)y, (uint16_t)w,
                              (uint16_t)h)) {
    hal_mutex_unlock(s_display_mutex);
    return false;
  }

  s_tft_stream_active = true;
  return true;
#else
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  hal_derr("hal_display_begin_write: TFT backend is not enabled");
  return false;
#endif
}

static bool ensure_tft_stream_ready(const char *fn) {
#ifdef HAL_ENABLE_TFT
  if (s_tft_stream_active && using_tft()) {
    return true;
  }
#endif
  hal_derr("%s: no active TFT write stream", fn);
  return false;
}

bool hal_display_write_pixels_fast(const uint16_t *pixels, size_t count) {
  if ((pixels == NULL && count > 0u) ||
      !ensure_tft_stream_ready("hal_display_write_pixels_fast")) {
    return false;
  }
#ifdef HAL_ENABLE_TFT
  return tft_write_pixels_fast_driver(pixels, count);
#else
  (void)pixels;
  (void)count;
  return false;
#endif
}

bool hal_display_write_pixels_be(const uint8_t *pixels_be, size_t byte_count) {
  if ((pixels_be == NULL && byte_count > 0u) || (byte_count & 1u) != 0u ||
      !ensure_tft_stream_ready("hal_display_write_pixels_be")) {
    return false;
  }
#ifdef HAL_ENABLE_TFT
  return tft_write_pixels_be_driver(pixels_be, byte_count);
#else
  (void)pixels_be;
  (void)byte_count;
  return false;
#endif
}

bool hal_display_write_pixels_dma(const uint8_t *pixels_be, size_t byte_count) {
  if ((pixels_be == NULL && byte_count > 0u) || (byte_count & 1u) != 0u ||
      !ensure_tft_stream_ready("hal_display_write_pixels_dma")) {
    return false;
  }
#ifdef HAL_ENABLE_TFT
  return tft_write_pixels_dma_driver(pixels_be, byte_count);
#else
  (void)pixels_be;
  (void)byte_count;
  return false;
#endif
}

bool hal_display_end_write(void) {
#ifdef HAL_ENABLE_TFT
  if (!s_tft_stream_active) {
    hal_derr("hal_display_end_write: no active TFT write stream");
    return false;
  }

  const bool ok = tft_end_write_driver();
  s_tft_stream_active = false;
  hal_mutex_unlock(s_display_mutex);
  return ok;
#else
  hal_derr("hal_display_end_write: TFT backend is not enabled");
  return false;
#endif
}

/* ---- Text ---------------------------------------------------------------- */

static void apply_font_to(JHGfx *gfx, hal_font_id_t font) {
  switch (font) {
  case HAL_FONT_SANS_BOLD_9PT:
    gfx->setFont(&FreeSansBold9pt7b);
    break;
  case HAL_FONT_SERIF_9PT:
    gfx->setFont(&FreeSerif9pt7b);
    break;
  default:
    gfx->setFont(NULL);
    break;
  }
}

bool hal_display_set_font(hal_font_id_t font) {
  DisplayLock guard;
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    return ensure_display_ready("hal_display_set_font");
  }
  s_font = font;
  apply_font_to(gfx, font);
  return true;
}

bool hal_display_set_text_color(uint16_t color) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_set_text_color")) {
    return false;
  }
  s_text_color = color;
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.setTextColor(oled_color(color));
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
  if (using_tft()) {
    s_tft_gfx.setTextColor(color);
    return true;
  }
#endif
  return false;
}

bool hal_display_set_text_size(uint8_t size) {
  DisplayLock guard;
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    return ensure_display_ready("hal_display_set_text_size");
  }
  if (size == 0u) {
    hal_derr("hal_display_set_text_size: size must be > 0");
    return false;
  }
  s_text_size = size;
  gfx->setTextSize(size);
  return true;
}

bool hal_display_set_cursor(int x, int y) {
  DisplayLock guard;
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    return ensure_display_ready("hal_display_set_cursor");
  }
  s_cursor_x = x;
  s_cursor_y = y;
  gfx->setCursor((int16_t)x, (int16_t)y);
  return true;
}

bool hal_display_print(const char *s) {
  DisplayLock guard;
  if (!s) {
    hal_derr("hal_display_print: text pointer is NULL");
    return false;
  }
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    return ensure_display_ready("hal_display_print");
  }
  gfx->print(s);
  s_cursor_x = gfx->getCursorX();
  s_cursor_y = gfx->getCursorY();
  return true;
}

bool hal_display_println(const char *s) {
  DisplayLock guard;
  if (!s) {
    hal_derr("hal_display_println: text pointer is NULL");
    return false;
  }
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    return ensure_display_ready("hal_display_println");
  }
  gfx->print(s);
  gfx->write((uint8_t)'\n');
  s_cursor_x = gfx->getCursorX();
  s_cursor_y = gfx->getCursorY();
  return true;
}

bool hal_display_print_at(int x, int y, const char *s) {
  if (!s) {
    hal_derr("hal_display_print_at: text pointer is NULL");
    return false;
  }
  return hal_display_set_cursor(x, y) && hal_display_print(s);
}

bool hal_display_clear_text_line(int line_index, int line_height,
                                 uint16_t bg_color) {
  if (line_index < 0 || line_height <= 0) {
    hal_derr(
        "hal_display_clear_text_line: invalid line/index line=%d height=%d",
        line_index, line_height);
    return false;
  }
  const int width = hal_display_get_width();
  if (width <= 0) {
    hal_derr("hal_display_clear_text_line: display width is not configured");
    return false;
  }
  return hal_display_fill_rect(0, line_index * line_height, width, line_height,
                               bg_color);
}

bool hal_display_print_line(int line_index, int line_height, const char *text,
                            bool clear_first, uint16_t fg_color,
                            uint16_t bg_color) {
  if (!text) {
    hal_derr("hal_display_print_line: text pointer is NULL");
    return false;
  }
  if (line_index < 0 || line_height <= 0) {
    hal_derr("hal_display_print_line: invalid line/index line=%d height=%d",
             line_index, line_height);
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
  if (!ensure_display_ready("hal_display_draw_text_centered")) {
    return false;
  }
  bool ok = true;
  if (clear_first) {
    ok &= hal_display_fill_screen(bg_color);
  }
  int w = 0;
  int h = 0;
  ok &= hal_display_get_text_bounds(text, &w, &h);
  ok &= hal_display_set_text_color(fg_color);
  ok &= hal_display_print_at((hal_display_get_width() - w) / 2,
                             (hal_display_get_height() - h) / 2, text);
  if (flush_after) {
    ok &= hal_display_flush();
  }
  return ok;
}

bool hal_display_get_text_bounds(const char *s, int *w, int *h) {
  DisplayLock guard;
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
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    *out_w = 0;
    *out_h = 0;
    return ensure_display_ready("hal_display_get_text_bounds");
  }

  int16_t x1, y1;
  uint16_t bw, bh;
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &bw, &bh);
  *out_w = (int)bw;
  *out_h = (int)bh;
  return true;
}

int hal_display_text_width(const char *text) {
  int w = 0;
  hal_display_get_text_bounds(text, &w, NULL);
  return w;
}

int hal_display_text_height(const char *text) {
  int h = 0;
  hal_display_get_text_bounds(text, NULL, &h);
  return h;
}

bool hal_display_println_prepared_text(char *text) {
  return hal_display_println(text);
}

bool hal_display_set_default_font(void) {
  bool ok = true;
  ok &= hal_display_set_font(HAL_FONT_DEFAULT);
  ok &= hal_display_set_text_size(1u);
  return ok;
}

bool hal_display_set_default_font_with_pos_and_color(int x, int y,
                                                     uint16_t color) {
  bool ok = true;
  ok &= hal_display_set_default_font();
  ok &= hal_display_set_text_color(color);
  ok &= hal_display_set_cursor(x, y);
  return ok;
}

bool hal_display_set_text_size_one_with_color(uint16_t color) {
  bool ok = true;
  ok &= hal_display_set_text_size(1u);
  ok &= hal_display_set_text_color(color);
  return ok;
}

bool hal_display_set_sans_bold_with_pos_and_color(int x, int y,
                                                  uint16_t color) {
  bool ok = true;
  ok &= hal_display_set_font(HAL_FONT_SANS_BOLD_9PT);
  ok &= hal_display_set_cursor(x, y);
  ok &= hal_display_set_text_size_one_with_color(color);
  return ok;
}

bool hal_display_set_serif9pt_with_color(uint16_t color) {
  bool ok = true;
  ok &= hal_display_set_font(HAL_FONT_SERIF_9PT);
  ok &= hal_display_set_text_size_one_with_color(color);
  return ok;
}

int hal_display_prepare_text_v(char *display_txt, size_t display_txt_size,
                               const char *format, va_list args) {
  if (!display_txt || display_txt_size == 0u || !format) {
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
  const int w =
      hal_display_prepare_text_v(display_txt, display_txt_size, format, args);
  va_end(args);
  return w;
}

#endif /* HAL_ENABLE_DISPLAY */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
