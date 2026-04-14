#include "../../hal_config.h"
#ifndef HAL_DISABLE_DISPLAY

#include "../../hal_display.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include <Arduino.h>
#include "drivers/Adafruit_GFX_Library/Adafruit_GFX.h"
#ifndef HAL_DISABLE_SSD1306
#include "drivers/Adafruit_SSD1306/Adafruit_SSD1306.h"
#include <Wire.h>
#endif
#include "drivers/Adafruit_GFX_Library/Fonts/FreeSansBold9pt7b.h"
#include "drivers/Adafruit_GFX_Library/Fonts/FreeSerif9pt7b.h"
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- Compile-time display driver selection --------------------------------
 *
 * Define exactly one of:
 *   HAL_DISPLAY_ILI9341
 *   HAL_DISPLAY_ST7789
 *   HAL_DISPLAY_ST7735
 *   HAL_DISPLAY_ST7796S
 *
 * For ST7735 you may also define HAL_DISPLAY_ST7735_TAB to override the
 * initR() tab-colour constant (defaults to INITR_BLACKTAB).
 *
 * To exclude all SPI TFT drivers define HAL_DISABLE_TFT.
 * To exclude the SSD1306 OLED driver define HAL_DISABLE_SSD1306.
 * At least one backend must be enabled (do not define both disable flags
 * at the same time — use HAL_DISABLE_DISPLAY instead).
 * -------------------------------------------------------------------------- */

#ifndef HAL_DISABLE_TFT
#if defined(HAL_DISPLAY_ILI9341)
    #include "drivers/Adafruit_ILI9341/Adafruit_ILI9341.h"
    using DisplayDriver = Adafruit_ILI9341;
#elif defined(HAL_DISPLAY_ST7789)
    #include "drivers/Adafruit_ST7735_and_ST7789_Library/Adafruit_ST7789.h"
    using DisplayDriver = Adafruit_ST7789;
#elif defined(HAL_DISPLAY_ST7735)
    #include "drivers/Adafruit_ST7735_and_ST7789_Library/Adafruit_ST7735.h"
    using DisplayDriver = Adafruit_ST7735;
    #ifndef HAL_DISPLAY_ST7735_TAB
        #define HAL_DISPLAY_ST7735_TAB INITR_BLACKTAB
    #endif
#elif defined(HAL_DISPLAY_ST7796S)
    #include "drivers/Adafruit_ST7735_and_ST7789_Library/Adafruit_ST7796S.h"
    using DisplayDriver = Adafruit_ST7796S;
#else
    #error "No TFT driver selected. Define one HAL_DISPLAY_* macro."
#endif
#endif /* !HAL_DISABLE_TFT */

/* ---- Static state -------------------------------------------------------- */

#ifndef HAL_DISABLE_TFT
static uint8_t s_tft_mem[sizeof(DisplayDriver)]
    __attribute__((aligned(__alignof__(DisplayDriver))));
// Keep concrete driver type so bitmap drawing resolves to Adafruit_SPITFT fast path.
static DisplayDriver *s_tft = NULL;
#endif /* !HAL_DISABLE_TFT */

#ifndef HAL_DISABLE_SSD1306
static uint8_t s_oled_mem[sizeof(Adafruit_SSD1306)]
    __attribute__((aligned(__alignof__(Adafruit_SSD1306))));
static Adafruit_SSD1306 *s_oled = NULL;
#endif /* !HAL_DISABLE_SSD1306 */

typedef enum {
    HAL_DISPLAY_BACKEND_NONE = 0,
    HAL_DISPLAY_BACKEND_TFT,
    HAL_DISPLAY_BACKEND_SSD1306,
} hal_display_backend_t;

static hal_display_backend_t s_backend = HAL_DISPLAY_BACKEND_NONE;
static int           s_width  = 0;
static int           s_height = 0;

/* ---- Multicore-safe display mutex ---------------------------------------- */
static hal_mutex_t s_display_mutex = NULL;

static void display_ensure_mutex(void) {
    if (!s_display_mutex) {
        hal_critical_section_enter();
        if (!s_display_mutex) {
            s_display_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

struct DisplayLock {
    DisplayLock()  { display_ensure_mutex(); hal_mutex_lock(s_display_mutex); }
    ~DisplayLock() { hal_mutex_unlock(s_display_mutex); }
};

/* -------------------------------------------------------------------------- */

static inline bool using_tft(void) {
#ifndef HAL_DISABLE_TFT
    return s_backend == HAL_DISPLAY_BACKEND_TFT && s_tft != NULL;
#else
    return false;
#endif
}

static inline bool using_oled(void) {
#ifndef HAL_DISABLE_SSD1306
    return s_backend == HAL_DISPLAY_BACKEND_SSD1306 && s_oled != NULL;
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

#ifndef HAL_DISABLE_SSD1306
static inline uint16_t map_color_to_oled(uint16_t color) {
    if (color == HAL_COLOR_BLACK) {
        return SSD1306_BLACK;
    }
    if (color == SSD1306_INVERSE) {
        return SSD1306_INVERSE;
    }
    return SSD1306_WHITE;
}

static TwoWire *display_i2c_wire(uint8_t bus) {
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
    return bus == 1 ? &Wire1 : &Wire;
#else
    (void)bus;
    return &Wire;
#endif
}
#endif /* !HAL_DISABLE_SSD1306 */

/* ---- ILI9341 extended init command table --------------------------------- */

#if defined(HAL_DISPLAY_ILI9341) && !defined(HAL_DISABLE_TFT)
static const uint8_t PROGMEM s_initcmd[] = {
    0xEF, 3, 0x03, 0x80, 0x02,
    0xCF, 3, 0x00, 0xC1, 0x30,
    0xED, 4, 0x64, 0x03, 0x12, 0x81,
    0xE8, 3, 0x85, 0x00, 0x78,
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
    0xF7, 1, 0x20,
    0xEA, 2, 0x00, 0x00,
    ILI9341_PWCTR1  , 1, 0x23,
    ILI9341_PWCTR2  , 1, 0x10,
    ILI9341_VMCTR1  , 2, 0x3e, 0x28,
    ILI9341_VMCTR2  , 1, 0x86,
    ILI9341_MADCTL  , 1, 0x48,
    ILI9341_VSCRSADD, 1, 0x00,
    ILI9341_PIXFMT  , 1, 0x55,
    ILI9341_FRMCTR1 , 2, 0x00, 0x18,
    ILI9341_DFUNCTR , 3, 0x08, 0x82, 0x27,
    0xF2, 1, 0x00,
    ILI9341_GAMMASET , 1, 0x01,
    ILI9341_GMCTRP1 , 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08,
        0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    ILI9341_GMCTRN1 , 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07,
        0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    ILI9341_SLPOUT  , 0x80,
    ILI9341_DISPON  , 0x80,
    0x00
};
#endif /* HAL_DISPLAY_ILI9341 && !HAL_DISABLE_TFT */

/* ---- Init / control ------------------------------------------------------ */

#ifndef HAL_DISABLE_TFT
void hal_display_init(uint8_t cs, uint8_t dc, uint8_t rst) {
    s_tft = new(s_tft_mem) DisplayDriver(cs, dc, rst);
    s_backend = HAL_DISPLAY_BACKEND_TFT;
#if defined(HAL_DISPLAY_ILI9341)
    /* ILI9341 does not need width/height; use configurable SPI clock. */
    s_tft->begin();
#endif
    /* For all other drivers, hardware init is deferred to
     * hal_display_configure() which carries the required dimensions. */
}
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
    s_oled = new(s_oled_mem) Adafruit_SSD1306(
        width, height, display_i2c_wire(i2c_bus), rst_pin
    );
    if (!s_oled->begin(switchvcc, i2c_addr, true, periphBegin)) {
        s_backend = HAL_DISPLAY_BACKEND_NONE;
        s_width = 0;
        s_height = 0;
        return false;
    }
    s_backend = HAL_DISPLAY_BACKEND_SSD1306;
    s_width = width;
    s_height = height;
    return true;
}
#endif /* !HAL_DISABLE_SSD1306 */

bool hal_display_configure(int width, int height, uint8_t rotation,
                            bool invert, bool bgr) {
    DisplayLock guard;
    if (width <= 0 || height <= 0) {
        hal_derr("hal_display_configure: invalid size %dx%d", width, height);
        return false;
    }

#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_width = width;
        s_height = height;
        s_oled->setRotation(rotation);
        s_oled->invertDisplay(invert);
        (void)bgr;
        return true;
    }
#endif

#ifndef HAL_DISABLE_TFT
    if (!using_tft()) {
        hal_derr("hal_display_configure: TFT backend is not initialized");
        return false;
    }
    s_width  = width;
    s_height = height;

#if defined(HAL_DISPLAY_ST7789)
    {
        s_tft->init(width, height);
        s_tft->invertDisplay(invert);
        (void)bgr;
    }
#elif defined(HAL_DISPLAY_ST7735)
    {
        s_tft->initR(HAL_DISPLAY_ST7735_TAB);
        s_tft->invertDisplay(invert);
        (void)bgr;
    }
#elif defined(HAL_DISPLAY_ST7796S)
    {
        /* ST7796S init() signature: init(height, width, …) — note the swap */
        s_tft->init(height, width, 0, 0, bgr ? ST7796S_BGR : 0);
        s_tft->invertDisplay(invert);
    }
#else /* ILI9341 — begin() already called in hal_display_init() */
    {
        s_tft->invertDisplay(invert);
        (void)bgr;
    }
#endif

    s_tft->setRotation(rotation);
    return true;
#else
    hal_derr("hal_display_configure: no active display backend");
    return false;
#endif /* !HAL_DISABLE_TFT */
}

void hal_display_soft_init(int delay_ms) {
    DisplayLock guard;
#if defined(HAL_DISPLAY_ILI9341) && !defined(HAL_DISABLE_TFT)
    if (!s_tft) return;
    uint8_t cmd, x, numArgs;
    const uint8_t *addr = s_initcmd;
    while ((cmd = pgm_read_byte(addr++)) > 0) {
        x       = pgm_read_byte(addr++);
        numArgs = x & 0x7F;
        s_tft->sendCommand(cmd, addr, numArgs);
        addr += numArgs;
        if (x & 0x80) {
            delay(delay_ms);
        }
    }
#else
    (void)delay_ms;
#endif
}

bool hal_display_set_rotation(uint8_t r) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_set_rotation")) return false;
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->setRotation(r);
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->setRotation(r);
        return true;
    }
#endif
    return false;
}

bool hal_display_invert(bool invert) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_invert")) return false;
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->invertDisplay(invert);
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->invertDisplay(invert);
        return true;
    }
#endif
    return false;
}

int hal_display_get_width(void)  { return s_width; }
int hal_display_get_height(void) { return s_height; }

/* ---- Screen -------------------------------------------------------------- */

bool hal_display_fill_screen(uint16_t color) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_fill_screen")) return false;
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->fillScreen(map_color_to_oled(color));
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->fillScreen(color);
        return true;
    }
#endif
    return false;
}

bool hal_display_flush(void) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_flush")) return false;
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->display();
    }
#endif
    return true;
}

bool hal_display_draw_image(int x, int y, int w, int h,
                            uint16_t background, uint16_t *data) {
    bool ok = hal_display_fill_rect(x, y, w, h, background);
    ok &= hal_display_draw_rgb_bitmap(x, y, data, w, h);
    return ok;
}

/* ---- Geometry ------------------------------------------------------------ */

bool hal_display_fill_rect(int x, int y, int w, int h, uint16_t color) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_fill_rect")) return false;
    if (w <= 0 || h <= 0) {
        hal_derr("hal_display_fill_rect: invalid size %dx%d", w, h);
        return false;
    }
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->fillRect(x, y, w, h, map_color_to_oled(color));
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->fillRect(x, y, w, h, color);
        return true;
    }
#endif
    return false;
}

bool hal_display_draw_rect(int x, int y, int w, int h, uint16_t color) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_draw_rect")) return false;
    if (w <= 0 || h <= 0) {
        hal_derr("hal_display_draw_rect: invalid size %dx%d", w, h);
        return false;
    }
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->drawRect(x, y, w, h, map_color_to_oled(color));
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->drawRect(x, y, w, h, color);
        return true;
    }
#endif
    return false;
}

bool hal_display_fill_circle(int x, int y, int r, uint16_t color) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_fill_circle")) return false;
    if (r < 0) {
        hal_derr("hal_display_fill_circle: invalid radius %d", r);
        return false;
    }
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->fillCircle(x, y, r, map_color_to_oled(color));
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->fillCircle(x, y, r, color);
        return true;
    }
#endif
    return false;
}

bool hal_display_draw_circle(int x, int y, int r, uint16_t color) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_draw_circle")) return false;
    if (r < 0) {
        hal_derr("hal_display_draw_circle: invalid radius %d", r);
        return false;
    }
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->drawCircle(x, y, r, map_color_to_oled(color));
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->drawCircle(x, y, r, color);
        return true;
    }
#endif
    return false;
}

bool hal_display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_fill_round_rect")) return false;
    if (w <= 0 || h <= 0 || r < 0) {
        hal_derr("hal_display_fill_round_rect: invalid size/radius w=%d h=%d r=%d", w, h, r);
        return false;
    }
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->fillRoundRect(x, y, w, h, r, map_color_to_oled(color));
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->fillRoundRect(x, y, w, h, r, color);
        return true;
    }
#endif
    return false;
}

bool hal_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_draw_line")) return false;
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->drawLine(x0, y0, x1, y1, map_color_to_oled(color));
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->drawLine(x0, y0, x1, y1, color);
        return true;
    }
#endif
    return false;
}

/* ---- Bitmap -------------------------------------------------------------- */

bool hal_display_draw_rgb_bitmap(int x, int y, uint16_t *data, int w, int h) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_draw_rgb_bitmap")) return false;
    if (!data) {
        hal_derr("hal_display_draw_rgb_bitmap: data pointer is NULL");
        return false;
    }
    if (w <= 0 || h <= 0) {
        hal_derr("hal_display_draw_rgb_bitmap: invalid size %dx%d", w, h);
        return false;
    }
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->drawRGBBitmap(x, y, data, w, h);
        return true;
    }
#endif
    hal_derr("hal_display_draw_rgb_bitmap: unsupported on current backend");
    return false;
}

/* ---- Text ---------------------------------------------------------------- */

bool hal_display_set_font(hal_font_id_t font) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_set_font")) return false;
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        switch (font) {
            case HAL_FONT_SANS_BOLD_9PT: s_oled->setFont(&FreeSansBold9pt7b); break;
            case HAL_FONT_SERIF_9PT:     s_oled->setFont(&FreeSerif9pt7b);    break;
            default:                     s_oled->setFont();                    break;
        }
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (!using_tft()) return false;
    switch (font) {
        case HAL_FONT_SANS_BOLD_9PT: s_tft->setFont(&FreeSansBold9pt7b); break;
        case HAL_FONT_SERIF_9PT:     s_tft->setFont(&FreeSerif9pt7b);    break;
        default:                     s_tft->setFont();                    break;
    }
    return true;
#else
    return false;
#endif
}

bool hal_display_set_text_color(uint16_t color) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_set_text_color")) return false;
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->setTextColor(map_color_to_oled(color));
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->setTextColor(color);
        return true;
    }
#endif
    return false;
}

bool hal_display_set_text_size(uint8_t size) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_set_text_size")) return false;
    if (size == 0) {
        hal_derr("hal_display_set_text_size: size must be > 0");
        return false;
    }
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->setTextSize(size);
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->setTextSize(size);
        return true;
    }
#endif
    return false;
}

bool hal_display_set_cursor(int x, int y) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_set_cursor")) return false;
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->setCursor(x, y);
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->setCursor(x, y);
        return true;
    }
#endif
    return false;
}

bool hal_display_print(const char *s) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_print")) return false;
    if (!s) {
        hal_derr("hal_display_print: text pointer is NULL");
        return false;
    }
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->print(s);
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->print(s);
        return true;
    }
#endif
    return false;
}

bool hal_display_println(const char *s) {
    DisplayLock guard;
    if (!ensure_display_ready("hal_display_println")) return false;
    if (!s) {
        hal_derr("hal_display_println: text pointer is NULL");
        return false;
    }
#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        s_oled->println(s);
        return true;
    }
#endif
#ifndef HAL_DISABLE_TFT
    if (using_tft()) {
        s_tft->println(s);
        return true;
    }
#endif
    return false;
}

bool hal_display_print_at(int x, int y, const char *s) {
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
    const int width = hal_display_get_width();
    if (width <= 0) {
        hal_derr("hal_display_clear_text_line: display width is not configured");
        return false;
    }
    const int y = line_index * line_height;
    return hal_display_fill_rect(0, y, width, line_height, bg_color);
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
    const int y = line_index * line_height;
    bool ok = true;
    if (clear_first) {
        ok &= hal_display_clear_text_line(line_index, line_height, bg_color);
    }
    ok &= hal_display_set_text_color(fg_color);
    ok &= hal_display_print_at(0, y, text);
    return ok;
}

bool hal_display_draw_text_centered(const char *text, uint16_t fg_color,
                                    uint16_t bg_color, bool clear_first,
                                    bool flush_after) {
    if (!text) {
        hal_derr("hal_display_draw_text_centered: text pointer is NULL");
        return false;
    }
    if (!ensure_display_ready("hal_display_draw_text_centered")) return false;

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
    if (!ensure_display_ready("hal_display_get_text_bounds")) {
        *out_w = 0;
        *out_h = 0;
        return false;
    }

#ifndef HAL_DISABLE_SSD1306
    if (using_oled()) {
        int16_t x1, y1;
        uint16_t uw, uh;
        s_oled->getTextBounds(s, 0, 0, &x1, &y1, &uw, &uh);
        *out_w = (int)uw;
        *out_h = (int)uh;
        return true;
    }
#endif

#ifndef HAL_DISABLE_TFT
    if (!using_tft()) { *out_w = 0; *out_h = 0; return false; }
    int16_t x1, y1;
    uint16_t uw, uh;
    s_tft->getTextBounds(s, 0, 0, &x1, &y1, &uw, &uh);
    *out_w = (int)uw;
    *out_h = (int)uh;
    return true;
#else
    *out_w = 0;
    *out_h = 0;
    return false;
#endif
}

int hal_display_text_width(const char *text) {
    int w = 0, h = 0;
    hal_display_get_text_bounds(text, &w, &h);
    return w;
}

int hal_display_text_height(const char *text) {
    int w = 0, h = 0;
    hal_display_get_text_bounds(text, &w, &h);
    return h;
}

bool hal_display_println_prepared_text(char *text) {
    return hal_display_println(text);
}

bool hal_display_set_default_font(void) {
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

bool hal_display_set_text_size_one_with_color(uint16_t color) {
    bool ok = true;
    ok &= hal_display_set_text_size(1);
    ok &= hal_display_set_text_color(color);
    return ok;
}

bool hal_display_set_sans_bold_with_pos_and_color(int x, int y, uint16_t color) {
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

    int w = 0, h = 0;
    hal_display_get_text_bounds(display_txt, &w, &h);
    return w;
}

int hal_display_prepare_text(char *display_txt, size_t display_txt_size,
                             const char *format, ...) {
    va_list args;
    va_start(args, format);
    int w = hal_display_prepare_text_v(display_txt, display_txt_size, format, args);
    va_end(args);
    return w;
}

#endif /* HAL_DISABLE_DISPLAY */
