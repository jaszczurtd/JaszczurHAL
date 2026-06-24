/*
 * JaszczurHAL GFX Engine - Portable graphics primitives.
 *
 * Geometry algorithms, text rendering and bitmap drawing routines adapted
 * from the Adafruit GFX Library (Copyright (c) 2012-2013 Adafruit Industries,
 * BSD License).  The original library was designed as an Arduino C++ class
 * hierarchy.  This implementation preserves the proven rendering logic while
 * removing all platform-specific dependencies (Arduino.h, Print.h, PROGMEM,
 * avr/pgmspace.h) and expressing the API in terms of JaszczurHAL primitives
 * so that it can be shared across RP2040 and STM32 backends identically.
 *
 * Original work: https://github.com/adafruit/Adafruit-GFX-Library
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef JH_GFX_H
#define JH_GFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "jh_gfx_font.h"

#ifdef __cplusplus

class JHGfx {
public:
  JHGfx(int16_t w, int16_t h);
  virtual ~JHGfx() = default;

  // Pure-virtual pixel output - must be implemented by the backend.
  virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;

  // Transaction API - may be overridden for bulk-write optimisation.
  virtual void startWrite(void);
  virtual void endWrite(void);
  virtual void writePixel(int16_t x, int16_t y, uint16_t color);
  virtual void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                             uint16_t color);
  virtual void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
  virtual void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
  virtual void writeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         uint16_t color);

  // Basic draw API - may be overridden for hardware acceleration.
  virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
  virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
  virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                        uint16_t color);
  virtual void fillScreen(uint16_t color);
  virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                        uint16_t color);
  virtual void drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                        uint16_t color);

  // Control API
  virtual void setRotation(uint8_t r);
  virtual void invertDisplay(bool i);

  // Geometry
  void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
  void drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername,
                        uint16_t color);
  void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
  void fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners,
                        int16_t delta, uint16_t color);
  void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2,
                    int16_t y2, uint16_t color);
  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2,
                    int16_t y2, uint16_t color);
  void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                     uint16_t color);
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                     uint16_t color);

  // Bitmap
  void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w,
                  int16_t h, uint16_t color);
  void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w,
                  int16_t h, uint16_t color, uint16_t bg);
  void drawBitmap(int16_t x, int16_t y, uint8_t *bitmap, int16_t w, int16_t h,
                  uint16_t color);
  void drawBitmap(int16_t x, int16_t y, uint8_t *bitmap, int16_t w, int16_t h,
                  uint16_t color, uint16_t bg);
  void drawXBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w,
                   int16_t h, uint16_t color);
  void drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                           int16_t w, int16_t h);
  void drawGrayscaleBitmap(int16_t x, int16_t y, uint8_t *bitmap, int16_t w,
                           int16_t h);
  void drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                           const uint8_t mask[], int16_t w, int16_t h);
  void drawGrayscaleBitmap(int16_t x, int16_t y, uint8_t *bitmap, uint8_t *mask,
                           int16_t w, int16_t h);
  void drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w,
                     int16_t h);
  void drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w,
                     int16_t h);
  void drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[],
                     const uint8_t mask[], int16_t w, int16_t h);
  void drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, uint8_t *mask,
                     int16_t w, int16_t h);

  // Text
  void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color,
                uint16_t bg, uint8_t size);
  void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color,
                uint16_t bg, uint8_t size_x, uint8_t size_y);
  size_t write(uint8_t c);
  size_t write(const char *s);
  void print(const char *s);

  void getTextBounds(const char *str, int16_t x, int16_t y, int16_t *x1,
                     int16_t *y1, uint16_t *w, uint16_t *h);

  void setTextSize(uint8_t s);
  void setTextSize(uint8_t sx, uint8_t sy);
  void setFont(const GFXfont *f = NULL);

  void setCursor(int16_t x, int16_t y) {
    cursor_x = x;
    cursor_y = y;
  }
  void setTextColor(uint16_t c) { textcolor = textbgcolor = c; }
  void setTextColor(uint16_t c, uint16_t bg) {
    textcolor = c;
    textbgcolor = bg;
  }
  void setTextWrap(bool w) { wrap = w; }
  void cp437(bool x = true) { _cp437 = x; }

  int16_t width(void) const { return _width; }
  int16_t height(void) const { return _height; }
  uint8_t getRotation(void) const { return rotation; }
  int16_t getCursorX(void) const { return cursor_x; }
  int16_t getCursorY(void) const { return cursor_y; }

protected:
  void charBounds(unsigned char c, int16_t *x, int16_t *y, int16_t *minx,
                  int16_t *miny, int16_t *maxx, int16_t *maxy);

  int16_t WIDTH;   ///< Raw display width - never changes
  int16_t HEIGHT;  ///< Raw display height - never changes
  int16_t _width;  ///< Width modified by current rotation
  int16_t _height; ///< Height modified by current rotation
  int16_t cursor_x;
  int16_t cursor_y;
  uint16_t textcolor;
  uint16_t textbgcolor;
  uint8_t textsize_x;
  uint8_t textsize_y;
  uint8_t rotation;
  bool wrap;
  bool _cp437;
  GFXfont *gfxFont;
};

// --- Canvas classes (offscreen framebuffers) ---

class JHGfxCanvas1 : public JHGfx {
public:
  JHGfxCanvas1(uint16_t w, uint16_t h, bool allocate_buffer = true);
  ~JHGfxCanvas1(void);
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  bool getPixel(int16_t x, int16_t y) const;
  uint8_t *getBuffer(void) const { return buffer; }

protected:
  bool getRawPixel(int16_t x, int16_t y) const;
  void drawFastRawVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
  void drawFastRawHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
  uint8_t *buffer;
  bool buffer_owned;
};

class JHGfxCanvas8 : public JHGfx {
public:
  JHGfxCanvas8(uint16_t w, uint16_t h, bool allocate_buffer = true);
  ~JHGfxCanvas8(void);
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  uint8_t getPixel(int16_t x, int16_t y) const;
  uint8_t *getBuffer(void) const { return buffer; }

protected:
  uint8_t getRawPixel(int16_t x, int16_t y) const;
  void drawFastRawVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
  void drawFastRawHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
  uint8_t *buffer;
  bool buffer_owned;
};

class JHGfxCanvas16 : public JHGfx {
public:
  JHGfxCanvas16(uint16_t w, uint16_t h, bool allocate_buffer = true);
  ~JHGfxCanvas16(void);
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  uint16_t getPixel(int16_t x, int16_t y) const;
  void byteSwap(void);
  uint16_t *getBuffer(void) const { return buffer; }

protected:
  uint16_t getRawPixel(int16_t x, int16_t y) const;
  void drawFastRawVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
  void drawFastRawHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
  uint16_t *buffer;
  bool buffer_owned;
};

// Backward-compatible aliases for existing code using Adafruit-style names.
typedef JHGfxCanvas1 GFXcanvas1;
typedef JHGfxCanvas8 GFXcanvas8;
typedef JHGfxCanvas16 GFXcanvas16;

#endif // __cplusplus
#endif // JH_GFX_H
