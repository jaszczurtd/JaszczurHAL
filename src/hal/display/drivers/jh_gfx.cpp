/*
 * JaszczurHAL GFX Engine - Implementation.
 *
 * Portable graphics primitives: line drawing (Bresenham), circles (midpoint),
 * triangles (scanline fill), rounded rectangles, bitmap rendering, and text
 * layout with support for proportional GFXfont fonts as well as the classic
 * fixed 5x7 bitmap font.
 *
 * Rendering algorithms adapted from the Adafruit GFX Library
 * (Copyright (c) 2012-2013 Adafruit Industries, BSD-2-Clause License).
 * See jh_gfx.h header for full attribution.
 */

#include "jh_gfx.h"

#include <stdlib.h>
#include <string.h>

// ---- Built-in 5x7 font (classic fixed-space bitmap) -------------------------
#include "jh_gfx_glcdfont.h"

// ---- Helpers ----------------------------------------------------------------

#ifndef _jh_swap_int16
#define _jh_swap_int16(a, b)                                                   \
  {                                                                            \
    int16_t t = (a);                                                           \
    (a) = (b);                                                                 \
    (b) = t;                                                                   \
  }
#endif

static inline int16_t jh_min16(int16_t a, int16_t b) { return a < b ? a : b; }
static inline int16_t jh_abs16(int16_t v) { return v < 0 ? -v : v; }

template <typename Pixel>
static void draw_masked_bitmap(JHGfx &gfx, int16_t x, int16_t y,
                               const Pixel *bitmap, const uint8_t *mask,
                               int16_t width, int16_t height) {
  const int16_t mask_width = (width + 7) / 8;
  uint8_t mask_byte = 0u;
  gfx.startWrite();
  for (int16_t row = 0; row < height; ++row, ++y) {
    for (int16_t column = 0; column < width; ++column) {
      mask_byte = (column & 7) ? (uint8_t)(mask_byte << 1u)
                               : mask[row * mask_width + column / 8];
      if ((mask_byte & 0x80u) != 0u) {
        gfx.writePixel(x + column, y, (uint16_t)bitmap[row * width + column]);
      }
    }
  }
  gfx.endWrite();
}

// =============================================================================
// JHGfx base class
// =============================================================================

JHGfx::JHGfx(int16_t w, int16_t h) : WIDTH(w), HEIGHT(h) {
  _width = WIDTH;
  _height = HEIGHT;
  rotation = 0;
  cursor_x = cursor_y = 0;
  textsize_x = textsize_y = 1;
  textcolor = textbgcolor = 0xFFFF;
  wrap = true;
  _cp437 = false;
  gfxFont = NULL;
}

// ---- Transaction API --------------------------------------------------------

void JHGfx::startWrite(void) {}
void JHGfx::endWrite(void) {}

void JHGfx::writePixel(int16_t x, int16_t y, uint16_t color) {
  drawPixel(x, y, color);
}

void JHGfx::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint16_t color) {
  fillRect(x, y, w, h, color);
}

void JHGfx::writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  drawFastVLine(x, y, h, color);
}

void JHGfx::writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  drawFastHLine(x, y, w, color);
}

void JHGfx::writeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      uint16_t color) {
  int16_t steep = jh_abs16(y1 - y0) > jh_abs16(x1 - x0);
  if (steep) {
    _jh_swap_int16(x0, y0);
    _jh_swap_int16(x1, y1);
  }
  if (x0 > x1) {
    _jh_swap_int16(x0, x1);
    _jh_swap_int16(y0, y1);
  }

  int16_t dx = x1 - x0;
  int16_t dy = jh_abs16(y1 - y0);
  int16_t err = dx / 2;
  int16_t ystep = (y0 < y1) ? 1 : -1;

  for (; x0 <= x1; x0++) {
    if (steep) {
      writePixel(y0, x0, color);
    } else {
      writePixel(x0, y0, color);
    }
    err -= dy;
    if (err < 0) {
      y0 += ystep;
      err += dx;
    }
  }
}

// ---- Basic draw API ---------------------------------------------------------

void JHGfx::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  startWrite();
  writeLine(x, y, x, y + h - 1, color);
  endWrite();
}

void JHGfx::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  startWrite();
  writeLine(x, y, x + w - 1, y, color);
  endWrite();
}

void JHGfx::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                     uint16_t color) {
  startWrite();
  for (int16_t i = x; i < x + w; i++) {
    writeFastVLine(i, y, h, color);
  }
  endWrite();
}

void JHGfx::fillScreen(uint16_t color) {
  fillRect(0, 0, _width, _height, color);
}

void JHGfx::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                     uint16_t color) {
  if (x0 == x1) {
    if (y0 > y1)
      _jh_swap_int16(y0, y1);
    drawFastVLine(x0, y0, y1 - y0 + 1, color);
  } else if (y0 == y1) {
    if (x0 > x1)
      _jh_swap_int16(x0, x1);
    drawFastHLine(x0, y0, x1 - x0 + 1, color);
  } else {
    startWrite();
    writeLine(x0, y0, x1, y1, color);
    endWrite();
  }
}

void JHGfx::drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                     uint16_t color) {
  startWrite();
  writeFastHLine(x, y, w, color);
  writeFastHLine(x, y + h - 1, w, color);
  writeFastVLine(x, y, h, color);
  writeFastVLine(x + w - 1, y, h, color);
  endWrite();
}

// ---- Control ----------------------------------------------------------------

void JHGfx::setRotation(uint8_t x) {
  rotation = (x & 3);
  switch (rotation) {
  case 0:
  case 2:
    _width = WIDTH;
    _height = HEIGHT;
    break;
  case 1:
  case 3:
    _width = HEIGHT;
    _height = WIDTH;
    break;
  default:
    break;
  }
}

void JHGfx::invertDisplay(bool i) { (void)i; }

// ---- Circle -----------------------------------------------------------------

void JHGfx::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;

  startWrite();
  writePixel(x0, y0 + r, color);
  writePixel(x0, y0 - r, color);
  writePixel(x0 + r, y0, color);
  writePixel(x0 - r, y0, color);

  while (x < y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;

    writePixel(x0 + x, y0 + y, color);
    writePixel(x0 - x, y0 + y, color);
    writePixel(x0 + x, y0 - y, color);
    writePixel(x0 - x, y0 - y, color);
    writePixel(x0 + y, y0 + x, color);
    writePixel(x0 - y, y0 + x, color);
    writePixel(x0 + y, y0 - x, color);
    writePixel(x0 - y, y0 - x, color);
  }
  endWrite();
}

void JHGfx::drawCircleHelper(int16_t x0, int16_t y0, int16_t r,
                             uint8_t cornername, uint16_t color) {
  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;

  while (x < y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;
    if (cornername & 0x4) {
      writePixel(x0 + x, y0 + y, color);
      writePixel(x0 + y, y0 + x, color);
    }
    if (cornername & 0x2) {
      writePixel(x0 + x, y0 - y, color);
      writePixel(x0 + y, y0 - x, color);
    }
    if (cornername & 0x8) {
      writePixel(x0 - y, y0 + x, color);
      writePixel(x0 - x, y0 + y, color);
    }
    if (cornername & 0x1) {
      writePixel(x0 - y, y0 - x, color);
      writePixel(x0 - x, y0 - y, color);
    }
  }
}

void JHGfx::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  startWrite();
  writeFastVLine(x0, y0 - r, 2 * r + 1, color);
  fillCircleHelper(x0, y0, r, 3, 0, color);
  endWrite();
}

void JHGfx::fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners,
                             int16_t delta, uint16_t color) {
  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;
  int16_t px = x;
  int16_t py = y;

  delta++;

  while (x < y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;
    if (x < (y + 1)) {
      if (corners & 1)
        writeFastVLine(x0 + x, y0 - y, 2 * y + delta, color);
      if (corners & 2)
        writeFastVLine(x0 - x, y0 - y, 2 * y + delta, color);
    }
    if (y != py) {
      if (corners & 1)
        writeFastVLine(x0 + py, y0 - px, 2 * px + delta, color);
      if (corners & 2)
        writeFastVLine(x0 - py, y0 - px, 2 * px + delta, color);
      py = y;
    }
    px = x;
  }
}

// ---- Rectangle variants -----------------------------------------------------

void JHGfx::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                          uint16_t color) {
  int16_t max_radius = ((w < h) ? w : h) / 2;
  if (r > max_radius)
    r = max_radius;
  startWrite();
  writeFastHLine(x + r, y, w - 2 * r, color);
  writeFastHLine(x + r, y + h - 1, w - 2 * r, color);
  writeFastVLine(x, y + r, h - 2 * r, color);
  writeFastVLine(x + w - 1, y + r, h - 2 * r, color);
  drawCircleHelper(x + r, y + r, r, 1, color);
  drawCircleHelper(x + w - r - 1, y + r, r, 2, color);
  drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);
  drawCircleHelper(x + r, y + h - r - 1, r, 8, color);
  endWrite();
}

void JHGfx::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                          uint16_t color) {
  int16_t max_radius = ((w < h) ? w : h) / 2;
  if (r > max_radius)
    r = max_radius;
  startWrite();
  writeFillRect(x + r, y, w - 2 * r, h, color);
  fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
  fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1, color);
  endWrite();
}

// ---- Triangle ---------------------------------------------------------------

void JHGfx::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         int16_t x2, int16_t y2, uint16_t color) {
  drawLine(x0, y0, x1, y1, color);
  drawLine(x1, y1, x2, y2, color);
  drawLine(x2, y2, x0, y0, color);
}

void JHGfx::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         int16_t x2, int16_t y2, uint16_t color) {
  int16_t a, b, y, last;

  if (y0 > y1) {
    _jh_swap_int16(y0, y1);
    _jh_swap_int16(x0, x1);
  }
  if (y1 > y2) {
    _jh_swap_int16(y2, y1);
    _jh_swap_int16(x2, x1);
  }
  if (y0 > y1) {
    _jh_swap_int16(y0, y1);
    _jh_swap_int16(x0, x1);
  }

  startWrite();
  if (y0 == y2) {
    a = b = x0;
    if (x1 < a)
      a = x1;
    else if (x1 > b)
      b = x1;
    if (x2 < a)
      a = x2;
    else if (x2 > b)
      b = x2;
    writeFastHLine(a, y0, b - a + 1, color);
    endWrite();
    return;
  }

  int16_t dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0,
          dx12 = x2 - x1, dy12 = y2 - y1;
  int32_t sa = 0, sb = 0;

  if (y1 == y2)
    last = y1;
  else
    last = y1 - 1;

  for (y = y0; y <= last; y++) {
    a = x0 + sa / dy01;
    b = x0 + sb / dy02;
    sa += dx01;
    sb += dx02;
    if (a > b)
      _jh_swap_int16(a, b);
    writeFastHLine(a, y, b - a + 1, color);
  }

  /* dy12 == 0 means y1 == y2 (flat bottom edge); the loop below never runs
   * in that case because the first loop already covered through y2. Guarding
   * keeps the dy12 division provably safe. */
  if (dy12 != 0) {
    sa = (int32_t)dx12 * (y - y1);
    sb = (int32_t)dx02 * (y - y0);
    for (; y <= y2; y++) {
      a = x1 + sa / dy12;
      b = x0 + sb / dy02;
      sa += dx12;
      sb += dx02;
      if (a > b)
        _jh_swap_int16(a, b);
      writeFastHLine(a, y, b - a + 1, color);
    }
  }
  endWrite();
}

// ---- Bitmap -----------------------------------------------------------------

void JHGfx::drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w,
                       int16_t h, uint16_t color) {
  int16_t byteWidth = (w + 7) / 8;
  uint8_t b = 0;
  startWrite();
  for (int16_t j = 0; j < h; j++, y++) {
    for (int16_t i = 0; i < w; i++) {
      if (i & 7)
        b <<= 1;
      else
        b = bitmap[j * byteWidth + i / 8];
      if (b & 0x80)
        writePixel(x + i, y, color);
    }
  }
  endWrite();
}

void JHGfx::drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w,
                       int16_t h, uint16_t color, uint16_t bg) {
  int16_t byteWidth = (w + 7) / 8;
  uint8_t b = 0;
  startWrite();
  for (int16_t j = 0; j < h; j++, y++) {
    for (int16_t i = 0; i < w; i++) {
      if (i & 7)
        b <<= 1;
      else
        b = bitmap[j * byteWidth + i / 8];
      writePixel(x + i, y, (b & 0x80) ? color : bg);
    }
  }
  endWrite();
}

void JHGfx::drawBitmap(int16_t x, int16_t y, uint8_t *bitmap, int16_t w,
                       int16_t h, uint16_t color) {
  drawBitmap(x, y, static_cast<const uint8_t *>(bitmap), w, h, color);
}

void JHGfx::drawBitmap(int16_t x, int16_t y, uint8_t *bitmap, int16_t w,
                       int16_t h, uint16_t color, uint16_t bg) {
  drawBitmap(x, y, static_cast<const uint8_t *>(bitmap), w, h, color, bg);
}

void JHGfx::drawXBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w,
                        int16_t h, uint16_t color) {
  int16_t byteWidth = (w + 7) / 8;
  uint8_t b = 0;
  startWrite();
  for (int16_t j = 0; j < h; j++, y++) {
    for (int16_t i = 0; i < w; i++) {
      if (i & 7)
        b >>= 1;
      else
        b = bitmap[j * byteWidth + i / 8];
      if (b & 0x01)
        writePixel(x + i, y, color);
    }
  }
  endWrite();
}

void JHGfx::drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                                int16_t w, int16_t h) {
  startWrite();
  for (int16_t j = 0; j < h; j++, y++) {
    for (int16_t i = 0; i < w; i++) {
      writePixel(x + i, y, (uint16_t)bitmap[j * w + i]);
    }
  }
  endWrite();
}

void JHGfx::drawGrayscaleBitmap(int16_t x, int16_t y, uint8_t *bitmap,
                                int16_t w, int16_t h) {
  drawGrayscaleBitmap(x, y, static_cast<const uint8_t *>(bitmap), w, h);
}

void JHGfx::drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                                const uint8_t mask[], int16_t w, int16_t h) {
  draw_masked_bitmap(*this, x, y, bitmap, mask, w, h);
}

void JHGfx::drawGrayscaleBitmap(int16_t x, int16_t y, uint8_t *bitmap,
                                uint8_t *mask, int16_t w, int16_t h) {
  drawGrayscaleBitmap(x, y, static_cast<const uint8_t *>(bitmap),
                      static_cast<const uint8_t *>(mask), w, h);
}

void JHGfx::drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[],
                          int16_t w, int16_t h) {
  startWrite();
  for (int16_t j = 0; j < h; j++, y++) {
    for (int16_t i = 0; i < w; i++) {
      writePixel(x + i, y, bitmap[j * w + i]);
    }
  }
  endWrite();
}

void JHGfx::drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w,
                          int16_t h) {
  drawRGBBitmap(x, y, static_cast<const uint16_t *>(bitmap), w, h);
}

void JHGfx::drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[],
                          const uint8_t mask[], int16_t w, int16_t h) {
  draw_masked_bitmap(*this, x, y, bitmap, mask, w, h);
}

void JHGfx::drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, uint8_t *mask,
                          int16_t w, int16_t h) {
  drawRGBBitmap(x, y, static_cast<const uint16_t *>(bitmap),
                static_cast<const uint8_t *>(mask), w, h);
}

// ---- Text / character -------------------------------------------------------

void JHGfx::drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color,
                     uint16_t bg, uint8_t size) {
  drawChar(x, y, c, color, bg, size, size);
}

void JHGfx::drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color,
                     uint16_t bg, uint8_t size_x, uint8_t size_y) {
  if (!gfxFont) { // Classic built-in font
    if ((x >= _width) || (y >= _height) || ((x + 6 * size_x - 1) < 0) ||
        ((y + 8 * size_y - 1) < 0))
      return;

    if (!_cp437 && (c >= 176))
      c++;

    startWrite();
    for (int8_t i = 0; i < 5; i++) {
      uint8_t line = jh_gfx_builtin_font[c * 5 + i];
      for (int8_t j = 0; j < 8; j++, line >>= 1) {
        if (line & 1) {
          if (size_x == 1 && size_y == 1)
            writePixel(x + i, y + j, color);
          else
            writeFillRect(x + i * size_x, y + j * size_y, size_x, size_y,
                          color);
        } else if (bg != color) {
          if (size_x == 1 && size_y == 1)
            writePixel(x + i, y + j, bg);
          else
            writeFillRect(x + i * size_x, y + j * size_y, size_x, size_y, bg);
        }
      }
    }
    if (bg != color) {
      if (size_x == 1 && size_y == 1)
        writeFastVLine(x + 5, y, 8, bg);
      else
        writeFillRect(x + 5 * size_x, y, size_x, 8 * size_y, bg);
    }
    endWrite();

  } else { // Custom font
    c -= (uint8_t)gfxFont->first;
    GFXglyph *glyph = gfxFont->glyph + c;
    uint8_t *bitmap = gfxFont->bitmap;

    uint16_t bo = glyph->bitmapOffset;
    uint8_t w = glyph->width, h = glyph->height;
    int8_t xo = glyph->xOffset, yo = glyph->yOffset;
    uint8_t xx, yy, bits = 0, bit = 0;
    int16_t xo16 = 0, yo16 = 0;

    if (size_x > 1 || size_y > 1) {
      // Glyph offsets are intentionally signed (int8_t); sign extension
      // to int16_t is the desired behaviour here.
      // NOLINTNEXTLINE(bugprone-signed-char-misuse,cert-str34-c)
      xo16 = xo;
      // NOLINTNEXTLINE(bugprone-signed-char-misuse,cert-str34-c)
      yo16 = yo;
    }

    startWrite();
    for (yy = 0; yy < h; yy++) {
      for (xx = 0; xx < w; xx++) {
        if (!(bit++ & 7)) {
          bits = bitmap[bo++];
        }
        if (bits & 0x80) {
          if (size_x == 1 && size_y == 1) {
            writePixel(x + xo + xx, y + yo + yy, color);
          } else {
            writeFillRect(x + (xo16 + xx) * size_x, y + (yo16 + yy) * size_y,
                          size_x, size_y, color);
          }
        }
        bits <<= 1;
      }
    }
    endWrite();
  }
}

size_t JHGfx::write(uint8_t c) {
  if (!gfxFont) { // Classic built-in font
    if (c == '\n') {
      cursor_x = 0;
      cursor_y += textsize_y * 8;
    } else if (c != '\r') {
      if (wrap && ((cursor_x + textsize_x * 6) > _width)) {
        cursor_x = 0;
        cursor_y += textsize_y * 8;
      }
      drawChar(cursor_x, cursor_y, c, textcolor, textbgcolor, textsize_x,
               textsize_y);
      cursor_x += textsize_x * 6;
    }
  } else { // Custom font
    if (c == '\n') {
      cursor_x = 0;
      cursor_y += (int16_t)textsize_y * gfxFont->yAdvance;
    } else if (c != '\r') {
      uint8_t first = gfxFont->first;
      if ((c >= first) && (c <= gfxFont->last)) {
        GFXglyph *glyph = gfxFont->glyph + (c - first);
        uint8_t w = glyph->width, h = glyph->height;
        if ((w > 0) && (h > 0)) {
          // Glyph xOffset is intentionally signed (int8_t).
          // NOLINTNEXTLINE(bugprone-signed-char-misuse,cert-str34-c)
          int16_t xo = (int8_t)glyph->xOffset;
          if (wrap && ((cursor_x + textsize_x * (xo + w)) > _width)) {
            cursor_x = 0;
            cursor_y += (int16_t)textsize_y * gfxFont->yAdvance;
          }
          drawChar(cursor_x, cursor_y, c, textcolor, textbgcolor, textsize_x,
                   textsize_y);
        }
        cursor_x += (uint8_t)glyph->xAdvance * (int16_t)textsize_x;
      }
    }
  }
  return 1;
}

size_t JHGfx::write(const char *s) {
  if (!s)
    return 0;
  size_t n = 0;
  while (*s) {
    n += write((uint8_t)*s++);
  }
  return n;
}

void JHGfx::print(const char *s) { write(s); }

void JHGfx::setTextSize(uint8_t s) { setTextSize(s, s); }

void JHGfx::setTextSize(uint8_t s_x, uint8_t s_y) {
  textsize_x = (s_x > 0) ? s_x : 1;
  textsize_y = (s_y > 0) ? s_y : 1;
}

void JHGfx::setFont(const GFXfont *f) {
  if (f) {
    if (!gfxFont) {
      cursor_y += 6;
    }
  } else if (gfxFont) {
    cursor_y -= 6;
  }
  gfxFont = (GFXfont *)f;
}

void JHGfx::charBounds(unsigned char c, int16_t *x, int16_t *y, int16_t *minx,
                       int16_t *miny, int16_t *maxx, int16_t *maxy) {
  if (gfxFont) {
    if (c == '\n') {
      *x = 0;
      *y += textsize_y * gfxFont->yAdvance;
    } else if (c != '\r') {
      uint8_t first = gfxFont->first, last = gfxFont->last;
      if ((c >= first) && (c <= last)) {
        GFXglyph *glyph = gfxFont->glyph + (c - first);
        uint8_t gw = glyph->width, gh = glyph->height, xa = glyph->xAdvance;
        int8_t xo = glyph->xOffset, yo = glyph->yOffset;
        if (wrap && ((*x + (((int16_t)xo + gw) * textsize_x)) > _width)) {
          *x = 0;
          *y += textsize_y * gfxFont->yAdvance;
        }
        int16_t tsx = (int16_t)textsize_x, tsy = (int16_t)textsize_y,
                x1 = *x + xo * tsx, y1 = *y + yo * tsy, x2 = x1 + gw * tsx - 1,
                y2 = y1 + gh * tsy - 1;
        if (x1 < *minx)
          *minx = x1;
        if (y1 < *miny)
          *miny = y1;
        if (x2 > *maxx)
          *maxx = x2;
        if (y2 > *maxy)
          *maxy = y2;
        *x += xa * tsx;
      }
    }
  } else { // Default font
    if (c == '\n') {
      *x = 0;
      *y += textsize_y * 8;
    } else if (c != '\r') {
      if (wrap && ((*x + textsize_x * 6) > _width)) {
        *x = 0;
        *y += textsize_y * 8;
      }
      int16_t x2 = *x + textsize_x * 6 - 1;
      int16_t y2 = *y + textsize_y * 8 - 1;
      if (x2 > *maxx)
        *maxx = x2;
      if (y2 > *maxy)
        *maxy = y2;
      if (*x < *minx)
        *minx = *x;
      if (*y < *miny)
        *miny = *y;
      *x += textsize_x * 6;
    }
  }
}

void JHGfx::getTextBounds(const char *str, int16_t x, int16_t y, int16_t *x1,
                          int16_t *y1, uint16_t *w, uint16_t *h) {
  uint8_t c;
  int16_t minx = 0x7FFF, miny = 0x7FFF, maxx = -1, maxy = -1;

  *x1 = x;
  *y1 = y;
  *w = *h = 0;

  while ((c = (uint8_t)*str++)) {
    charBounds(c, &x, &y, &minx, &miny, &maxx, &maxy);
  }

  if (maxx >= minx) {
    *x1 = minx;
    *w = maxx - minx + 1;
  }
  if (maxy >= miny) {
    *y1 = miny;
    *h = maxy - miny + 1;
  }
}

// =============================================================================
// JHGfxCanvas1 - 1-bit offscreen buffer
// =============================================================================

static void canvas_transform_point(uint8_t rotation, int16_t raw_width,
                                   int16_t raw_height, int16_t &x, int16_t &y) {
  const int16_t original_x = x;
  switch (rotation) {
  case 1:
    x = raw_width - 1 - y;
    y = original_x;
    break;
  case 2:
    x = raw_width - 1 - x;
    y = raw_height - 1 - y;
    break;
  case 3:
    x = y;
    y = raw_height - 1 - original_x;
    break;
  default:
    break;
  }
}

template <typename Canvas>
static void canvas_draw_fast_v_line(Canvas &canvas, int16_t x, int16_t y,
                                    int16_t height, uint16_t color,
                                    int16_t raw_width, int16_t raw_height) {
  if (height < 0) {
    height = -height;
    y -= height - 1;
    if (y < 0) {
      height += y;
      y = 0;
    }
  }
  if (x < 0 || x >= canvas.width() || y >= canvas.height() ||
      y + height - 1 < 0) {
    return;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  if (y + height > canvas.height()) {
    height = canvas.height() - y;
  }

  switch (canvas.getRotation()) {
  case 0:
    canvas.drawFastRawVLine(x, y, height, color);
    break;
  case 1: {
    const int16_t original_x = x;
    x = raw_width - y - height;
    y = original_x;
    canvas.drawFastRawHLine(x, y, height, color);
    break;
  }
  case 2:
    x = raw_width - 1 - x;
    y = raw_height - y - height;
    canvas.drawFastRawVLine(x, y, height, color);
    break;
  case 3: {
    const int16_t original_x = x;
    x = y;
    y = raw_height - 1 - original_x;
    canvas.drawFastRawHLine(x, y, height, color);
    break;
  }
  }
}

template <typename Canvas>
static void canvas_draw_fast_h_line(Canvas &canvas, int16_t x, int16_t y,
                                    int16_t width, uint16_t color,
                                    int16_t raw_width, int16_t raw_height) {
  if (width < 0) {
    width = -width;
    x -= width - 1;
    if (x < 0) {
      width += x;
      x = 0;
    }
  }
  if (y < 0 || y >= canvas.height() || x >= canvas.width() ||
      x + width - 1 < 0) {
    return;
  }
  if (x < 0) {
    width += x;
    x = 0;
  }
  if (x + width >= canvas.width()) {
    width = canvas.width() - x;
  }

  switch (canvas.getRotation()) {
  case 0:
    canvas.drawFastRawHLine(x, y, width, color);
    break;
  case 1: {
    const int16_t original_x = x;
    x = raw_width - 1 - y;
    y = original_x;
    canvas.drawFastRawVLine(x, y, width, color);
    break;
  }
  case 2:
    x = raw_width - x - width;
    y = raw_height - 1 - y;
    canvas.drawFastRawHLine(x, y, width, color);
    break;
  case 3: {
    const int16_t original_x = x;
    x = y;
    y = raw_height - original_x - width;
    canvas.drawFastRawVLine(x, y, width, color);
    break;
  }
  }
}

JHGfxCanvas1::JHGfxCanvas1(uint16_t w, uint16_t h, bool allocate_buffer)
    : JHGfx(w, h), buffer_owned(allocate_buffer) {
  if (allocate_buffer) {
    uint32_t bytes = ((w + 7) / 8) * h;
    buffer = (uint8_t *)malloc(bytes);
    if (buffer)
      memset(buffer, 0, bytes);
  } else {
    buffer = nullptr;
  }
}

JHGfxCanvas1::~JHGfxCanvas1(void) {
  if (buffer && buffer_owned)
    free(buffer);
}

void JHGfxCanvas1::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (!buffer)
    return;
  if ((x < 0) || (y < 0) || (x >= _width) || (y >= _height))
    return;

  canvas_transform_point(rotation, WIDTH, HEIGHT, x, y);

  uint8_t *ptr = &buffer[(x / 8) + y * ((WIDTH + 7) / 8)];
  if (color)
    *ptr |= (0x80 >> (x & 7));
  else
    *ptr &= ~(0x80 >> (x & 7));
}

bool JHGfxCanvas1::getPixel(int16_t x, int16_t y) const {
  canvas_transform_point(rotation, WIDTH, HEIGHT, x, y);
  return getRawPixel(x, y);
}

bool JHGfxCanvas1::getRawPixel(int16_t x, int16_t y) const {
  if ((x < 0) || (y < 0) || (x >= WIDTH) || (y >= HEIGHT))
    return false;
  if (!buffer)
    return false;
  uint8_t *ptr = &buffer[(x / 8) + y * ((WIDTH + 7) / 8)];
  return ((*ptr) & (0x80 >> (x & 7))) != 0;
}

void JHGfxCanvas1::fillScreen(uint16_t color) {
  if (buffer) {
    uint32_t bytes = ((WIDTH + 7) / 8) * HEIGHT;
    memset(buffer, color ? 0xFF : 0x00, bytes);
  }
}

void JHGfxCanvas1::drawFastVLine(int16_t x, int16_t y, int16_t h,
                                 uint16_t color) {
  canvas_draw_fast_v_line(*this, x, y, h, color, WIDTH, HEIGHT);
}

void JHGfxCanvas1::drawFastHLine(int16_t x, int16_t y, int16_t w,
                                 uint16_t color) {
  canvas_draw_fast_h_line(*this, x, y, w, color, WIDTH, HEIGHT);
}

void JHGfxCanvas1::drawFastRawVLine(int16_t x, int16_t y, int16_t h,
                                    uint16_t color) {
  int16_t row_bytes = ((WIDTH + 7) / 8);
  uint8_t *ptr = &buffer[(x / 8) + y * row_bytes];
  if (color > 0) {
    uint8_t bit_mask = (0x80 >> (x & 7));
    for (int16_t i = 0; i < h; i++) {
      *ptr |= bit_mask;
      ptr += row_bytes;
    }
  } else {
    uint8_t bit_mask = ~(0x80 >> (x & 7));
    for (int16_t i = 0; i < h; i++) {
      *ptr &= bit_mask;
      ptr += row_bytes;
    }
  }
}

void JHGfxCanvas1::drawFastRawHLine(int16_t x, int16_t y, int16_t w,
                                    uint16_t color) {
  int16_t rowBytes = ((WIDTH + 7) / 8);
  uint8_t *ptr = &buffer[(x / 8) + y * rowBytes];
  size_t remainingWidthBits = w;

  if ((x & 7) > 0) {
    uint8_t startByteBitMask = 0x00;
    for (int8_t i = (x & 7); ((i < 8) && (remainingWidthBits > 0)); i++) {
      startByteBitMask |= (0x80 >> i);
      remainingWidthBits--;
    }
    if (color > 0)
      *ptr |= startByteBitMask;
    else
      *ptr &= ~startByteBitMask;
    ptr++;
  }

  if (remainingWidthBits > 0) {
    size_t remainingWholeBytes = remainingWidthBits / 8;
    size_t lastByteBits = remainingWidthBits % 8;
    memset(ptr, color > 0 ? 0xFF : 0x00, remainingWholeBytes);

    if (lastByteBits > 0) {
      uint8_t lastByteBitMask = 0x00;
      for (size_t i = 0; i < lastByteBits; i++) {
        lastByteBitMask |= (0x80 >> i);
      }
      ptr += remainingWholeBytes;
      if (color > 0)
        *ptr |= lastByteBitMask;
      else
        *ptr &= ~lastByteBitMask;
    }
  }
}

// =============================================================================
// JHGfxCanvas8 - 8-bit offscreen buffer
// =============================================================================

JHGfxCanvas8::JHGfxCanvas8(uint16_t w, uint16_t h, bool allocate_buffer)
    : JHGfx(w, h), buffer_owned(allocate_buffer) {
  if (allocate_buffer) {
    uint32_t bytes = w * h;
    buffer = (uint8_t *)malloc(bytes);
    if (buffer)
      memset(buffer, 0, bytes);
  } else {
    buffer = nullptr;
  }
}

JHGfxCanvas8::~JHGfxCanvas8(void) {
  if (buffer && buffer_owned)
    free(buffer);
}

void JHGfxCanvas8::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (!buffer)
    return;
  if ((x < 0) || (y < 0) || (x >= _width) || (y >= _height))
    return;

  canvas_transform_point(rotation, WIDTH, HEIGHT, x, y);
  buffer[x + y * WIDTH] = color;
}

uint8_t JHGfxCanvas8::getPixel(int16_t x, int16_t y) const {
  canvas_transform_point(rotation, WIDTH, HEIGHT, x, y);
  return getRawPixel(x, y);
}

uint8_t JHGfxCanvas8::getRawPixel(int16_t x, int16_t y) const {
  if ((x < 0) || (y < 0) || (x >= WIDTH) || (y >= HEIGHT))
    return 0;
  if (!buffer)
    return 0;
  return buffer[x + y * WIDTH];
}

void JHGfxCanvas8::fillScreen(uint16_t color) {
  if (buffer)
    memset(buffer, color, (size_t)WIDTH * HEIGHT);
}

void JHGfxCanvas8::drawFastVLine(int16_t x, int16_t y, int16_t h,
                                 uint16_t color) {
  canvas_draw_fast_v_line(*this, x, y, h, color, WIDTH, HEIGHT);
}

void JHGfxCanvas8::drawFastHLine(int16_t x, int16_t y, int16_t w,
                                 uint16_t color) {
  canvas_draw_fast_h_line(*this, x, y, w, color, WIDTH, HEIGHT);
}

void JHGfxCanvas8::drawFastRawVLine(int16_t x, int16_t y, int16_t h,
                                    uint16_t color) {
  uint8_t *buffer_ptr = buffer + (size_t)y * WIDTH + x;
  for (int16_t i = 0; i < h; i++) {
    *buffer_ptr = color;
    buffer_ptr += WIDTH;
  }
}

void JHGfxCanvas8::drawFastRawHLine(int16_t x, int16_t y, int16_t w,
                                    uint16_t color) {
  memset(buffer + (size_t)y * WIDTH + x, color, w);
}

// =============================================================================
// JHGfxCanvas16 - 16-bit offscreen buffer
// =============================================================================

JHGfxCanvas16::JHGfxCanvas16(uint16_t w, uint16_t h, bool allocate_buffer)
    : JHGfx(w, h), buffer_owned(allocate_buffer) {
  if (allocate_buffer) {
    uint32_t bytes = (uint32_t)w * h * 2;
    buffer = (uint16_t *)malloc(bytes);
    if (buffer)
      memset(buffer, 0, bytes);
  } else {
    buffer = nullptr;
  }
}

JHGfxCanvas16::~JHGfxCanvas16(void) {
  if (buffer && buffer_owned)
    free(buffer);
}

void JHGfxCanvas16::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (!buffer)
    return;
  if ((x < 0) || (y < 0) || (x >= _width) || (y >= _height))
    return;

  canvas_transform_point(rotation, WIDTH, HEIGHT, x, y);
  buffer[x + y * WIDTH] = color;
}

uint16_t JHGfxCanvas16::getPixel(int16_t x, int16_t y) const {
  canvas_transform_point(rotation, WIDTH, HEIGHT, x, y);
  return getRawPixel(x, y);
}

uint16_t JHGfxCanvas16::getRawPixel(int16_t x, int16_t y) const {
  if ((x < 0) || (y < 0) || (x >= WIDTH) || (y >= HEIGHT))
    return 0;
  if (!buffer)
    return 0;
  return buffer[x + y * WIDTH];
}

void JHGfxCanvas16::fillScreen(uint16_t color) {
  if (buffer) {
    if (color == 0) {
      memset(buffer, 0, (size_t)WIDTH * HEIGHT * 2);
    } else {
      uint32_t n = (uint32_t)WIDTH * HEIGHT;
      for (uint32_t i = 0; i < n; i++)
        buffer[i] = color;
    }
  }
}

void JHGfxCanvas16::byteSwap(void) {
  if (!buffer)
    return;
  uint32_t n = (uint32_t)WIDTH * HEIGHT;
  for (uint32_t i = 0; i < n; i++) {
    buffer[i] = (buffer[i] >> 8) | (buffer[i] << 8);
  }
}

void JHGfxCanvas16::drawFastVLine(int16_t x, int16_t y, int16_t h,
                                  uint16_t color) {
  canvas_draw_fast_v_line(*this, x, y, h, color, WIDTH, HEIGHT);
}

void JHGfxCanvas16::drawFastHLine(int16_t x, int16_t y, int16_t w,
                                  uint16_t color) {
  canvas_draw_fast_h_line(*this, x, y, w, color, WIDTH, HEIGHT);
}

void JHGfxCanvas16::drawFastRawVLine(int16_t x, int16_t y, int16_t h,
                                     uint16_t color) {
  uint16_t *buffer_ptr = buffer + (size_t)y * WIDTH + x;
  for (int16_t i = 0; i < h; i++) {
    *buffer_ptr = color;
    buffer_ptr += WIDTH;
  }
}

void JHGfxCanvas16::drawFastRawHLine(int16_t x, int16_t y, int16_t w,
                                     uint16_t color) {
  uint16_t *buffer_ptr = buffer + (size_t)y * WIDTH + x;
  for (int16_t i = 0; i < w; i++) {
    buffer_ptr[i] = color;
  }
}
