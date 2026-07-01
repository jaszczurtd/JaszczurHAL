#pragma once

#include "hal_config.h"
#ifdef HAL_ENABLE_DISPLAY

/**
 * @file hal_display.h
 * @brief Hardware abstraction for TFT and OLED displays.
 *
 * Supports SPI TFT displays and SSD1306 OLED over I2C.
 *
 * Backend selection (compile-time, opt-in):
 *
 *   HAL_ENABLE_TFT     - enable the SPI TFT family (requires one of the
 *                         HAL_ENABLE_ILI9341 / ST7789 / ST7735 / ST7796S
 *                         driver flags; propagates HAL_ENABLE_DISPLAY).
 *                         Without HAL_ENABLE_TFT, hal_display_init() and
 *                         hal_display_soft_init() are not available.
 *   HAL_ENABLE_SSD1306 - enable the SSD1306 OLED driver (propagates
 *                         HAL_ENABLE_DISPLAY).
 *                         Without it, hal_display_init_ssd1306_i2c() is
 *                         not available.
 *
 * Both flags may be enabled simultaneously; the runtime entry points
 * stay independent. Enabling HAL_ENABLE_DISPLAY alone (without a TFT
 * driver or SSD1306) triggers a compile-time #error from hal_config.h.
 *
 * TFT drivers are selected at compile time (ignored when HAL_ENABLE_TFT is
 * unset): Define exactly one of the following before including this header (or
 * in the build system):
 *
 *   HAL_DISPLAY_ILI9341  - 240×320
 *   HAL_DISPLAY_ST7789   - variable size (pass w/h to hal_display_configure)
 *   HAL_DISPLAY_ST7735   - 128×160 typical; set HAL_DISPLAY_ST7735_TAB to
 *                          override initR() tab-colour (default INITR_BLACKTAB)
 *   HAL_DISPLAY_ST7796S  - 320×480 typical, BGR colour order
 *
 * Per-driver exclusion flags (from hal_config.h):
 *   HAL_ENABLE_ILI9341 / HAL_ENABLE_ST7789 / HAL_ENABLE_ST7735 /
 * HAL_ENABLE_ST7796S
 *
 * There is no implicit TFT default driver. The project must explicitly define
 * exactly one HAL_DISPLAY_* macro when TFT backend is enabled.
 *
 * SSD1306 is initialized through the dedicated I2C helper (requires
 * HAL_ENABLE_SSD1306 to be undefined): hal_display_init_ssd1306_i2c(...)
 *
 * Typical usage:
 *   hal_display_init(CS, DC, RST);
 *   hal_display_configure(width, height, rotation, invert, bgr);
 *   // draw ...
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAL_ENABLE_TFT
#if (((defined(HAL_DISPLAY_ILI9341) ? 1 : 0) +                                 \
      (defined(HAL_DISPLAY_ST7789) ? 1 : 0) +                                  \
      (defined(HAL_DISPLAY_ST7735) ? 1 : 0) +                                  \
      (defined(HAL_DISPLAY_ST7796S) ? 1 : 0)) > 1)
#error "Define exactly one HAL_DISPLAY_* macro (multiple selected)."
#endif

#if !defined(HAL_DISPLAY_ILI9341) && !defined(HAL_DISPLAY_ST7789) &&           \
    !defined(HAL_DISPLAY_ST7735) && !defined(HAL_DISPLAY_ST7796S)
#error                                                                         \
    "No TFT driver selected. Define one HAL_DISPLAY_* macro or unset HAL_ENABLE_TFT."
#endif

#if defined(HAL_DISPLAY_ILI9341) && !defined(HAL_ENABLE_ILI9341)
#error "HAL_DISPLAY_ILI9341 selected, but HAL_ENABLE_ILI9341 is not defined."
#endif
#if defined(HAL_DISPLAY_ST7789) && !defined(HAL_ENABLE_ST7789)
#error "HAL_DISPLAY_ST7789 selected, but HAL_ENABLE_ST7789 is not defined."
#endif
#if defined(HAL_DISPLAY_ST7735) && !defined(HAL_ENABLE_ST7735)
#error "HAL_DISPLAY_ST7735 selected, but HAL_ENABLE_ST7735 is not defined."
#endif
#if defined(HAL_DISPLAY_ST7796S) && !defined(HAL_ENABLE_ST7796S)
#error "HAL_DISPLAY_ST7796S selected, but HAL_ENABLE_ST7796S is not defined."
#endif
#endif /* HAL_ENABLE_TFT */

/* ---- Common RGB565 colors ----------------------------------------------- */
/*
 * These constants are display-controller agnostic. They represent standard
 * RGB565 values and can be used with ILI9341/ST7789/ST7735/ST7796S alike.
 *
 * Why guarded with #ifndef:
 * - allows projects to provide compatible aliases before including this file
 * - prevents redefinition warnings when headers are combined
 */

#ifndef HAL_COLOR_BLACK
#define HAL_COLOR_BLACK 0x0000
#define HAL_COLOR_WHITE 0xFFFF
#define HAL_COLOR_RED 0xF800
#define HAL_COLOR_GREEN 0x07E0
#define HAL_COLOR_BLUE 0x001F
#define HAL_COLOR_ORANGE 0xFD20
#define HAL_COLOR_PURPLE 0x780F
#define HAL_COLOR_YELLOW 0xFFE0
#define HAL_COLOR_CYAN 0x07FF
#endif

#ifndef HAL_COLOR
/*
 * Convenience selector:
 *   HAL_COLOR(RED)    -> HAL_COLOR_RED
 *   HAL_COLOR(WHITE)  -> HAL_COLOR_WHITE
 *
 * Note: pass only the symbolic suffix (RED/WHITE/...), not full macro names.
 */
#define HAL_COLOR(name) HAL_COLOR_##name
#endif

/* ---- Display orientation / mode ----------------------------------------- */

/*
 * Rotation values accepted by the shared GFX engine (JHGfx::setRotation()).
 *
 * 0   : native orientation
 * 90  : quarter-turn clockwise
 * 180 : upside-down
 * 270 : quarter-turn counter-clockwise
 */
typedef enum {
  HAL_DISPLAY_ROTATION_0 = 0,
  HAL_DISPLAY_ROTATION_90 = 1,
  HAL_DISPLAY_ROTATION_180 = 2,
  HAL_DISPLAY_ROTATION_270 = 3,
} hal_display_rotation_t;

/*
 * Maps degrees (0/90/180/270) to HAL enum values.
 *
 * Invalid input falls back to HAL_DISPLAY_ROTATION_0 to keep behavior safe.
 * Prefer compile-time literals (0, 90, 180, 270) for readability.
 */
#define HAL_DISPLAY_ROTATION(deg)                                              \
  ((uint8_t)(((deg) == 0)     ? HAL_DISPLAY_ROTATION_0                         \
             : ((deg) == 90)  ? HAL_DISPLAY_ROTATION_90                        \
             : ((deg) == 180) ? HAL_DISPLAY_ROTATION_180                       \
             : ((deg) == 270) ? HAL_DISPLAY_ROTATION_270                       \
                              : HAL_DISPLAY_ROTATION_0))

/*
 * Inversion flags used by hal_display_configure(..., invert, ...).
 * ON means logical color inversion performed by the display controller.
 */
#define HAL_DISPLAY_INVERT_OFF false
#define HAL_DISPLAY_INVERT_ON true

/*
 * Color-order flags used by hal_display_configure(..., bgr).
 * RGB: standard red/green/blue order.
 * BGR: blue/green/red order (required by some controller/panel variants).
 */
#define HAL_DISPLAY_COLOR_ORDER_RGB false
#define HAL_DISPLAY_COLOR_ORDER_BGR true

/* ---- SSD1306 power mode ------------------------------------------------- */

#ifndef HAL_DISPLAY_VCC_EXTERNAL
#define HAL_DISPLAY_VCC_EXTERNAL 0x01
#define HAL_DISPLAY_VCC_SWITCHCAP 0x02
#endif

/** @brief Available font identifiers. */
typedef enum {
  HAL_FONT_DEFAULT = 0,   /**< Built-in default font. */
  HAL_FONT_SANS_BOLD_9PT, /**< FreeSansBold 9pt. */
  HAL_FONT_SERIF_9PT,     /**< FreeSerif 9pt. */
} hal_font_id_t;

/* ---- Init / control ---- */

#ifdef HAL_ENABLE_TFT
/**
 * @brief Construct the display object and start the SPI driver.
 *
 * Only available when HAL_ENABLE_TFT is defined.
 *
 * For ILI9341 this also calls begin().  For all other drivers the full
 * hardware initialisation is deferred to hal_display_configure() because
 * those drivers require width/height parameters.
 *
 * @param cs  Chip-select pin.
 * @param dc  Data/command pin.
 * @param rst Reset pin.
 */
void hal_display_init(uint8_t cs, uint8_t dc, uint8_t rst);
#endif /* HAL_ENABLE_TFT */

#ifdef HAL_ENABLE_SSD1306
/**
 * @brief Construct and initialise an SSD1306 OLED connected via I2C.
 *
 * Only available when HAL_ENABLE_SSD1306 is defined.
 *
 * This helper is intended for monochrome SSD1306 modules connected over HAL
 * I2C. The I2C peripheral should be configured before calling this function
 * (for example via hal_i2c_init()).
 *
 * @param width       Display width in pixels (typically 128).
 * @param height      Display height in pixels (typically 32 or 64).
 * @param i2c_addr    7-bit I2C address (for example 0x3C).
 * @param rst_pin     Reset pin, or -1 when reset is not connected.
 * @param switchvcc   SSD1306 power mode (HAL_DISPLAY_VCC_SWITCHCAP or
 * HAL_DISPLAY_VCC_EXTERNAL).
 * @param periphBegin Retained for source compatibility; the HAL I2C bus is
 *                    initialised lazily, so this flag has no effect.
 * @return true when initialisation succeeded.
 */
bool hal_display_init_ssd1306_i2c(int width, int height, uint8_t i2c_addr,
                                  int8_t rst_pin, uint8_t switchvcc,
                                  bool periphBegin);

/**
 * @brief Construct and initialise SSD1306 OLED on selected I2C bus.
 *
 * @param width       Display width in pixels.
 * @param height      Display height in pixels.
 * @param i2c_bus     I2C bus index (0 = primary bus, 1 = secondary bus).
 * @param i2c_addr    7-bit I2C address.
 * @param rst_pin     Reset pin, or -1 when not connected.
 * @param switchvcc   SSD1306 power mode.
 * @param periphBegin Retained for source compatibility; has no effect.
 * @return true when initialisation succeeded.
 */
bool hal_display_init_ssd1306_i2c_ex(int width, int height, uint8_t i2c_bus,
                                     uint8_t i2c_addr, int8_t rst_pin,
                                     uint8_t switchvcc, bool periphBegin);
#endif /* HAL_ENABLE_SSD1306 */

/**
 * @brief Configure display dimensions, colour order, rotation and inversion.
 *
 * For ST7789 / ST7735 / ST7796S this performs the full hardware init
 * (init() / initR()) in addition to applying the options.
 * For ILI9341 it applies options on top of the already-running begin().
 * Always call this after hal_display_init().
 *
 * @param width    Logical width in pixels (before rotation is applied).
 * @param height   Logical height in pixels (before rotation is applied).
 * @param rotation Screen rotation 0-3.
 * @param invert   true to invert display colours.
 * @param bgr      true to use BGR colour order (required for ST7796S).
 */
bool hal_display_configure(int width, int height, uint8_t rotation, bool invert,
                           bool bgr);

/**
 * @brief Re-send the backend register-init command sequence when available.
 * @param delay_ms ILI9341 delay in milliseconds between commands that request
 *                 it. ST77xx sequences carry per-command delay values.
 *                 No-op for display types without a soft-init path.
 */
void hal_display_soft_init(int delay_ms);

/**
 * @brief Set the display rotation (0-3).
 * @param r Rotation value.
 */
bool hal_display_set_rotation(uint8_t r);

/**
 * @brief Invert (or un-invert) display colours at runtime.
 * @param invert true to enable colour inversion.
 */
bool hal_display_invert(bool invert);

/**
 * @brief Return the display width that was set via hal_display_configure().
 * @return Width in pixels, or 0 if not yet configured.
 */
int hal_display_get_width(void);

/**
 * @brief Return the display height that was set via hal_display_configure().
 * @return Height in pixels, or 0 if not yet configured.
 */
int hal_display_get_height(void);

/* ---- Screen ---- */

/**
 * @brief Fill the entire screen with a single colour.
 * @param color RGB565 colour value.
 */
bool hal_display_fill_screen(uint16_t color);

/**
 * @brief Flush pending drawing operations to the physical display.
 *
 * For buffered displays (SSD1306) this sends the framebuffer to the panel.
 * For immediate-mode TFT drivers this is a no-op.
 */
bool hal_display_flush(void);

/**
 * @brief Draw bitmap with background clear in a single helper call.
 * @param x,y        Top-left corner.
 * @param w,h        Width and height.
 * @param background RGB565 background color.
 * @param data       Pointer to RGB565 bitmap data.
 */
bool hal_display_draw_image(int x, int y, int w, int h, uint16_t background,
                            uint16_t *data);

/* ---- Geometry ---- */

/**
 * @brief Draw a filled rectangle.
 * @param x,y  Top-left corner.
 * @param w,h  Width and height.
 * @param color RGB565 colour.
 */
bool hal_display_fill_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief Draw a rectangle outline.
 * @param x,y  Top-left corner.
 * @param w,h  Width and height.
 * @param color RGB565 colour.
 */
bool hal_display_draw_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief Draw a filled circle.
 * @param x,y  Centre coordinates.
 * @param r    Radius.
 * @param color RGB565 colour.
 */
bool hal_display_fill_circle(int x, int y, int r, uint16_t color);

/**
 * @brief Draw a circle outline.
 * @param x,y  Centre coordinates.
 * @param r    Radius.
 * @param color RGB565 colour.
 */
bool hal_display_draw_circle(int x, int y, int r, uint16_t color);

/**
 * @brief Draw a filled rounded rectangle.
 * @param x,y  Top-left corner.
 * @param w,h  Width and height.
 * @param r    Corner radius.
 * @param color RGB565 colour.
 */
bool hal_display_fill_round_rect(int x, int y, int w, int h, int r,
                                 uint16_t color);

/**
 * @brief Draw a line between two points.
 * @param x0,y0 Start point.
 * @param x1,y1 End point.
 * @param color  RGB565 colour.
 */
bool hal_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color);

/* ---- Bitmap ---- */

/**
 * @brief Draw an RGB565 bitmap at the given position.
 * @param x,y  Top-left corner.
 * @param data Pointer to pixel data.
 * @param w,h  Width and height of the bitmap.
 */
bool hal_display_draw_rgb_bitmap(int x, int y, uint16_t *data, int w, int h);

/**
 * @brief Set a TFT address window and begin streaming RGB565 pixel data.
 *
 * The stream keeps the display/SPI transaction open until
 * hal_display_end_write() is called. This is intended for large contiguous
 * updates such as full frames or scanline batches where per-pixel or
 * per-scanline window setup would dominate runtime.
 *
 * Only immediate-mode TFT backends support this API.
 *
 * @param x,y Top-left corner.
 * @param w,h Width and height of the stream window.
 * @return true when the stream is open.
 */
bool hal_display_begin_write(int x, int y, int w, int h);

/**
 * @brief Stream native-endian RGB565 pixels into an open TFT write window.
 *
 * Pixel words are converted to the big-endian byte order expected by common
 * TFT controllers, then written in large chunks without changing the address
 * window.
 *
 * @param pixels Native-endian RGB565 pixel words.
 * @param count Number of pixels.
 * @return true when the pixels were written.
 */
bool hal_display_write_pixels_fast(const uint16_t *pixels, size_t count);

/**
 * @brief Stream already big-endian RGB565 bytes into an open TFT write window.
 *
 * This path performs no byte swapping and no extra chunking.
 *
 * @param pixels_be RGB565 bytes in controller order: high byte, low byte.
 * @param byte_count Number of bytes to write. Must be even.
 * @return true when the bytes were written.
 */
bool hal_display_write_pixels_be(const uint8_t *pixels_be, size_t byte_count);

/**
 * @brief Stream big-endian RGB565 bytes through the backend DMA fast path.
 *
 * On RP2040 this uses SPI TX DMA. Other backends fall back to a blocking SPI
 * write while preserving the same public contract.
 *
 * @param pixels_be RGB565 bytes in controller order: high byte, low byte.
 * @param byte_count Number of bytes to write. Must be even.
 * @return true when the bytes were written.
 */
bool hal_display_write_pixels_dma(const uint8_t *pixels_be, size_t byte_count);

/**
 * @brief Start streaming big-endian RGB565 bytes through async DMA.
 *
 * When the backend supports asynchronous SPI DMA this returns before the bytes
 * have left the bus. Keep @p pixels_be valid until
 * hal_display_write_pixels_dma_async_wait() completes and do not start another
 * async display write before the previous one is done. Backends without async
 * DMA complete the transfer before returning.
 */
bool hal_display_write_pixels_dma_async_start(const uint8_t *pixels_be,
                                              size_t byte_count);

/**
 * @brief Return true while an asynchronous display DMA write is still active.
 */
bool hal_display_write_pixels_dma_async_busy(void);

/**
 * @brief Wait for the asynchronous display DMA write to complete.
 */
bool hal_display_write_pixels_dma_async_wait(void);

/**
 * @brief Finish the currently open TFT write stream.
 * @return true when the stream was closed cleanly.
 */
bool hal_display_end_write(void);

/* ---- Text ---- */

/**
 * @brief Select the active font.
 * @param font Font identifier from hal_font_id_t.
 */
bool hal_display_set_font(hal_font_id_t font);

/**
 * @brief Set the text foreground colour.
 * @param color RGB565 colour.
 */
bool hal_display_set_text_color(uint16_t color);

/**
 * @brief Set the text magnification factor.
 * @param size Scale factor (1 = normal).
 */
bool hal_display_set_text_size(uint8_t size);

/**
 * @brief Set the text cursor position.
 * @param x,y Cursor coordinates in pixels.
 */
bool hal_display_set_cursor(int x, int y);

/**
 * @brief Print a string at the current cursor position.
 * @param s Null-terminated string.
 */
bool hal_display_print(const char *s);

/**
 * @brief Print a string followed by a newline.
 * @param s Null-terminated string.
 */
bool hal_display_println(const char *s);

/**
 * @brief Print a string at a given position.
 * @param x Horizontal position in pixels.
 * @param y Vertical position in pixels.
 * @param s Null-terminated string.
 */
bool hal_display_print_at(int x, int y, const char *s);

/**
 * @brief Clear one full-width text line.
 * @param line_index 0-based line index.
 * @param line_height Line height in pixels.
 * @param bg_color Background color.
 */
bool hal_display_clear_text_line(int line_index, int line_height,
                                 uint16_t bg_color);

/**
 * @brief Print text in a line grid with optional clear.
 * @param line_index 0-based line index.
 * @param line_height Line height in pixels.
 * @param text Null-terminated string.
 * @param clear_first true to clear target line before printing.
 * @param fg_color Text color.
 * @param bg_color Background color used when clear_first is true.
 */
bool hal_display_print_line(int line_index, int line_height, const char *text,
                            bool clear_first, uint16_t fg_color,
                            uint16_t bg_color);

/**
 * @brief Draw centered text using current display dimensions.
 * @param text Null-terminated string.
 * @param fg_color Text color.
 * @param bg_color Background color.
 * @param clear_first true to clear the screen before drawing.
 * @param flush_after true to call hal_display_flush() after drawing.
 */
bool hal_display_draw_text_centered(const char *text, uint16_t fg_color,
                                    uint16_t bg_color, bool clear_first,
                                    bool flush_after);

/**
 * @brief Get the bounding box of a string in pixels.
 * @param s       Null-terminated string.
 * @param[out] w  Width in pixels.
 * @param[out] h  Height in pixels.
 */
bool hal_display_get_text_bounds(const char *s, int *w, int *h);

/**
 * @brief Get rendered text width in pixels.
 * @param text Null-terminated string.
 * @return Width in pixels.
 */
int hal_display_text_width(const char *text);

/**
 * @brief Get rendered text height in pixels.
 * @param text Null-terminated string.
 * @return Height in pixels.
 */
int hal_display_text_height(const char *text);

/**
 * @brief Print already formatted text with newline.
 * @param text Null-terminated string.
 */
bool hal_display_println_prepared_text(char *text);

/**
 * @brief Apply default font and size 1.
 */
bool hal_display_set_default_font(void);

/**
 * @brief Apply default font, text color and cursor position.
 * @param x,y   Cursor position.
 * @param color RGB565 color.
 */
bool hal_display_set_default_font_with_pos_and_color(int x, int y,
                                                     uint16_t color);

/**
 * @brief Apply text size 1 and given color.
 * @param color RGB565 color.
 */
bool hal_display_set_text_size_one_with_color(uint16_t color);

/**
 * @brief Apply sans-bold 9pt font, set cursor and text style.
 * @param x,y   Cursor position.
 * @param color RGB565 color.
 */
bool hal_display_set_sans_bold_with_pos_and_color(int x, int y, uint16_t color);

/**
 * @brief Apply serif 9pt font and text style.
 * @param color RGB565 color.
 */
bool hal_display_set_serif9pt_with_color(uint16_t color);

/**
 * @brief Format text into a caller-provided buffer and return rendered width.
 * @param display_txt      Destination buffer.
 * @param display_txt_size Destination buffer size in bytes.
 * @param format           printf-like format string.
 * @return Rendered text width in pixels.
 */
int hal_display_prepare_text(char *display_txt, size_t display_txt_size,
                             const char *format, ...);

/**
 * @brief va_list variant of hal_display_prepare_text().
 * @param display_txt      Destination buffer.
 * @param display_txt_size Destination buffer size in bytes.
 * @param format           printf-like format string.
 * @param args             Variadic argument list.
 * @return Rendered text width in pixels.
 */
int hal_display_prepare_text_v(char *display_txt, size_t display_txt_size,
                               const char *format, va_list args);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_DISPLAY */
