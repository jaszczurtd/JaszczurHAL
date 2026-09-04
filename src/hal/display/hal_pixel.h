#pragma once

/** @file Pixel-format conversion helpers independent of a display backend. */

#include "hal/core/hal_status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert one RGB888 pixel to RGB565.
 * @param red 8-bit red channel.
 * @param green 8-bit green channel.
 * @param blue 8-bit blue channel.
 * @return Packed RGB565 pixel in host byte order.
 */
uint16_t hal_pixel_rgb888_to_rgb565(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Convert an RGB888 buffer to RGB565.
 * @param rgb Input with three bytes per pixel in red, green, blue order.
 * @param rgb565 Output with @p pixel_count entries.
 * @param pixel_count Number of pixels to convert.
 * @return HAL_OK, or HAL_EINVAL for a NULL buffer.
 */
hal_status_t hal_pixel_rgb888_buffer_to_rgb565_ex(const uint8_t *rgb,
                                                  uint16_t *rgb565,
                                                  size_t pixel_count);

/**
 * @brief Convert an RGBA8888 buffer to RGB565 while ignoring alpha.
 * @param rgba Input with four bytes per pixel in red, green, blue, alpha order.
 * @param rgb565 Output with @p pixel_count entries.
 * @param pixel_count Number of pixels to convert.
 * @return HAL_OK, or HAL_EINVAL for a NULL buffer.
 */
hal_status_t hal_pixel_rgba8888_buffer_to_rgb565_ex(const uint8_t *rgba,
                                                    uint16_t *rgb565,
                                                    size_t pixel_count);

#ifdef __cplusplus
}
#endif
