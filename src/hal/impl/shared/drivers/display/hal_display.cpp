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
 * the JaszczurHAL SPI / I2C / GPIO buses. TFT panels (ILI9341, ST7735,
 * ST7789, ST7796S, GC9A01) are driven in immediate mode; the SSD1306 OLED is
 * rendered into an in-RAM framebuffer that hal_display_flush() pushes to the
 * panel.
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
    defined(HAL_DISPLAY_ST7796S) || defined(HAL_DISPLAY_GC9A01)
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

#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
#include "rgb_oled_driver.h"
#endif

#ifdef HAL_ENABLE_ST7567
#include "st7567_driver.h"
#endif

#ifdef HAL_ENABLE_SSD16XX
#include "ssd16xx_driver.h"
#endif

#ifdef HAL_ENABLE_UC81XX
#include "uc81xx_driver.h"
#endif

#if defined(HAL_ENABLE_TFT) || defined(HAL_ENABLE_SSD1331) ||                  \
    defined(HAL_ENABLE_SSD135X)
#define JH_DISPLAY_HAS_IMMEDIATE_RGB 1
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- Backend selection --------------------------------------------------- */

typedef enum {
  DISPLAY_BACKEND_NONE = 0,
  DISPLAY_BACKEND_TFT,
  DISPLAY_BACKEND_SSD1306,
  DISPLAY_BACKEND_RGB_OLED,
  DISPLAY_BACKEND_ST7567,
  DISPLAY_BACKEND_SSD16XX,
  DISPLAY_BACKEND_UC81XX,
} display_backend_t;

static display_backend_t s_backend = DISPLAY_BACKEND_NONE;
static int s_width = 0;
static int s_height = 0;
static uint8_t s_rotation = HAL_DISPLAY_ROTATION_0;

/* ---- TFT state ----------------------------------------------------------- */

#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
static jh_ili9341_t s_tft = {};
#elif defined(HAL_DISPLAY_ST7735) || defined(HAL_DISPLAY_ST7789) ||            \
    defined(HAL_DISPLAY_ST7796S) || defined(HAL_DISPLAY_GC9A01)
static jh_st77xx_t s_tft = {};
static jh_st77xx_config_t s_tft_config = {};
static bool s_tft_pins_configured = false;
#endif
static bool s_tft_ready = false;
#endif /* HAL_ENABLE_TFT */

#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
static jh_rgb_oled_t s_rgb_oled = {};
#endif

#ifdef HAL_ENABLE_ST7567
static jh_st7567_t s_st7567 = {};
#endif

#ifdef HAL_ENABLE_SSD16XX
static jh_ssd16xx_t s_ssd16xx = {};
static jh_ssd16xx_profile_t s_ssd16xx_full_profile = {};
static jh_ssd16xx_profile_t s_ssd16xx_partial_profile = {};
#endif

#ifdef HAL_ENABLE_UC81XX
static jh_uc81xx_t s_uc81xx = {};
static jh_uc81xx_profile_t s_uc81xx_full_profile = {};
static jh_uc81xx_profile_t s_uc81xx_partial_profile = {};
#endif

#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
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
#endif /* JH_DISPLAY_HAS_IMMEDIATE_RGB */

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

static bool oled_controller_valid(hal_display_oled_controller_t controller) {
  return controller >= HAL_DISPLAY_OLED_CONTROLLER_SSD1306 &&
         controller <= HAL_DISPLAY_OLED_CONTROLLER_CH1115;
}

static bool oled_bus_type_valid(hal_display_oled_bus_t bus_type) {
  return bus_type == HAL_DISPLAY_OLED_BUS_I2C ||
         bus_type == HAL_DISPLAY_OLED_BUS_SPI;
}

static jh_ssd1306_controller_t
map_oled_controller(hal_display_oled_controller_t controller) {
  switch (controller) {
  case HAL_DISPLAY_OLED_CONTROLLER_SSD1309:
    return JH_SSD1306_CONTROLLER_SSD1309;
  case HAL_DISPLAY_OLED_CONTROLLER_SSD1315:
    return JH_SSD1306_CONTROLLER_SSD1315;
  case HAL_DISPLAY_OLED_CONTROLLER_SH1106:
    return JH_SSD1306_CONTROLLER_SH1106;
  case HAL_DISPLAY_OLED_CONTROLLER_CH1115:
    return JH_SSD1306_CONTROLLER_CH1115;
  case HAL_DISPLAY_OLED_CONTROLLER_SSD1306:
  default:
    return JH_SSD1306_CONTROLLER_SSD1306;
  }
}

static jh_ssd1306_bus_t map_oled_bus(hal_display_oled_bus_t bus_type) {
  return bus_type == HAL_DISPLAY_OLED_BUS_SPI ? JH_SSD1306_BUS_SPI
                                              : JH_SSD1306_BUS_I2C;
}

static jh_ssd1306_orientation_t
map_oled_orientation(hal_display_oled_orientation_t orientation) {
  return orientation == HAL_DISPLAY_OLED_ORIENTATION_ROTATED_180
             ? JH_SSD1306_ORIENTATION_ROTATED_180
             : JH_SSD1306_ORIENTATION_NORMAL;
}

static hal_status_t oled_init_locked(const jh_ssd1306_config_t *config,
                                     const char *fn_name) {
  if (config == NULL || config->width == 0u || config->height == 0u) {
    hal_derr("%s: invalid size", fn_name);
    return HAL_EINVAL;
  }

  if (!s_oled_gfx.allocate((int16_t)config->width, (int16_t)config->height)) {
    hal_derr("%s: framebuffer allocation failed", fn_name);
    return HAL_ENOMEM;
  }

  if (!jh_ssd1306_init(&s_oled, config)) {
    s_oled_gfx.release();
    s_backend = DISPLAY_BACKEND_NONE;
    s_width = 0;
    s_height = 0;
    hal_derr("%s: SSD1306-family init failed", fn_name);
    return HAL_EIO;
  }

  s_backend = DISPLAY_BACKEND_SSD1306;
  s_oled_gfx.setRotation(0u);
  s_oled_gfx.fillScreen(JH_SSD1306_BLACK);
  s_width = s_oled_gfx.width();
  s_height = s_oled_gfx.height();
  return jh_ssd1306_display(&s_oled, s_oled_gfx.data()) ? HAL_OK : HAL_EIO;
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

static inline bool using_rgb_oled(void) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  return s_backend == DISPLAY_BACKEND_RGB_OLED && s_rgb_oled.initialized;
#else
  return false;
#endif
}

static inline bool using_st7567(void) {
#ifdef HAL_ENABLE_ST7567
  return s_backend == DISPLAY_BACKEND_ST7567 && s_st7567.initialized;
#else
  return false;
#endif
}

static inline bool using_ssd16xx(void) {
#ifdef HAL_ENABLE_SSD16XX
  return s_backend == DISPLAY_BACKEND_SSD16XX && s_ssd16xx.initialized;
#else
  return false;
#endif
}

static inline bool using_uc81xx(void) {
#ifdef HAL_ENABLE_UC81XX
  return s_backend == DISPLAY_BACKEND_UC81XX && s_uc81xx.initialized;
#else
  return false;
#endif
}

static inline bool using_epd(void) { return using_ssd16xx() || using_uc81xx(); }

static inline bool using_immediate_rgb(void) {
  return using_tft() || using_rgb_oled();
}

static inline bool has_active_display(void) {
  return using_oled() || using_immediate_rgb() || using_st7567() || using_epd();
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
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    return &s_tft_gfx;
  }
#endif
  return NULL;
}

static hal_status_t gfx_unavailable_status(const char *fn) {
  if (has_active_display()) {
    hal_derr("%s: active backend does not advertise legacy GFX", fn);
    return HAL_EUNSUPPORTED;
  }
  (void)ensure_display_ready(fn);
  return HAL_EUNINIT;
}

/* ---- TFT primitives (immediate mode) ------------------------------------- */

#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
static int iabs_int(int v) { return v < 0 ? -v : v; }

static bool tft_fill_rect_driver(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                 uint16_t color) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return jh_rgb_oled_fill_rect(&s_rgb_oled, x, y, w, h, color);
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_fill_rect(&s_tft, x, y, w, h, color);
#else
  return jh_st77xx_fill_rect(&s_tft, x, y, w, h, color);
#endif
#else
  return false;
#endif
}

static bool tft_draw_rgb_bitmap_driver(uint16_t x, uint16_t y,
                                       const uint16_t *pixels, uint16_t w,
                                       uint16_t h) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return jh_rgb_oled_draw_rgb_bitmap(&s_rgb_oled, x, y, pixels, w, h);
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_draw_rgb_bitmap(&s_tft, x, y, pixels, w, h);
#else
  return jh_st77xx_draw_rgb_bitmap(&s_tft, x, y, pixels, w, h);
#endif
#else
  return false;
#endif
}

static bool tft_begin_write_driver(uint16_t x, uint16_t y, uint16_t w,
                                   uint16_t h) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return jh_rgb_oled_begin_write(&s_rgb_oled, x, y, w, h);
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_begin_write(&s_tft, x, y, w, h);
#else
  return jh_st77xx_begin_write(&s_tft, x, y, w, h);
#endif
#else
  return false;
#endif
}

static bool tft_write_pixels_fast_driver(const uint16_t *pixels, size_t count) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return jh_rgb_oled_write_pixels_fast(&s_rgb_oled, pixels, count);
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_write_pixels_fast(&s_tft, pixels, count);
#else
  return jh_st77xx_write_pixels_fast(&s_tft, pixels, count);
#endif
#else
  return false;
#endif
}

static bool tft_write_pixels_be_driver(const uint8_t *pixels_be,
                                       size_t byte_count) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return jh_rgb_oled_write_pixels_be(&s_rgb_oled, pixels_be, byte_count);
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_write_pixels_be(&s_tft, pixels_be, byte_count);
#else
  return jh_st77xx_write_pixels_be(&s_tft, pixels_be, byte_count);
#endif
#else
  return false;
#endif
}

static bool tft_write_pixels_dma_driver(const uint8_t *pixels_be,
                                        size_t byte_count) {
  (void)pixels_be;
  (void)byte_count;
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return false;
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_write_pixels_dma(&s_tft, pixels_be, byte_count);
#else
  return jh_st77xx_write_pixels_dma(&s_tft, pixels_be, byte_count);
#endif
#else
  return false;
#endif
}

static bool tft_write_pixels_dma_async_start_driver(const uint8_t *pixels_be,
                                                    size_t byte_count) {
  (void)pixels_be;
  (void)byte_count;
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return false;
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_write_pixels_dma_async_start(&s_tft, pixels_be, byte_count);
#else
  return jh_st77xx_write_pixels_dma_async_start(&s_tft, pixels_be, byte_count);
#endif
#else
  return false;
#endif
}

static bool tft_write_pixels_dma_async_busy_driver(void) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return false;
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_write_pixels_dma_async_busy(&s_tft);
#else
  return jh_st77xx_write_pixels_dma_async_busy(&s_tft);
#endif
#else
  return false;
#endif
}

static bool tft_write_pixels_dma_async_wait_driver(void) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_write_pixels_dma_async_wait(&s_tft);
#else
  return jh_st77xx_write_pixels_dma_async_wait(&s_tft);
#endif
#else
  return false;
#endif
}

static bool tft_end_write_driver(void) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return jh_rgb_oled_end_write(&s_rgb_oled);
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_end_write(&s_tft);
#else
  return jh_st77xx_end_write(&s_tft);
#endif
#else
  return false;
#endif
}

static bool tft_set_rotation_driver(uint8_t rotation) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return jh_rgb_oled_set_rotation(&s_rgb_oled, rotation);
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_set_rotation(&s_tft, rotation);
#else
  return jh_st77xx_set_rotation(&s_tft, rotation);
#endif
#else
  return false;
#endif
}

static bool tft_invert_driver(bool invert) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return jh_rgb_oled_invert(&s_rgb_oled, invert);
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_invert(&s_tft, invert);
#else
  return jh_st77xx_invert(&s_tft, invert);
#endif
#else
  return false;
#endif
}

static bool tft_soft_init_driver(uint32_t delay_ms) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    (void)delay_ms;
    return true;
  }
#endif
#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  return jh_ili9341_soft_init(&s_tft, delay_ms);
#else
  (void)delay_ms;
  return jh_st77xx_soft_init(&s_tft);
#endif
#else
  return false;
#endif
}

static uint16_t tft_native_width(void) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return s_rgb_oled.width;
  }
#endif
#ifdef HAL_ENABLE_TFT
  return s_tft.width;
#else
  return 0u;
#endif
}
static uint16_t tft_native_height(void) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return s_rgb_oled.height;
  }
#endif
#ifdef HAL_ENABLE_TFT
  return s_tft.height;
#else
  return 0u;
#endif
}

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
#endif /* JH_DISPLAY_HAS_IMMEDIATE_RGB */

/* ---- Init / control ------------------------------------------------------ */

#ifdef HAL_ENABLE_TFT
hal_status_t hal_display_init(uint8_t cs, uint8_t dc, uint8_t rst) {
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
#if defined(HAL_DISPLAY_ILI9341)
  return s_tft_ready ? HAL_OK : HAL_EIO;
#else
  return HAL_OK;
#endif
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
  DisplayLock guard;
  /* The HAL I2C bus is initialised lazily on the first transaction, so the
   * Arduino-era periphBegin flag has no direct equivalent here. */
  (void)periph_begin;

  if (width <= 0 || height <= 0) {
    hal_derr("hal_display_init_ssd1306_i2c: invalid size %dx%d", width, height);
    return HAL_EINVAL;
  }

  jh_ssd1306_config_t config = {};
  config.controller = JH_SSD1306_CONTROLLER_SSD1306;
  config.bus_type = JH_SSD1306_BUS_I2C;
  config.bus = i2c_bus;
  config.i2c_addr = i2c_addr;
  config.width = (uint16_t)width;
  config.height = (uint16_t)height;
  // rst_pin is intentionally signed (int8_t, -1 means "not connected").
  // NOLINTNEXTLINE(bugprone-signed-char-misuse,cert-str34-c)
  config.rst_pin = rst_pin;
  config.vccstate = switchvcc;
  config.clock_hz = JH_SSD1306_DEFAULT_I2C_HZ;

  return oled_init_locked(&config, "hal_display_init_ssd1306_i2c");
}

hal_status_t hal_display_init_ssd1306_family_ex(
    const hal_display_ssd1306_family_config_t *config) {
  DisplayLock guard;
  if (config == NULL) {
    return HAL_EINVAL;
  }
  if (config->width <= 0 || config->height <= 0 ||
      !oled_controller_valid(config->controller) ||
      !oled_bus_type_valid(config->bus_type) ||
      config->orientation > HAL_DISPLAY_OLED_ORIENTATION_ROTATED_180) {
    hal_derr("hal_display_init_ssd1306_family: invalid config");
    return HAL_EINVAL;
  }
#ifndef HAL_ENABLE_SPI
  if (config->bus_type == HAL_DISPLAY_OLED_BUS_SPI) {
    return HAL_EUNSUPPORTED;
  }
#endif

  (void)config->periphBegin;

  jh_ssd1306_config_t driver_config = {};
  driver_config.controller = map_oled_controller(config->controller);
  driver_config.bus_type = map_oled_bus(config->bus_type);
  driver_config.bus = config->bus;
  driver_config.i2c_addr = config->i2c_addr;
  driver_config.width = (uint16_t)config->width;
  driver_config.height = (uint16_t)config->height;
  driver_config.rst_pin = config->rst_pin;
  driver_config.vccstate = config->switchvcc;
  driver_config.clock_hz = config->clock_hz;
  driver_config.spi_dc_pin = config->spi_dc_pin;
  driver_config.spi_cs_pin = config->spi_cs_pin;
  driver_config.spi_mode = config->spi_mode;
  driver_config.segment_offset = config->segment_offset;
  driver_config.page_offset = config->page_offset;
  driver_config.display_offset = config->display_offset;
  driver_config.orientation = map_oled_orientation(config->orientation);
  driver_config.internal_iref = config->internal_iref;

  return oled_init_locked(&driver_config, "hal_display_init_ssd1306_family");
}
#endif /* HAL_ENABLE_SSD1306 */

#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
hal_status_t
hal_display_init_rgb_oled_ex(const hal_display_rgb_oled_config_t *config) {
  DisplayLock guard;
  if (config == NULL || config->dc_pin < 0 || config->width == 0u ||
      config->height == 0u ||
      config->controller > HAL_DISPLAY_RGB_OLED_SSD1357) {
    return HAL_EINVAL;
  }
#ifndef HAL_ENABLE_SSD1331
  if (config->controller == HAL_DISPLAY_RGB_OLED_SSD1331) {
    return HAL_EUNSUPPORTED;
  }
#endif
#ifndef HAL_ENABLE_SSD135X
  if (config->controller != HAL_DISPLAY_RGB_OLED_SSD1331) {
    return HAL_EUNSUPPORTED;
  }
#endif

  jh_rgb_oled_config_t driver = {};
  driver.controller = (jh_rgb_oled_controller_t)config->controller;
  driver.bus = config->bus;
  driver.cs_pin = config->cs_pin;
  driver.dc_pin = config->dc_pin;
  driver.rst_pin = config->rst_pin;
  driver.clock_hz = config->clock_hz;
  driver.spi_mode = config->spi_mode;
  driver.width = config->width;
  driver.height = config->height;
  driver.start_line = config->start_line;
  driver.display_offset = config->display_offset;
  driver.multiplex_ratio = config->multiplex_ratio;
  driver.phase_length = config->phase_length;
  driver.oscillator_freq = config->oscillator_freq;
  driver.precharge_time_a = config->precharge_time_a;
  driver.precharge_time_b = config->precharge_time_b;
  driver.precharge_time_c = config->precharge_time_c;
  driver.precharge_time = config->precharge_time;
  driver.precharge_voltage = config->precharge_voltage;
  driver.vcomh_voltage = config->vcomh_voltage;
  driver.current_att = config->current_att;
  driver.remap_value = config->remap_value;
  driver.column_offset = config->column_offset;
  driver.contrast_a = config->contrast_a;
  driver.contrast_b = config->contrast_b;
  driver.contrast_c = config->contrast_c;
  driver.power_save = config->power_save;
  driver.inverted = config->inverted;

  if (!jh_rgb_oled_init(&s_rgb_oled, &driver)) {
    s_backend = DISPLAY_BACKEND_NONE;
    return HAL_EIO;
  }
  s_backend = DISPLAY_BACKEND_RGB_OLED;
  s_width = (int)s_rgb_oled.width;
  s_height = (int)s_rgb_oled.height;
  s_tft_gfx.configure((int16_t)s_width, (int16_t)s_height);
  s_tft_gfx.setRotation(0u);
  s_rotation = HAL_DISPLAY_ROTATION_0;
  return HAL_OK;
}
#endif

#ifdef HAL_ENABLE_ST7567
hal_status_t
hal_display_init_st7567_ex(const hal_display_st7567_config_t *config) {
  DisplayLock guard;
  if (config == NULL || config->width == 0u || config->height == 0u ||
      config->bus_type > HAL_DISPLAY_ST7567_BUS_SPI ||
      (config->pixel_format != HAL_DISPLAY_PIXEL_FORMAT_MONO01 &&
       config->pixel_format != HAL_DISPLAY_PIXEL_FORMAT_MONO10)) {
    return HAL_EINVAL;
  }
#ifndef HAL_ENABLE_SPI
  if (config->bus_type == HAL_DISPLAY_ST7567_BUS_SPI) {
    return HAL_EUNSUPPORTED;
  }
#endif

  jh_st7567_config_t driver = {};
  driver.bus_type = (jh_st7567_bus_t)config->bus_type;
  driver.bus = config->bus;
  driver.i2c_addr = config->i2c_addr;
  driver.rst_pin = config->rst_pin;
  driver.spi_dc_pin = config->spi_dc_pin;
  driver.spi_cs_pin = config->spi_cs_pin;
  driver.clock_hz = config->clock_hz;
  driver.spi_mode = config->spi_mode;
  driver.width = config->width;
  driver.height = config->height;
  driver.column_offset = config->column_offset;
  driver.line_offset = config->line_offset;
  driver.regulation_ratio = config->regulation_ratio;
  driver.segment_invdir = config->segment_invdir;
  driver.com_invdir = config->com_invdir;
  driver.inversion_on = config->inversion_on;
  driver.bias = config->bias;
  driver.pixel_format = config->pixel_format == HAL_DISPLAY_PIXEL_FORMAT_MONO01
                            ? JH_ST7567_PIXEL_MONO01
                            : JH_ST7567_PIXEL_MONO10;

  if (!jh_st7567_init(&s_st7567, &driver)) {
    s_backend = DISPLAY_BACKEND_NONE;
    return HAL_EIO;
  }
  s_backend = DISPLAY_BACKEND_ST7567;
  s_width = (int)s_st7567.width;
  s_height = (int)s_st7567.height;
  s_rotation = HAL_DISPLAY_ROTATION_0;
  return HAL_OK;
}
#endif

#if defined(HAL_ENABLE_SSD16XX) || defined(HAL_ENABLE_UC81XX)
static jh_epd_spi_config_t
map_epd_transport(const hal_display_epd_spi_config_t *transport) {
  jh_epd_spi_config_t mapped = {};
  mapped.bus = transport->bus;
  mapped.cs_pin = transport->cs_pin;
  mapped.dc_pin = transport->dc_pin;
  mapped.rst_pin = transport->rst_pin;
  mapped.busy_pin = transport->busy_pin;
  mapped.clock_hz = transport->clock_hz;
  mapped.busy_timeout_ms = transport->busy_timeout_ms;
  mapped.spi_mode = transport->spi_mode;
  mapped.busy_active_high = transport->busy_active_high;
  return mapped;
}
#endif

#ifdef HAL_ENABLE_SSD16XX
static jh_ssd16xx_profile_t
map_ssd16xx_profile(const hal_display_ssd16xx_profile_t *profile) {
  jh_ssd16xx_profile_t mapped = {};
  if (profile == NULL) {
    return mapped;
  }
  mapped.lut = {profile->lut.data, profile->lut.len};
  mapped.gate_voltage = {profile->gate_voltage.data, profile->gate_voltage.len};
  mapped.source_voltage = {profile->source_voltage.data,
                           profile->source_voltage.len};
  mapped.vcom = profile->vcom;
  mapped.border_waveform = profile->border_waveform;
  mapped.dummy_line = profile->dummy_line;
  mapped.gate_line_width = profile->gate_line_width;
  mapped.override_vcom = profile->override_vcom;
  mapped.override_border_waveform = profile->override_border_waveform;
  mapped.override_dummy_line = profile->override_dummy_line;
  mapped.override_gate_line_width = profile->override_gate_line_width;
  return mapped;
}

hal_status_t
hal_display_init_ssd16xx_ex(const hal_display_ssd16xx_config_t *config) {
  DisplayLock guard;
  if (config == NULL || config->width == 0u || config->height < 8u ||
      config->rotation > HAL_DISPLAY_ROTATION_270 ||
      config->controller > HAL_DISPLAY_SSD16XX_SSD1681) {
    return HAL_EINVAL;
  }
  jh_ssd16xx_config_t driver = {};
  driver.controller = (jh_ssd16xx_controller_t)config->controller;
  driver.transport = map_epd_transport(&config->transport);
  driver.width = config->width;
  driver.height = config->height;
  driver.rotation = (uint8_t)config->rotation;
  driver.temperature_sensor_selection = config->temperature_sensor_selection;
  driver.softstart = {config->softstart.data, config->softstart.len};
  if (config->full_profile != NULL) {
    s_ssd16xx_full_profile = map_ssd16xx_profile(config->full_profile);
    driver.full_profile = &s_ssd16xx_full_profile;
  }
  if (config->partial_profile != NULL) {
    s_ssd16xx_partial_profile = map_ssd16xx_profile(config->partial_profile);
    driver.partial_profile = &s_ssd16xx_partial_profile;
  }
  const hal_status_t status = jh_ssd16xx_init(&s_ssd16xx, &driver);
  if (hal_status_is_error(status)) {
    s_backend = DISPLAY_BACKEND_NONE;
    return status;
  }
  s_backend = DISPLAY_BACKEND_SSD16XX;
  if (config->rotation == HAL_DISPLAY_ROTATION_90 ||
      config->rotation == HAL_DISPLAY_ROTATION_270) {
    s_width = (int)(config->height & ~7u);
    s_height = config->width;
  } else {
    s_width = config->width;
    s_height = (int)(config->height & ~7u);
  }
  s_rotation = (uint8_t)config->rotation;
  return HAL_OK;
}
#endif

#ifdef HAL_ENABLE_UC81XX
static jh_uc81xx_profile_t
map_uc81xx_profile(const hal_display_uc81xx_profile_t *profile) {
  jh_uc81xx_profile_t mapped = {};
  if (profile == NULL) {
    return mapped;
  }
  mapped.power = {profile->power.data, profile->power.len};
  mapped.lut_vcom = {profile->lut_vcom.data, profile->lut_vcom.len};
  mapped.lut_white_to_white = {profile->lut_white_to_white.data,
                               profile->lut_white_to_white.len};
  mapped.lut_black_to_white = {profile->lut_black_to_white.data,
                               profile->lut_black_to_white.len};
  mapped.lut_white_to_black = {profile->lut_white_to_black.data,
                               profile->lut_white_to_black.len};
  mapped.lut_black_to_black = {profile->lut_black_to_black.data,
                               profile->lut_black_to_black.len};
  mapped.lut_border = {profile->lut_border.data, profile->lut_border.len};
  mapped.cdi = profile->cdi;
  mapped.tcon = profile->tcon;
  mapped.pll = profile->pll;
  mapped.vdcs = profile->vdcs;
  mapped.override_cdi = profile->override_cdi;
  mapped.override_tcon = profile->override_tcon;
  mapped.override_pll = profile->override_pll;
  mapped.override_vdcs = profile->override_vdcs;
  return mapped;
}

hal_status_t
hal_display_init_uc81xx_ex(const hal_display_uc81xx_config_t *config) {
  DisplayLock guard;
  if (config == NULL || config->width == 0u || config->height == 0u ||
      config->controller > HAL_DISPLAY_UC81XX_UC8179) {
    return HAL_EINVAL;
  }
  jh_uc81xx_config_t driver = {};
  driver.controller = (jh_uc81xx_controller_t)config->controller;
  driver.transport = map_epd_transport(&config->transport);
  driver.width = config->width;
  driver.height = config->height;
  driver.softstart = {config->softstart.data, config->softstart.len};
  if (config->full_profile != NULL) {
    s_uc81xx_full_profile = map_uc81xx_profile(config->full_profile);
    driver.full_profile = &s_uc81xx_full_profile;
  }
  if (config->partial_profile != NULL) {
    s_uc81xx_partial_profile = map_uc81xx_profile(config->partial_profile);
    driver.partial_profile = &s_uc81xx_partial_profile;
  }
  const hal_status_t status = jh_uc81xx_init(&s_uc81xx, &driver);
  if (hal_status_is_error(status)) {
    s_backend = DISPLAY_BACKEND_NONE;
    return status;
  }
  s_backend = DISPLAY_BACKEND_UC81XX;
  s_width = config->width;
  s_height = config->height;
  s_rotation = HAL_DISPLAY_ROTATION_0;
  return HAL_OK;
}
#endif

hal_status_t hal_display_configure_ex(int width, int height, uint8_t rotation,
                                      bool invert, bool bgr) {
  DisplayLock guard;
  if (width <= 0 || height <= 0) {
    hal_derr("hal_display_configure: invalid size %dx%d", width, height);
    return HAL_EINVAL;
  }

#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.setRotation(rotation);
    if (!jh_ssd1306_invert(&s_oled, invert)) {
      return HAL_EIO;
    }
    (void)bgr;
    s_width = s_oled_gfx.width();
    s_height = s_oled_gfx.height();
    s_rotation = rotation & 0x03u;
    return HAL_OK;
  }
#endif

#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    if (width != s_width || height != s_height || bgr ||
        (rotation & 0x03u) != HAL_DISPLAY_ROTATION_0) {
      return HAL_EUNSUPPORTED;
    }
    if (!tft_set_rotation_driver(rotation) || !tft_invert_driver(invert)) {
      return HAL_EIO;
    }
    s_rotation = rotation & 0x03u;
    return HAL_OK;
  }
#endif

#ifdef HAL_ENABLE_ST7567
  if (using_st7567()) {
    if (width != s_width || height != s_height || rotation != 0u || bgr ||
        invert != s_st7567.config.inversion_on) {
      return HAL_EUNSUPPORTED;
    }
    return HAL_OK;
  }
#endif

  if (using_epd()) {
    if (width != s_width || height != s_height || bgr || invert ||
        rotation != s_rotation) {
      return HAL_EUNSUPPORTED;
    }
    return HAL_OK;
  }

#ifdef HAL_ENABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
  if (!using_tft()) {
    hal_derr("hal_display_configure: TFT backend is not initialized");
    return HAL_EUNINIT;
  }
  (void)bgr;
  if (!tft_invert_driver(invert) || !tft_set_rotation_driver(rotation)) {
    return HAL_EIO;
  }
#else
  if (!s_tft_pins_configured) {
    hal_derr("hal_display_configure: TFT pins are not initialized");
    return HAL_EUNINIT;
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
#elif defined(HAL_DISPLAY_GC9A01)
  s_tft_config.chip = JH_ST77XX_CHIP_GC9A01;
#endif
  s_tft_ready = jh_st77xx_init(&s_tft, &s_tft_config);
  if (!s_tft_ready) {
    hal_derr("hal_display_configure: ST77xx init failed");
    return HAL_EIO;
  }
  if (!tft_invert_driver(invert) || !tft_set_rotation_driver(rotation)) {
    s_tft_ready = false;
    return HAL_EIO;
  }
#endif
  s_width = (int)tft_native_width();
  s_height = (int)tft_native_height();
  s_tft_gfx.configure((int16_t)s_width, (int16_t)s_height);
  s_rotation = rotation & 0x03u;
  return HAL_OK;
#else
  (void)rotation;
  (void)invert;
  (void)bgr;
  hal_derr("hal_display_configure: no active display backend");
  return HAL_EUNSUPPORTED;
#endif /* HAL_ENABLE_TFT */
}

bool hal_display_configure(int width, int height, uint8_t rotation, bool invert,
                           bool bgr) {
  return hal_status_to_bool(
      hal_display_configure_ex(width, height, rotation, invert, bgr));
}

hal_status_t hal_display_soft_init(int delay_ms) {
  DisplayLock guard;
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    return tft_soft_init_driver(delay_ms > 0 ? (uint32_t)delay_ms : 0u)
               ? HAL_OK
               : HAL_EIO;
  }
#endif
  (void)delay_ms;
  return HAL_OK;
}

hal_status_t hal_display_suspend_ex(void) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_suspend")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    return jh_ssd1306_suspend(&s_oled) ? HAL_OK : HAL_EIO;
  }
#endif
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return jh_rgb_oled_suspend(&s_rgb_oled) ? HAL_OK : HAL_EIO;
  }
#endif
#ifdef HAL_ENABLE_ST7567
  if (using_st7567()) {
    return jh_st7567_suspend(&s_st7567) ? HAL_OK : HAL_EIO;
  }
#endif
#ifdef HAL_ENABLE_SSD16XX
  if (using_ssd16xx()) {
    return jh_ssd16xx_suspend(&s_ssd16xx);
  }
#endif
#ifdef HAL_ENABLE_UC81XX
  if (using_uc81xx()) {
    return jh_uc81xx_suspend(&s_uc81xx);
  }
#endif
  return HAL_EUNSUPPORTED;
}

hal_status_t hal_display_resume_ex(void) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_resume")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    return jh_ssd1306_resume(&s_oled) ? HAL_OK : HAL_EIO;
  }
#endif
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
  if (using_rgb_oled()) {
    return jh_rgb_oled_resume(&s_rgb_oled) ? HAL_OK : HAL_EIO;
  }
#endif
#ifdef HAL_ENABLE_ST7567
  if (using_st7567()) {
    return jh_st7567_resume(&s_st7567) ? HAL_OK : HAL_EIO;
  }
#endif
#ifdef HAL_ENABLE_SSD16XX
  if (using_ssd16xx()) {
    return jh_ssd16xx_resume(&s_ssd16xx);
  }
#endif
#ifdef HAL_ENABLE_UC81XX
  if (using_uc81xx()) {
    return jh_uc81xx_resume(&s_uc81xx);
  }
#endif
  return HAL_EUNSUPPORTED;
}

hal_status_t hal_display_set_rotation_ex(uint8_t r) {
  (void)r;
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_set_rotation")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.setRotation(r);
    s_width = s_oled_gfx.width();
    s_height = s_oled_gfx.height();
    s_rotation = r & 0x03u;
    return HAL_OK;
  }
#endif
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
    if (using_rgb_oled() && (r & 0x03u) != HAL_DISPLAY_ROTATION_0) {
      return HAL_EUNSUPPORTED;
    }
#endif
    if (!tft_set_rotation_driver(r)) {
      return HAL_EIO;
    }
    s_width = (int)tft_native_width();
    s_height = (int)tft_native_height();
    s_tft_gfx.configure((int16_t)s_width, (int16_t)s_height);
    s_rotation = r & 0x03u;
    return HAL_OK;
  }
#endif
#ifdef HAL_ENABLE_SSD16XX
  if (using_ssd16xx()) {
    const hal_status_t status = jh_ssd16xx_set_rotation(&s_ssd16xx, r & 0x03u);
    if (hal_status_is_ok(status)) {
      s_rotation = r & 0x03u;
      if (s_rotation == HAL_DISPLAY_ROTATION_90 ||
          s_rotation == HAL_DISPLAY_ROTATION_270) {
        s_width = (int)(s_ssd16xx.config.height & ~7u);
        s_height = s_ssd16xx.config.width;
      } else {
        s_width = s_ssd16xx.config.width;
        s_height = (int)(s_ssd16xx.config.height & ~7u);
      }
    }
    return status;
  }
#endif
  return HAL_EUNSUPPORTED;
}

bool hal_display_set_rotation(uint8_t r) {
  return hal_status_to_bool(hal_display_set_rotation_ex(r));
}

hal_status_t hal_display_invert_ex(bool invert) {
  (void)invert;
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_invert")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    return jh_ssd1306_invert(&s_oled, invert) ? HAL_OK : HAL_EIO;
  }
#endif
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    return tft_invert_driver(invert) ? HAL_OK : HAL_EIO;
  }
#endif
  return HAL_EUNSUPPORTED;
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

hal_status_t hal_display_get_capabilities_ex(hal_display_capabilities_t *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }
  DisplayLock guard;
  memset(out, 0, sizeof(*out));
  if (!has_active_display()) {
    return HAL_EUNINIT;
  }
  out->width = (uint16_t)s_width;
  out->height = (uint16_t)s_height;
  out->current_rotation = s_rotation;
  out->x_alignment = 1u;
  out->y_alignment = 1u;
  out->width_alignment = 1u;
  out->height_alignment = 1u;

  if (using_tft()) {
    out->supported_pixel_formats = HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE |
                                   HAL_DISPLAY_PIXEL_FORMAT_RGB565_NATIVE;
    out->current_pixel_format = HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE;
    out->supported_rotations = HAL_DISPLAY_ROTATION_MASK_ALL;
    out->flags = HAL_DISPLAY_CAP_RAW_WRITE | HAL_DISPLAY_CAP_STREAM_WRITE |
                 HAL_DISPLAY_CAP_DMA_WRITE | HAL_DISPLAY_CAP_ASYNC_DMA_WRITE |
                 HAL_DISPLAY_CAP_LEGACY_GFX;
    return HAL_OK;
  }
  if (using_rgb_oled()) {
    out->supported_pixel_formats = HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE |
                                   HAL_DISPLAY_PIXEL_FORMAT_RGB565_NATIVE;
    out->current_pixel_format = HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE;
    out->supported_rotations =
        HAL_DISPLAY_ROTATION_MASK(HAL_DISPLAY_ROTATION_0);
    out->flags = HAL_DISPLAY_CAP_RAW_WRITE | HAL_DISPLAY_CAP_STREAM_WRITE |
                 HAL_DISPLAY_CAP_LEGACY_GFX;
    return HAL_OK;
  }
  if (using_oled()) {
    out->supported_pixel_formats = HAL_DISPLAY_PIXEL_FORMAT_MONO01;
    out->current_pixel_format = HAL_DISPLAY_PIXEL_FORMAT_MONO01;
    out->supported_rotations = HAL_DISPLAY_ROTATION_MASK_ALL;
    out->screen_info = HAL_DISPLAY_SCREEN_INFO_MONO_VTILED;
    out->flags = HAL_DISPLAY_CAP_BUFFERED | HAL_DISPLAY_CAP_LEGACY_GFX;
    return HAL_OK;
  }
#ifdef HAL_ENABLE_ST7567
  if (using_st7567()) {
    out->supported_pixel_formats =
        HAL_DISPLAY_PIXEL_FORMAT_MONO01 | HAL_DISPLAY_PIXEL_FORMAT_MONO10;
    out->current_pixel_format =
        s_st7567.config.pixel_format == JH_ST7567_PIXEL_MONO01
            ? HAL_DISPLAY_PIXEL_FORMAT_MONO01
            : HAL_DISPLAY_PIXEL_FORMAT_MONO10;
    out->supported_rotations =
        HAL_DISPLAY_ROTATION_MASK(HAL_DISPLAY_ROTATION_0);
    out->y_alignment = 8u;
    out->height_alignment = 8u;
    out->screen_info = HAL_DISPLAY_SCREEN_INFO_MONO_VTILED;
    out->flags = HAL_DISPLAY_CAP_RAW_WRITE;
    return HAL_OK;
  }
#endif
#ifdef HAL_ENABLE_SSD16XX
  if (using_ssd16xx()) {
    out->supported_pixel_formats = HAL_DISPLAY_PIXEL_FORMAT_MONO10;
    out->current_pixel_format = HAL_DISPLAY_PIXEL_FORMAT_MONO10;
    out->supported_rotations = HAL_DISPLAY_ROTATION_MASK_ALL;
    out->screen_info =
        HAL_DISPLAY_SCREEN_INFO_MONO_MSB_FIRST | HAL_DISPLAY_SCREEN_INFO_EPD;
    if (s_rotation == HAL_DISPLAY_ROTATION_0 ||
        s_rotation == HAL_DISPLAY_ROTATION_180) {
      out->y_alignment = 8u;
      out->height_alignment = 8u;
      out->screen_info |= HAL_DISPLAY_SCREEN_INFO_MONO_VTILED;
    } else {
      out->x_alignment = 8u;
      out->width_alignment = 8u;
    }
    out->flags = HAL_DISPLAY_CAP_RAW_WRITE;
    return HAL_OK;
  }
#endif
#ifdef HAL_ENABLE_UC81XX
  if (using_uc81xx()) {
    out->supported_pixel_formats = HAL_DISPLAY_PIXEL_FORMAT_MONO10;
    out->current_pixel_format = HAL_DISPLAY_PIXEL_FORMAT_MONO10;
    out->supported_rotations =
        HAL_DISPLAY_ROTATION_MASK(HAL_DISPLAY_ROTATION_0);
    out->x_alignment = 8u;
    out->width_alignment = 8u;
    out->screen_info =
        HAL_DISPLAY_SCREEN_INFO_MONO_MSB_FIRST | HAL_DISPLAY_SCREEN_INFO_EPD;
    out->flags = HAL_DISPLAY_CAP_RAW_WRITE;
    return HAL_OK;
  }
#endif
  return HAL_EUNSUPPORTED;
}

hal_status_t
hal_display_set_pixel_format_ex(hal_display_pixel_format_t pixel_format) {
  DisplayLock guard;
  if (!has_active_display()) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_ST7567
  if (using_st7567()) {
    if (pixel_format != HAL_DISPLAY_PIXEL_FORMAT_MONO01 &&
        pixel_format != HAL_DISPLAY_PIXEL_FORMAT_MONO10) {
      return HAL_EUNSUPPORTED;
    }
    const jh_st7567_pixel_format_t format =
        pixel_format == HAL_DISPLAY_PIXEL_FORMAT_MONO01
            ? JH_ST7567_PIXEL_MONO01
            : JH_ST7567_PIXEL_MONO10;
    return jh_st7567_set_pixel_format(&s_st7567, format) ? HAL_OK : HAL_EIO;
  }
#endif
  if (using_immediate_rgb() &&
      pixel_format == HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE) {
    return HAL_OK;
  }
  if (using_oled() && pixel_format == HAL_DISPLAY_PIXEL_FORMAT_MONO01) {
    return HAL_OK;
  }
  if (using_epd() && pixel_format == HAL_DISPLAY_PIXEL_FORMAT_MONO10) {
    return HAL_OK;
  }
  return HAL_EUNSUPPORTED;
}

hal_status_t hal_display_write_raw_ex(uint16_t x, uint16_t y,
                                      const hal_display_buffer_desc_t *desc,
                                      const void *buffer) {
  DisplayLock guard;
  if (desc == NULL || buffer == NULL || desc->width == 0u ||
      desc->height == 0u || desc->pitch < desc->width) {
    return HAL_EINVAL;
  }
  if (!has_active_display()) {
    return HAL_EUNINIT;
  }
  const uint32_t writable_height =
      using_st7567() ? ((uint32_t)s_height + 7u) & ~7u : (uint32_t)s_height;
  if ((uint32_t)x + desc->width > (uint32_t)s_width ||
      (uint32_t)y + desc->height > writable_height) {
    return HAL_EINVAL;
  }
  if (desc->pitch != desc->width) {
    return HAL_EUNSUPPORTED;
  }

#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    if (desc->pixel_format != HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE &&
        desc->pixel_format != HAL_DISPLAY_PIXEL_FORMAT_RGB565_NATIVE) {
      return HAL_EUNSUPPORTED;
    }
    const size_t pixel_count = (size_t)desc->width * desc->height;
    const size_t required = pixel_count * sizeof(uint16_t);
    if (desc->buf_size < required) {
      return HAL_EINVAL;
    }
    if (!tft_begin_write_driver(x, y, desc->width, desc->height)) {
      return HAL_EIO;
    }
    const bool write_ok =
        desc->pixel_format == HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE
            ? tft_write_pixels_be_driver((const uint8_t *)buffer, required)
            : tft_write_pixels_fast_driver((const uint16_t *)buffer,
                                           pixel_count);
    const bool end_ok = tft_end_write_driver();
    return write_ok && end_ok ? HAL_OK : HAL_EIO;
  }
#endif

#ifdef HAL_ENABLE_ST7567
  if (using_st7567()) {
    const hal_display_pixel_format_t current =
        s_st7567.config.pixel_format == JH_ST7567_PIXEL_MONO01
            ? HAL_DISPLAY_PIXEL_FORMAT_MONO01
            : HAL_DISPLAY_PIXEL_FORMAT_MONO10;
    if (desc->pixel_format != current) {
      return HAL_EUNSUPPORTED;
    }
    if ((y & 7u) != 0u || (desc->height & 7u) != 0u) {
      return HAL_EINVAL;
    }
    const size_t required = (size_t)desc->width * ((size_t)desc->height / 8u);
    if (desc->buf_size < required) {
      return HAL_EINVAL;
    }
    return jh_st7567_write(&s_st7567, x, y, desc->width, desc->height,
                           (const uint8_t *)buffer, desc->buf_size)
               ? HAL_OK
               : HAL_EIO;
  }
#endif
#ifdef HAL_ENABLE_SSD16XX
  if (using_ssd16xx()) {
    if (desc->pixel_format != HAL_DISPLAY_PIXEL_FORMAT_MONO10) {
      return HAL_EUNSUPPORTED;
    }
    const size_t required = (size_t)desc->width * desc->height / 8u;
    if (required == 0u || desc->buf_size < required) {
      return HAL_EINVAL;
    }
    return jh_ssd16xx_write(&s_ssd16xx, x, y, desc->width, desc->height,
                            (const uint8_t *)buffer, desc->buf_size,
                            !desc->frame_incomplete);
  }
#endif
#ifdef HAL_ENABLE_UC81XX
  if (using_uc81xx()) {
    if (desc->pixel_format != HAL_DISPLAY_PIXEL_FORMAT_MONO10) {
      return HAL_EUNSUPPORTED;
    }
    const size_t required = (size_t)desc->width * desc->height / 8u;
    if (required == 0u || desc->buf_size < required) {
      return HAL_EINVAL;
    }
    return jh_uc81xx_write(&s_uc81xx, x, y, desc->width, desc->height,
                           (const uint8_t *)buffer, desc->buf_size,
                           !desc->frame_incomplete);
  }
#endif
  return HAL_EUNSUPPORTED;
}

#if defined(HAL_ENABLE_SSD16XX) || defined(HAL_ENABLE_UC81XX)
hal_status_t
hal_display_epd_refresh_ex(hal_display_epd_refresh_mode_t refresh_mode) {
  if (refresh_mode > HAL_DISPLAY_EPD_REFRESH_PARTIAL) {
    return HAL_EINVAL;
  }
  DisplayLock guard;
#ifdef HAL_ENABLE_SSD16XX
  if (using_ssd16xx()) {
    return jh_ssd16xx_refresh(&s_ssd16xx,
                              (jh_ssd16xx_refresh_mode_t)refresh_mode);
  }
#endif
#ifdef HAL_ENABLE_UC81XX
  if (using_uc81xx()) {
    return jh_uc81xx_refresh(&s_uc81xx, (jh_uc81xx_refresh_mode_t)refresh_mode);
  }
#endif
  return has_active_display() ? HAL_EUNSUPPORTED : HAL_EUNINIT;
}
#endif

/* ---- Screen -------------------------------------------------------------- */

hal_status_t hal_display_fill_screen_ex(uint16_t color) {
  (void)color;
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_fill_screen")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.fillScreen(oled_color(color));
    return HAL_OK;
  }
#endif
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    if (s_width <= 0 || s_height <= 0) {
      hal_derr("hal_display_fill_screen: display is not configured");
      return HAL_EUNINIT;
    }
    return fill_rect_unlocked(0, 0, s_width, s_height, color) ? HAL_OK
                                                              : HAL_EIO;
  }
#endif
  return HAL_EUNSUPPORTED;
}

bool hal_display_fill_screen(uint16_t color) {
  return hal_status_to_bool(hal_display_fill_screen_ex(color));
}

hal_status_t hal_display_flush_ex(void) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_flush")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    return jh_ssd1306_display(&s_oled, s_oled_gfx.data()) ? HAL_OK : HAL_EIO;
  }
#endif
#ifdef HAL_ENABLE_SSD16XX
  if (using_ssd16xx()) {
    if (!s_ssd16xx.refresh_pending) {
      return HAL_OK;
    }
    return jh_ssd16xx_refresh(&s_ssd16xx, s_ssd16xx.pending_refresh_mode);
  }
#endif
#ifdef HAL_ENABLE_UC81XX
  if (using_uc81xx()) {
    if (!s_uc81xx.refresh_pending) {
      return HAL_OK;
    }
    return jh_uc81xx_refresh(&s_uc81xx, s_uc81xx.pending_refresh_mode);
  }
#endif
  return HAL_OK;
}

bool hal_display_flush(void) {
  return hal_status_to_bool(hal_display_flush_ex());
}

hal_status_t hal_display_draw_image_ex(int x, int y, int w, int h,
                                       uint16_t background, uint16_t *data) {
  if (data == NULL) {
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

/* ---- Geometry ------------------------------------------------------------ */

hal_status_t hal_display_fill_rect_ex(int x, int y, int w, int h,
                                      uint16_t color) {
  (void)x;
  (void)y;
  (void)color;
  DisplayLock guard;
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_fill_rect: invalid size %dx%d", w, h);
    return HAL_EINVAL;
  }
  if (!ensure_display_ready("hal_display_fill_rect")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.fillRect(x, y, w, h, oled_color(color));
    return HAL_OK;
  }
#endif
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    return fill_rect_unlocked(x, y, w, h, color) ? HAL_OK : HAL_EIO;
  }
#endif
  return HAL_EUNSUPPORTED;
}

bool hal_display_fill_rect(int x, int y, int w, int h, uint16_t color) {
  return hal_status_to_bool(hal_display_fill_rect_ex(x, y, w, h, color));
}

hal_status_t hal_display_draw_rect_ex(int x, int y, int w, int h,
                                      uint16_t color) {
  (void)x;
  (void)y;
  (void)color;
  DisplayLock guard;
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_draw_rect: invalid size %dx%d", w, h);
    return HAL_EINVAL;
  }
  if (!ensure_display_ready("hal_display_draw_rect")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.drawRect(x, y, w, h, oled_color(color));
    return HAL_OK;
  }
#endif
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    bool ok = true;
    ok &= fill_rect_unlocked(x, y, w, 1, color);
    ok &= fill_rect_unlocked(x, y + h - 1, w, 1, color);
    ok &= fill_rect_unlocked(x, y, 1, h, color);
    ok &= fill_rect_unlocked(x + w - 1, y, 1, h, color);
    return ok ? HAL_OK : HAL_EIO;
  }
#endif
  return HAL_EUNSUPPORTED;
}

bool hal_display_draw_rect(int x, int y, int w, int h, uint16_t color) {
  return hal_status_to_bool(hal_display_draw_rect_ex(x, y, w, h, color));
}

hal_status_t hal_display_fill_circle_ex(int x, int y, int r, uint16_t color) {
  (void)x;
  (void)y;
  (void)color;
  DisplayLock guard;
  if (r < 0) {
    hal_derr("hal_display_fill_circle: invalid radius %d", r);
    return HAL_EINVAL;
  }
  if (!ensure_display_ready("hal_display_fill_circle")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.fillCircle(x, y, r, oled_color(color));
    return HAL_OK;
  }
#endif
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    return fill_circle_unlocked(x, y, r, color) ? HAL_OK : HAL_EIO;
  }
#endif
  return HAL_EUNSUPPORTED;
}

bool hal_display_fill_circle(int x, int y, int r, uint16_t color) {
  return hal_status_to_bool(hal_display_fill_circle_ex(x, y, r, color));
}

hal_status_t hal_display_draw_circle_ex(int x, int y, int r, uint16_t color) {
  (void)x;
  (void)y;
  (void)color;
  DisplayLock guard;
  if (r < 0) {
    hal_derr("hal_display_draw_circle: invalid radius %d", r);
    return HAL_EINVAL;
  }
  if (!ensure_display_ready("hal_display_draw_circle")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.drawCircle(x, y, r, oled_color(color));
    return HAL_OK;
  }
#endif
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    return draw_circle_unlocked(x, y, r, color) ? HAL_OK : HAL_EIO;
  }
#endif
  return HAL_EUNSUPPORTED;
}

bool hal_display_draw_circle(int x, int y, int r, uint16_t color) {
  return hal_status_to_bool(hal_display_draw_circle_ex(x, y, r, color));
}

hal_status_t hal_display_fill_round_rect_ex(int x, int y, int w, int h, int r,
                                            uint16_t color) {
  (void)x;
  (void)y;
  (void)color;
  DisplayLock guard;
  if (w <= 0 || h <= 0 || r < 0) {
    hal_derr("hal_display_fill_round_rect: invalid size/radius w=%d h=%d r=%d",
             w, h, r);
    return HAL_EINVAL;
  }
  if (!ensure_display_ready("hal_display_fill_round_rect")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.fillRoundRect(x, y, w, h, r, oled_color(color));
    return HAL_OK;
  }
#endif
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    return fill_round_rect_unlocked(x, y, w, h, r, color) ? HAL_OK : HAL_EIO;
  }
#endif
  return HAL_EUNSUPPORTED;
}

bool hal_display_fill_round_rect(int x, int y, int w, int h, int r,
                                 uint16_t color) {
  return hal_status_to_bool(
      hal_display_fill_round_rect_ex(x, y, w, h, r, color));
}

hal_status_t hal_display_draw_line_ex(int x0, int y0, int x1, int y1,
                                      uint16_t color) {
  (void)x0;
  (void)y0;
  (void)x1;
  (void)y1;
  (void)color;
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_draw_line")) {
    return HAL_EUNINIT;
  }
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.drawLine(x0, y0, x1, y1, oled_color(color));
    return HAL_OK;
  }
#endif
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    return draw_line_unlocked(x0, y0, x1, y1, color) ? HAL_OK : HAL_EIO;
  }
#endif
  return HAL_EUNSUPPORTED;
}

bool hal_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
  return hal_status_to_bool(hal_display_draw_line_ex(x0, y0, x1, y1, color));
}

/* ---- Bitmap -------------------------------------------------------------- */

hal_status_t hal_display_draw_rgb_bitmap_ex(int x, int y, uint16_t *data, int w,
                                            int h) {
  DisplayLock guard;
  if (!data) {
    hal_derr("hal_display_draw_rgb_bitmap: data pointer is NULL");
    return HAL_EINVAL;
  }
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_draw_rgb_bitmap: invalid size %dx%d", w, h);
    return HAL_EINVAL;
  }
  if (!ensure_display_ready("hal_display_draw_rgb_bitmap")) {
    return HAL_EUNINIT;
  }
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    if (x < 0 || y < 0 || x + w > s_width || y + h > s_height) {
      hal_derr("hal_display_draw_rgb_bitmap: out-of-bounds clipping is not "
               "supported");
      return HAL_EINVAL;
    }
    return tft_draw_rgb_bitmap_driver((uint16_t)x, (uint16_t)y, data,
                                      (uint16_t)w, (uint16_t)h)
               ? HAL_OK
               : HAL_EIO;
  }
#else
  (void)x;
  (void)y;
#endif
  hal_derr("hal_display_draw_rgb_bitmap: RGB bitmaps are unsupported on "
           "current backend");
  return HAL_EUNSUPPORTED;
}

bool hal_display_draw_rgb_bitmap(int x, int y, uint16_t *data, int w, int h) {
  return hal_status_to_bool(hal_display_draw_rgb_bitmap_ex(x, y, data, w, h));
}

hal_status_t hal_display_begin_write_ex(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    hal_derr("hal_display_begin_write: invalid size %dx%d", w, h);
    return HAL_EINVAL;
  }
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  display_ensure_mutex();
  hal_mutex_lock(s_display_mutex);

  if (s_tft_stream_active) {
    hal_derr("hal_display_begin_write: stream already active");
    hal_mutex_unlock(s_display_mutex);
    return HAL_EBUSY;
  }
  if (!ensure_display_ready("hal_display_begin_write")) {
    hal_mutex_unlock(s_display_mutex);
    return HAL_EUNINIT;
  }
  if (!using_immediate_rgb()) {
    hal_derr("hal_display_begin_write: backend does not support streaming");
    hal_mutex_unlock(s_display_mutex);
    return HAL_EUNSUPPORTED;
  }
  if (x < 0 || y < 0 || x > s_width - w || y > s_height - h) {
    hal_derr("hal_display_begin_write: out-of-bounds clipping is not "
             "supported");
    hal_mutex_unlock(s_display_mutex);
    return HAL_EINVAL;
  }

  if (!tft_begin_write_driver((uint16_t)x, (uint16_t)y, (uint16_t)w,
                              (uint16_t)h)) {
    hal_mutex_unlock(s_display_mutex);
    return HAL_EIO;
  }

  s_tft_stream_active = true;
  return HAL_OK;
#else
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  hal_derr("hal_display_begin_write: TFT backend is not enabled");
  return HAL_EUNSUPPORTED;
#endif
}

bool hal_display_begin_write(int x, int y, int w, int h) {
  return hal_status_to_bool(hal_display_begin_write_ex(x, y, w, h));
}

static bool ensure_tft_stream_ready(const char *fn) {
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (s_tft_stream_active && using_immediate_rgb()) {
    return true;
  }
#endif
  hal_derr("%s: no active TFT write stream", fn);
  return false;
}

hal_status_t hal_display_write_pixels_fast_ex(const uint16_t *pixels,
                                              size_t count) {
  if (pixels == NULL && count > 0u) {
    return HAL_EINVAL;
  }
  if (!ensure_tft_stream_ready("hal_display_write_pixels_fast")) {
    return HAL_ESTATE;
  }
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  return tft_write_pixels_fast_driver(pixels, count) ? HAL_OK : HAL_EIO;
#else
  (void)pixels;
  (void)count;
  return HAL_EUNSUPPORTED;
#endif
}

bool hal_display_write_pixels_fast(const uint16_t *pixels, size_t count) {
  return hal_status_to_bool(hal_display_write_pixels_fast_ex(pixels, count));
}

hal_status_t hal_display_write_pixels_be_ex(const uint8_t *pixels_be,
                                            size_t byte_count) {
  if ((pixels_be == NULL && byte_count > 0u) || (byte_count & 1u) != 0u) {
    return HAL_EINVAL;
  }
  if (!ensure_tft_stream_ready("hal_display_write_pixels_be")) {
    return HAL_ESTATE;
  }
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  return tft_write_pixels_be_driver(pixels_be, byte_count) ? HAL_OK : HAL_EIO;
#else
  (void)pixels_be;
  (void)byte_count;
  return HAL_EUNSUPPORTED;
#endif
}

bool hal_display_write_pixels_be(const uint8_t *pixels_be, size_t byte_count) {
  return hal_status_to_bool(
      hal_display_write_pixels_be_ex(pixels_be, byte_count));
}

hal_status_t hal_display_write_pixels_dma_ex(const uint8_t *pixels_be,
                                             size_t byte_count) {
  if ((pixels_be == NULL && byte_count > 0u) || (byte_count & 1u) != 0u) {
    return HAL_EINVAL;
  }
  if (!ensure_tft_stream_ready("hal_display_write_pixels_dma")) {
    return HAL_ESTATE;
  }
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_rgb_oled()) {
    return HAL_EUNSUPPORTED;
  }
  return tft_write_pixels_dma_driver(pixels_be, byte_count) ? HAL_OK : HAL_EIO;
#else
  (void)pixels_be;
  (void)byte_count;
  return HAL_EUNSUPPORTED;
#endif
}

bool hal_display_write_pixels_dma(const uint8_t *pixels_be, size_t byte_count) {
  return hal_status_to_bool(
      hal_display_write_pixels_dma_ex(pixels_be, byte_count));
}

hal_status_t
hal_display_write_pixels_dma_async_start_ex(const uint8_t *pixels_be,
                                            size_t byte_count) {
  if ((pixels_be == NULL && byte_count > 0u) || (byte_count & 1u) != 0u) {
    return HAL_EINVAL;
  }
  if (!ensure_tft_stream_ready("hal_display_write_pixels_dma_async_start")) {
    return HAL_ESTATE;
  }
  if (hal_display_write_pixels_dma_async_busy()) {
    return HAL_EBUSY;
  }
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_rgb_oled()) {
    return HAL_EUNSUPPORTED;
  }
  return tft_write_pixels_dma_async_start_driver(pixels_be, byte_count)
             ? HAL_OK
             : HAL_EIO;
#else
  (void)pixels_be;
  (void)byte_count;
  return HAL_EUNSUPPORTED;
#endif
}

bool hal_display_write_pixels_dma_async_start(const uint8_t *pixels_be,
                                              size_t byte_count) {
  return hal_status_to_bool(
      hal_display_write_pixels_dma_async_start_ex(pixels_be, byte_count));
}

bool hal_display_write_pixels_dma_async_busy(void) {
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (!s_tft_stream_active) {
    return false;
  }
  return tft_write_pixels_dma_async_busy_driver();
#else
  return false;
#endif
}

hal_status_t hal_display_write_pixels_dma_async_wait_ex(void) {
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (!s_tft_stream_active) {
    return HAL_OK;
  }
  return tft_write_pixels_dma_async_wait_driver() ? HAL_OK : HAL_EIO;
#else
  return HAL_OK;
#endif
}

bool hal_display_write_pixels_dma_async_wait(void) {
  return hal_status_to_bool(hal_display_write_pixels_dma_async_wait_ex());
}

hal_status_t hal_display_end_write_ex(void) {
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (!s_tft_stream_active) {
    hal_derr("hal_display_end_write: no active TFT write stream");
    return HAL_ESTATE;
  }

  const bool wait_ok = tft_write_pixels_dma_async_wait_driver();
  const bool end_ok = tft_end_write_driver();
  s_tft_stream_active = false;
  hal_mutex_unlock(s_display_mutex);
  return wait_ok && end_ok ? HAL_OK : HAL_EIO;
#else
  hal_derr("hal_display_end_write: TFT backend is not enabled");
  return HAL_EUNSUPPORTED;
#endif
}

bool hal_display_end_write(void) {
  return hal_status_to_bool(hal_display_end_write_ex());
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

hal_status_t hal_display_set_font_ex(hal_font_id_t font) {
  DisplayLock guard;
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    return gfx_unavailable_status("hal_display_set_font");
  }
  s_font = font;
  apply_font_to(gfx, font);
  return HAL_OK;
}
bool hal_display_set_font(hal_font_id_t font) {
  return hal_status_to_bool(hal_display_set_font_ex(font));
}

hal_status_t hal_display_set_text_color_ex(uint16_t color) {
  DisplayLock guard;
  if (!ensure_display_ready("hal_display_set_text_color")) {
    return HAL_EUNINIT;
  }
  s_text_color = color;
#ifdef HAL_ENABLE_SSD1306
  if (using_oled()) {
    s_oled_gfx.setTextColor(oled_color(color));
    return HAL_OK;
  }
#endif
#ifdef JH_DISPLAY_HAS_IMMEDIATE_RGB
  if (using_immediate_rgb()) {
    s_tft_gfx.setTextColor(color);
    return HAL_OK;
  }
#endif
  return HAL_EUNSUPPORTED;
}
bool hal_display_set_text_color(uint16_t color) {
  return hal_status_to_bool(hal_display_set_text_color_ex(color));
}

hal_status_t hal_display_set_text_size_ex(uint8_t size) {
  DisplayLock guard;
  if (size == 0u) {
    hal_derr("hal_display_set_text_size: size must be > 0");
    return HAL_EINVAL;
  }
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    return gfx_unavailable_status("hal_display_set_text_size");
  }
  s_text_size = size;
  gfx->setTextSize(size);
  return HAL_OK;
}
bool hal_display_set_text_size(uint8_t size) {
  return hal_status_to_bool(hal_display_set_text_size_ex(size));
}

hal_status_t hal_display_set_cursor_ex(int x, int y) {
  DisplayLock guard;
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    return gfx_unavailable_status("hal_display_set_cursor");
  }
  s_cursor_x = x;
  s_cursor_y = y;
  gfx->setCursor((int16_t)x, (int16_t)y);
  return HAL_OK;
}
bool hal_display_set_cursor(int x, int y) {
  return hal_status_to_bool(hal_display_set_cursor_ex(x, y));
}

hal_status_t hal_display_print_ex(const char *s) {
  DisplayLock guard;
  if (!s) {
    hal_derr("hal_display_print: text pointer is NULL");
    return HAL_EINVAL;
  }
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    return gfx_unavailable_status("hal_display_print");
  }
  gfx->print(s);
  s_cursor_x = gfx->getCursorX();
  s_cursor_y = gfx->getCursorY();
  return HAL_OK;
}
bool hal_display_print(const char *s) {
  return hal_status_to_bool(hal_display_print_ex(s));
}

hal_status_t hal_display_println_ex(const char *s) {
  DisplayLock guard;
  if (!s) {
    hal_derr("hal_display_println: text pointer is NULL");
    return HAL_EINVAL;
  }
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    return gfx_unavailable_status("hal_display_println");
  }
  gfx->print(s);
  gfx->write((uint8_t)'\n');
  s_cursor_x = gfx->getCursorX();
  s_cursor_y = gfx->getCursorY();
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
  if (!ensure_display_ready("hal_display_draw_text_centered")) {
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
  if (hal_status_is_ok(status)) {
    status = hal_display_set_text_color_ex(fg_color);
  }
  if (hal_status_is_ok(status)) {
    status = hal_display_print_at_ex((hal_display_get_width() - w) / 2,
                                     (hal_display_get_height() - h) / 2, text);
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
  DisplayLock guard;
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
  JHGfx *gfx = active_gfx();
  if (!gfx) {
    *out_w = 0;
    *out_h = 0;
    return gfx_unavailable_status("hal_display_get_text_bounds");
  }

  int16_t x1, y1;
  uint16_t bw, bh;
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &bw, &bh);
  *out_w = (int)bw;
  *out_h = (int)bh;
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
  hal_status_t status = hal_display_set_text_size_ex(1u);
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

#endif /* HAL_ENABLE_DISPLAY */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
