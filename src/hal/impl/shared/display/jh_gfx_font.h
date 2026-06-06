/*
 * GFX font data structures.
 *
 * This file defines the glyph and font structs used by the JaszczurHAL
 * portable graphics engine.  The layout is intentionally compatible with
 * fonts originally created for the Adafruit GFX library so that existing
 * font header files can be reused without modification.
 *
 * Original font structure design by Adafruit Industries (BSD license).
 * Portable adaptation for JaszczurHAL.
 */

#ifndef JH_GFX_FONT_H
#define JH_GFX_FONT_H

#include <stdint.h>

// PROGMEM is an AVR/Arduino concept; on other platforms it is a no-op.
#ifndef PROGMEM
#define PROGMEM
#endif

/// Font data stored PER GLYPH
typedef struct {
    uint16_t bitmapOffset; ///< Pointer into GFXfont->bitmap
    uint8_t width;         ///< Bitmap dimensions in pixels
    uint8_t height;        ///< Bitmap dimensions in pixels
    uint8_t xAdvance;      ///< Distance to advance cursor (x axis)
    int8_t xOffset;        ///< X dist from cursor pos to UL corner
    int8_t yOffset;        ///< Y dist from cursor pos to UL corner
} GFXglyph;

/// Data stored for FONT AS A WHOLE
typedef struct {
    uint8_t *bitmap;  ///< Glyph bitmaps, concatenated
    GFXglyph *glyph;  ///< Glyph array
    uint16_t first;   ///< ASCII extents (first char)
    uint16_t last;    ///< ASCII extents (last char)
    uint8_t yAdvance; ///< Newline distance (y axis)
} GFXfont;

#endif // JH_GFX_FONT_H
