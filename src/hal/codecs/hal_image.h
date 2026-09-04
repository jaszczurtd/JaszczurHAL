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
/**
 * @brief Validate Base64 input and calculate its decoded PNG byte count.
 * @param base64 Base64 text, or NULL only when @p base64_len is zero.
 * @param base64_len Number of input characters.
 * @param png_size Receives the decoded byte count and is cleared on failure.
 * @return true for valid input, otherwise false.
 */
bool hal_image_png_base64_decoded_size(const char *base64, size_t base64_len,
                                       size_t *png_size);

/**
 * @brief Decode a Base64 PNG into an allocated RGBA8888 buffer.
 * @param rgba Receives memory allocated by LodePNG; release it with `free()`.
 * @param width Receives image width in pixels.
 * @param height Receives image height in pixels.
 * @param base64 Base64 PNG text.
 * @param base64_len Number of input characters.
 * @param png_work Caller-owned buffer for decoded PNG bytes.
 * @param png_work_size Capacity of @p png_work.
 * @param png_error Optional output for the LodePNG error code.
 * @return true on success; false for invalid input, Base64 failure, an
 * undersized work buffer, or a PNG decode error. Outputs are cleared on
 * failure.
 */
bool hal_image_png_base64_decode_rgba8888(uint8_t **rgba, unsigned *width,
                                          unsigned *height, const char *base64,
                                          size_t base64_len, uint8_t *png_work,
                                          size_t png_work_size,
                                          unsigned *png_error);

/**
 * @brief Decode a Base64 PNG directly into caller-owned RGB565 storage.
 * @param base64 Base64 PNG text.
 * @param base64_len Number of input characters.
 * @param png_work Caller-owned buffer for decoded PNG bytes.
 * @param png_work_size Capacity of @p png_work.
 * @param rgb565 Output pixel buffer.
 * @param rgb565_pixels Capacity of @p rgb565 in pixels.
 * @param width Receives image width in pixels.
 * @param height Receives image height in pixels.
 * @param png_error Optional output for the LodePNG error code.
 * @return true on success; false for invalid input, decode failure, size
 * overflow, or insufficient output capacity. Alpha is ignored.
 */
bool hal_image_png_base64_decode_rgb565(const char *base64, size_t base64_len,
                                        uint8_t *png_work, size_t png_work_size,
                                        uint16_t *rgb565, size_t rgb565_pixels,
                                        unsigned *width, unsigned *height,
                                        unsigned *png_error);
#endif

#ifdef HAL_ENABLE_JPEG
/**
 * @brief Decode baseline JPEG bytes into caller-owned RGB565 storage.
 * @param jpeg JPEG byte buffer.
 * @param jpeg_size Input size in bytes.
 * @param rgb565 Output pixel buffer.
 * @param rgb565_pixels Capacity of @p rgb565 in pixels.
 * @param width Receives image width in pixels.
 * @param height Receives image height in pixels.
 * @return true on success; false for invalid input, unsupported/progressive
 * JPEG data, allocation failure, decode failure, or insufficient capacity.
 */
bool hal_image_jpeg_decode_rgb565(const uint8_t *jpeg, size_t jpeg_size,
                                  uint16_t *rgb565, size_t rgb565_pixels,
                                  unsigned *width, unsigned *height);
#endif

#ifdef HAL_ENABLE_JPEG_AS_BASE64
/**
 * @brief Validate Base64 input and calculate its decoded JPEG byte count.
 * @param base64 Base64 text, or NULL only when @p base64_len is zero.
 * @param base64_len Number of input characters.
 * @param jpeg_size Receives the decoded byte count and is cleared on failure.
 * @return true for valid input, otherwise false.
 */
bool hal_image_jpeg_base64_decoded_size(const char *base64, size_t base64_len,
                                        size_t *jpeg_size);

/**
 * @brief Decode a Base64 JPEG into caller-owned RGB565 storage.
 * @param base64 Base64 JPEG text.
 * @param base64_len Number of input characters.
 * @param jpeg_work Caller-owned buffer for decoded JPEG bytes.
 * @param jpeg_work_size Capacity of @p jpeg_work.
 * @param rgb565 Output pixel buffer.
 * @param rgb565_pixels Capacity of @p rgb565 in pixels.
 * @param width Receives image width in pixels.
 * @param height Receives image height in pixels.
 * @return true on success; false for invalid input, Base64/decode failure,
 * size overflow, or insufficient work/output capacity.
 */
bool hal_image_jpeg_base64_decode_rgb565(const char *base64, size_t base64_len,
                                         uint8_t *jpeg_work,
                                         size_t jpeg_work_size,
                                         uint16_t *rgb565, size_t rgb565_pixels,
                                         unsigned *width, unsigned *height);
#endif

#ifdef __cplusplus
}
#endif
