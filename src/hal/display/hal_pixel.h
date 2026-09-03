#pragma once

/** @file Pixel-format conversion helpers independent of a display backend. */

#include "hal/core/hal_status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t hal_pixel_rgb888_to_rgb565(uint8_t red, uint8_t green, uint8_t blue);
hal_status_t hal_pixel_rgb888_buffer_to_rgb565_ex(const uint8_t *rgb,
                                                  uint16_t *rgb565,
                                                  size_t pixel_count);
hal_status_t hal_pixel_rgba8888_buffer_to_rgb565_ex(const uint8_t *rgba,
                                                    uint16_t *rgb565,
                                                    size_t pixel_count);

#ifdef __cplusplus
}
#endif
