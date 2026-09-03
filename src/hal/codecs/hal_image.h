#pragma once

/** @file Memory-oriented image decode helpers. */

#include "hal/core/hal_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAL_ENABLE_PNG_AS_BASE64
bool hal_image_png_base64_decoded_size(const char *base64, size_t base64_len,
                                       size_t *png_size);
bool hal_image_png_base64_decode_rgba8888(uint8_t **rgba, unsigned *width,
                                          unsigned *height, const char *base64,
                                          size_t base64_len, uint8_t *png_work,
                                          size_t png_work_size,
                                          unsigned *png_error);
bool hal_image_png_base64_decode_rgb565(const char *base64, size_t base64_len,
                                        uint8_t *png_work, size_t png_work_size,
                                        uint16_t *rgb565, size_t rgb565_pixels,
                                        unsigned *width, unsigned *height,
                                        unsigned *png_error);
#endif

#ifdef HAL_ENABLE_JPEG
bool hal_image_jpeg_decode_rgb565(const uint8_t *jpeg, size_t jpeg_size,
                                  uint16_t *rgb565, size_t rgb565_pixels,
                                  unsigned *width, unsigned *height);
#endif

#ifdef HAL_ENABLE_JPEG_AS_BASE64
bool hal_image_jpeg_base64_decoded_size(const char *base64, size_t base64_len,
                                        size_t *jpeg_size);
bool hal_image_jpeg_base64_decode_rgb565(const char *base64, size_t base64_len,
                                         uint8_t *jpeg_work,
                                         size_t jpeg_work_size,
                                         uint16_t *rgb565, size_t rgb565_pixels,
                                         unsigned *width, unsigned *height);
#endif

#ifdef __cplusplus
}
#endif
