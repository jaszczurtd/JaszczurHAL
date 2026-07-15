#pragma once

#include "hal_config.h"
#ifdef HAL_ENABLE_DISPLAY

/**
 * @file hal_display.h
 * @brief Hardware abstraction for TFT and OLED displays.
 *
 * Supports SPI TFT displays and SSD1306-family OLEDs over I2C/SPI.
 *
 * Backend selection (compile-time, opt-in):
 *
 *   HAL_ENABLE_TFT     - enable the SPI TFT family (requires one of the
 *                         HAL_ENABLE_ILI9341 / ST7789 / ST7735 / ST7796S
 *                         driver flags; propagates HAL_ENABLE_DISPLAY).
 *                         Without HAL_ENABLE_TFT, hal_display_init() and
 *                         hal_display_soft_init() are not available.
 *   HAL_ENABLE_SSD1306 - enable the SSD1306-family OLED driver (propagates
 *                         HAL_ENABLE_DISPLAY + HAL_ENABLE_I2C; SPI OLED
 *                         transport also needs HAL_ENABLE_SPI).
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
 * SSD1306-compatible OLEDs are initialized through either the historical I2C
 * helper or the configurable family helper (requires HAL_ENABLE_SSD1306 to be
 * defined): hal_display_init_ssd1306_i2c(...),
 * hal_display_init_ssd1306_family_ex(...)
 *
 * Typical usage:
 *   hal_display_init(CS, DC, RST);
 *   hal_display_configure(width, height, rotation, invert, bgr);
 *   // draw ...
 */

#include "hal_status.h"
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

/* ---- Raw buffer description --------------------------------------------- */

/*
 * Pixel formats used by the low-level display buffer/area-write layer.
 *
 * Multi-byte pixels are described in display-controller byte order. For the
 * common SPI TFT path that means RGB565_BE / already big-endian bytes when
 * streaming raw byte buffers. RGB565_NATIVE is provided for caller-owned
 * uint16_t buffers in the MCU native endianness, matching the existing
 * hal_display_write_pixels_fast() helper.
 *
 * Values are bit flags so a future capabilities struct can OR several
 * supported formats together.
 */
typedef enum {
  HAL_DISPLAY_PIXEL_FORMAT_NONE = 0u,
  HAL_DISPLAY_PIXEL_FORMAT_MONO01 = (1u << 0),    /* 0=black, 1=white */
  HAL_DISPLAY_PIXEL_FORMAT_MONO10 = (1u << 1),    /* 1=black, 0=white */
  HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE = (1u << 2), /* high byte first */
  HAL_DISPLAY_PIXEL_FORMAT_RGB565_NATIVE = (1u << 3),
  HAL_DISPLAY_PIXEL_FORMAT_RGB888 = (1u << 4),
  HAL_DISPLAY_PIXEL_FORMAT_BGR888 = (1u << 5),
  HAL_DISPLAY_PIXEL_FORMAT_L8 = (1u << 6),
} hal_display_pixel_format_t;

/*
 * Descriptor for a rectangular pixel buffer.
 *
 * - width/height describe the updated rectangle in pixels.
 * - pitch is the number of pixels between consecutive rows in the source
 *   buffer. It may be larger than width for sub-rectangles inside a larger
 *   framebuffer.
 * - buf_size is the available source-buffer size in bytes.
 * - frame_incomplete lets future streaming backends keep a panel transaction
 *   open across several area writes.
 */
typedef struct {
  hal_display_pixel_format_t pixel_format;
  uint16_t pitch;
  uint16_t width;
  uint16_t height;
  size_t buf_size;
  bool frame_incomplete;
} hal_display_buffer_desc_t;

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

#ifdef HAL_ENABLE_SSD1306
typedef enum {
  HAL_DISPLAY_OLED_CONTROLLER_SSD1306 = 0,
  HAL_DISPLAY_OLED_CONTROLLER_SSD1309,
  HAL_DISPLAY_OLED_CONTROLLER_SSD1315,
  HAL_DISPLAY_OLED_CONTROLLER_SH1106,
  HAL_DISPLAY_OLED_CONTROLLER_CH1115,
} hal_display_oled_controller_t;

typedef enum {
  HAL_DISPLAY_OLED_BUS_I2C = 0,
  HAL_DISPLAY_OLED_BUS_SPI,
} hal_display_oled_bus_t;

typedef enum {
  HAL_DISPLAY_OLED_ORIENTATION_NATIVE = 0,
  HAL_DISPLAY_OLED_ORIENTATION_ROTATED_180,
} hal_display_oled_orientation_t;

typedef struct {
  hal_display_oled_controller_t controller;
  hal_display_oled_bus_t bus_type;
  int width;
  int height;
  uint8_t bus;
  uint8_t i2c_addr;
  int16_t rst_pin;
  uint8_t switchvcc;
  uint32_t clock_hz;
  int16_t spi_dc_pin;
  int16_t spi_cs_pin;
  uint8_t spi_mode;
  uint8_t segment_offset;
  uint8_t page_offset;
  uint8_t display_offset;
  hal_display_oled_orientation_t orientation;
  bool internal_iref;
  bool periphBegin; /* Retained for Arduino-era I2C source compatibility. */
} hal_display_ssd1306_family_config_t;
#endif /* HAL_ENABLE_SSD1306 */

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
 * @return HAL_OK when the backend was prepared, HAL_EIO when immediate panel
 *         initialisation failed.
 */
hal_status_t hal_display_init(uint8_t cs, uint8_t dc, uint8_t rst);
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

/**
 * @brief Construct and initialise an SSD1306-family OLED.
 *
 * Supports SSD1306, SSD1309, SSD1315, SH1106 and CH1115 controller variants.
 * The config selects I2C or SPI transport, controller RAM offsets, controller
 * orientation and variant power/current-reference options. For I2C, the HAL
 * I2C peripheral is still initialised lazily. For SPI, configure the bus pins
 * with hal_spi_init() before calling this helper when the backend requires
 * explicit SPI pin assignment.
 *
 * @param config Family/bus/geometry configuration.
 * @return HAL_OK on success, HAL_EINVAL for invalid config, HAL_ENOMEM for
 *         framebuffer allocation failure, HAL_EUNSUPPORTED when the selected
 *         bus backend is not compiled in, or HAL_EIO for panel I/O failure.
 */
hal_status_t hal_display_init_ssd1306_family_ex(
    const hal_display_ssd1306_family_config_t *config);
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
 * @return HAL_OK on success/no-op, HAL_EIO when the backend sequence failed.
 */
hal_status_t hal_display_soft_init(int delay_ms);

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

/* ---- Status-returning APIs ---------------------------------------------- */
/*
 * Status-returning display APIs. Historical bool entry points above are thin
 * compatibility wrappers over these backend implementations. Functions that
 * historically returned void now return hal_status_t in place. Callers can
 * distinguish invalid arguments (HAL_EINVAL), an uninitialised backend
 * (HAL_EUNINIT), unsupported operations (HAL_EUNSUPPORTED), invalid stream
 * state (HAL_ESTATE), busy state (HAL_EBUSY), overflow (HAL_EOVERFLOW) and
 * backend I/O failure (HAL_EIO). Value-returning helpers expose their result
 * through an output parameter.
 *
 * Naming note: hal_display_init_ssd1306_i2c_ex() is already the historical
 * bus-selecting initialiser, so its status form is hal_display_init_ssd1306_
 * i2c_status_ex() (mirrors hal_wifi_ping_status_ex()). It is the single
 * status-returning SSD1306 entry point; pass i2c_bus = 0 for the primary bus.
 */

#ifdef HAL_ENABLE_SSD1306
hal_status_t
hal_display_init_ssd1306_i2c_status_ex(int width, int height, uint8_t i2c_bus,
                                       uint8_t i2c_addr, int8_t rst_pin,
                                       uint8_t switchvcc, bool periphBegin);
hal_status_t hal_display_init_ssd1306_family_ex(
    const hal_display_ssd1306_family_config_t *config);
#endif /* HAL_ENABLE_SSD1306 */

hal_status_t hal_display_configure_ex(int width, int height, uint8_t rotation,
                                      bool invert, bool bgr);
hal_status_t hal_display_suspend_ex(void);
hal_status_t hal_display_resume_ex(void);
hal_status_t hal_display_set_rotation_ex(uint8_t r);
hal_status_t hal_display_invert_ex(bool invert);
hal_status_t hal_display_get_width_ex(int *out_width);
hal_status_t hal_display_get_height_ex(int *out_height);

hal_status_t hal_display_fill_screen_ex(uint16_t color);
hal_status_t hal_display_flush_ex(void);
hal_status_t hal_display_draw_image_ex(int x, int y, int w, int h,
                                       uint16_t background, uint16_t *data);

hal_status_t hal_display_fill_rect_ex(int x, int y, int w, int h,
                                      uint16_t color);
hal_status_t hal_display_draw_rect_ex(int x, int y, int w, int h,
                                      uint16_t color);
hal_status_t hal_display_fill_circle_ex(int x, int y, int r, uint16_t color);
hal_status_t hal_display_draw_circle_ex(int x, int y, int r, uint16_t color);
hal_status_t hal_display_fill_round_rect_ex(int x, int y, int w, int h, int r,
                                            uint16_t color);
hal_status_t hal_display_draw_line_ex(int x0, int y0, int x1, int y1,
                                      uint16_t color);

hal_status_t hal_display_draw_rgb_bitmap_ex(int x, int y, uint16_t *data, int w,
                                            int h);
hal_status_t hal_display_begin_write_ex(int x, int y, int w, int h);
hal_status_t hal_display_write_pixels_fast_ex(const uint16_t *pixels,
                                              size_t count);
hal_status_t hal_display_write_pixels_be_ex(const uint8_t *pixels_be,
                                            size_t byte_count);
hal_status_t hal_display_write_pixels_dma_ex(const uint8_t *pixels_be,
                                             size_t byte_count);
hal_status_t
hal_display_write_pixels_dma_async_start_ex(const uint8_t *pixels_be,
                                            size_t byte_count);
hal_status_t hal_display_write_pixels_dma_async_wait_ex(void);
hal_status_t hal_display_end_write_ex(void);

hal_status_t hal_display_set_font_ex(hal_font_id_t font);
hal_status_t hal_display_set_text_color_ex(uint16_t color);
hal_status_t hal_display_set_text_size_ex(uint8_t size);
hal_status_t hal_display_set_cursor_ex(int x, int y);
hal_status_t hal_display_print_ex(const char *s);
hal_status_t hal_display_println_ex(const char *s);
hal_status_t hal_display_print_at_ex(int x, int y, const char *s);
hal_status_t hal_display_clear_text_line_ex(int line_index, int line_height,
                                            uint16_t bg_color);
hal_status_t hal_display_print_line_ex(int line_index, int line_height,
                                       const char *text, bool clear_first,
                                       uint16_t fg_color, uint16_t bg_color);
hal_status_t hal_display_draw_text_centered_ex(const char *text,
                                               uint16_t fg_color,
                                               uint16_t bg_color,
                                               bool clear_first,
                                               bool flush_after);
hal_status_t hal_display_get_text_bounds_ex(const char *s, int *w, int *h);
hal_status_t hal_display_text_width_ex(const char *text, int *out_width);
hal_status_t hal_display_text_height_ex(const char *text, int *out_height);
hal_status_t hal_display_println_prepared_text_ex(char *text);
hal_status_t hal_display_set_default_font_ex(void);
hal_status_t hal_display_set_default_font_with_pos_and_color_ex(int x, int y,
                                                                uint16_t color);
hal_status_t hal_display_set_text_size_one_with_color_ex(uint16_t color);
hal_status_t hal_display_set_sans_bold_with_pos_and_color_ex(int x, int y,
                                                             uint16_t color);
hal_status_t hal_display_set_serif9pt_with_color_ex(uint16_t color);
hal_status_t hal_display_prepare_text_ex(char *display_txt,
                                         size_t display_txt_size,
                                         int *out_width, const char *format,
                                         ...);
hal_status_t hal_display_prepare_text_v_ex(char *display_txt,
                                           size_t display_txt_size,
                                           int *out_width, const char *format,
                                           va_list args);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_DISPLAY */
