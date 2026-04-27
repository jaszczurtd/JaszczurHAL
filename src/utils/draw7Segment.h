#ifndef DRAW_7_SEGMENT_H
#define DRAW_7_SEGMENT_H

#include <hal/hal_config.h>
#ifndef HAL_DISABLE_DISPLAY

/**
 * @file draw7Segment.h
 * @brief 7-segment style string rendering via the HAL display layer.
 *
 * Draws proportional 7-segment characters using hal_display_* primitives.
 * Supported characters: digits 0-9, hex A-F, space, +, -, ., :, %, ^.
 *
 * No dependency on a specific display driver - works with any backend that
 * implements hal_display.h.
 */

#include <stdint.h>
#include <string.h>
#include <hal/hal_display.h>
#include <hal/hal_serial.h>

/**
 * @brief Draw a 7-segment string at the given position.
 * @param str         Null-terminated string to render.
 * @param x           Left edge in pixels.
 * @param y           Top edge in pixels.
 * @param digitWidth  Width of a single digit cell in pixels.
 * @param digitHeight Height of a single digit cell in pixels.
 * @param thickness   Segment thickness in pixels.
 * @param color       RGB565 colour.
 */
void draw7SegString(const char* str, int x, int y, int digitWidth, int digitHeight, float thickness, uint16_t color);

/**
 * @brief Calculate the pixel width of a 7-segment string.
 * @param str        Null-terminated string to measure.
 * @param digitWidth Width of a single digit cell in pixels.
 * @param thickness  Segment thickness in pixels.
 * @return Total width in pixels.
 */
int get7SegStringWidth(const char* str, int digitWidth, float thickness);

#endif /* HAL_DISABLE_DISPLAY */
#endif /* DRAW_7_SEGMENT_H */
