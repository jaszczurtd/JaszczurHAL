#include "hal/codecs/hal_image.h"

#include "hal/display/hal_pixel.h"

#include <stdlib.h>
#include <string.h>

#if defined(HAL_ENABLE_PNG_AS_BASE64) || defined(HAL_ENABLE_JPEG_AS_BASE64)
#include "hal/security/hal_crypto.h"
#endif

#ifdef HAL_ENABLE_PNG_AS_BASE64
#include "hal/codecs/lodepng/lodepng.h"
#endif

#ifdef HAL_ENABLE_JPEG
#include "hal/codecs/jpeg/tjpgd.h"
#endif

#if defined(HAL_ENABLE_PNG_AS_BASE64) || defined(HAL_ENABLE_JPEG)
static bool image_mul_size(size_t first, size_t second, size_t *out) {
  if (out == nullptr) {
    return false;
  }
  if (first != 0u && second > ((size_t)-1) / first) {
    *out = 0u;
    return false;
  }
  *out = first * second;
  return true;
}
#endif

#ifdef HAL_ENABLE_PNG_AS_BASE64
bool hal_image_png_base64_decoded_size(const char *base64, size_t base64_len,
                                       size_t *png_size) {
  if (png_size != nullptr) {
    *png_size = 0u;
  }
  return png_size != nullptr && (base64 != nullptr || base64_len == 0u) &&
         hal_base64_decode(base64, base64_len, nullptr, 0u, png_size);
}

bool hal_image_png_base64_decode_rgba8888(uint8_t **rgba, unsigned *width,
                                          unsigned *height, const char *base64,
                                          size_t base64_len, uint8_t *png_work,
                                          size_t png_work_size,
                                          unsigned *png_error) {
  if (png_error != nullptr) {
    *png_error = 0u;
  }
  if (rgba != nullptr) {
    *rgba = nullptr;
  }
  if (width != nullptr) {
    *width = 0u;
  }
  if (height != nullptr) {
    *height = 0u;
  }
  if (rgba == nullptr || width == nullptr || height == nullptr ||
      base64 == nullptr || png_work == nullptr) {
    return false;
  }

  size_t png_size = 0u;
  if (!hal_base64_decode(base64, base64_len, png_work, png_work_size,
                         &png_size)) {
    return false;
  }
  const unsigned error =
      lodepng_decode32(rgba, width, height, png_work, png_size);
  if (png_error != nullptr) {
    *png_error = error;
  }
  if (error == 0u) {
    return true;
  }
  if (*rgba != nullptr) {
    free(*rgba);
    *rgba = nullptr;
  }
  *width = 0u;
  *height = 0u;
  return false;
}

bool hal_image_png_base64_decode_rgb565(const char *base64, size_t base64_len,
                                        uint8_t *png_work, size_t png_work_size,
                                        uint16_t *rgb565, size_t rgb565_pixels,
                                        unsigned *width, unsigned *height,
                                        unsigned *png_error) {
  if (png_error != nullptr) {
    *png_error = 0u;
  }
  if (width != nullptr) {
    *width = 0u;
  }
  if (height != nullptr) {
    *height = 0u;
  }
  if (rgb565 == nullptr || width == nullptr || height == nullptr) {
    return false;
  }

  uint8_t *rgba = nullptr;
  unsigned decoded_width = 0u;
  unsigned decoded_height = 0u;
  if (!hal_image_png_base64_decode_rgba8888(
          &rgba, &decoded_width, &decoded_height, base64, base64_len, png_work,
          png_work_size, png_error)) {
    return false;
  }
  *width = decoded_width;
  *height = decoded_height;

  size_t pixels = 0u;
  const bool ok =
      image_mul_size((size_t)decoded_width, (size_t)decoded_height, &pixels) &&
      pixels <= rgb565_pixels &&
      hal_pixel_rgba8888_buffer_to_rgb565_ex(rgba, rgb565, pixels) == HAL_OK;
  free(rgba);
  return ok;
}
#endif

#ifdef HAL_ENABLE_JPEG
typedef struct {
  const uint8_t *input;
  size_t input_size;
  size_t input_offset;
  uint16_t *output;
  size_t output_pixels;
  size_t output_width;
  bool output_valid;
} image_jpeg_context_t;

static size_t image_jpeg_input(JDEC *decoder, uint8_t *buffer, size_t length) {
  image_jpeg_context_t *context = (image_jpeg_context_t *)decoder->device;
  if (context == nullptr || context->input_offset > context->input_size) {
    return 0u;
  }
  const size_t remaining = context->input_size - context->input_offset;
  if (length > remaining) {
    length = remaining;
  }
  if (buffer != nullptr && length != 0u) {
    memcpy(buffer, context->input + context->input_offset, length);
  }
  context->input_offset += length;
  return length;
}

static int image_jpeg_output(JDEC *decoder, void *bitmap, JRECT *rectangle) {
  image_jpeg_context_t *context = (image_jpeg_context_t *)decoder->device;
  if (context == nullptr || bitmap == nullptr || rectangle == nullptr ||
      rectangle->right < rectangle->left ||
      rectangle->bottom < rectangle->top) {
    return 0;
  }

  const size_t block_width =
      (size_t)rectangle->right - (size_t)rectangle->left + 1u;
  const size_t block_height =
      (size_t)rectangle->bottom - (size_t)rectangle->top + 1u;
  const uint16_t *source = (const uint16_t *)bitmap;
  for (size_t row = 0u; row < block_height; ++row) {
    const size_t output_row = (size_t)rectangle->top + row;
    size_t output_offset = 0u;
    if (!image_mul_size(output_row, context->output_width, &output_offset) ||
        output_offset > context->output_pixels ||
        (size_t)rectangle->left > context->output_pixels - output_offset ||
        block_width >
            context->output_pixels - output_offset - (size_t)rectangle->left) {
      context->output_valid = false;
      return 0;
    }
    output_offset += (size_t)rectangle->left;
    memcpy(context->output + output_offset, source + row * block_width,
           block_width * sizeof(*source));
  }
  return 1;
}

bool hal_image_jpeg_decode_rgb565(const uint8_t *jpeg, size_t jpeg_size,
                                  uint16_t *rgb565, size_t rgb565_pixels,
                                  unsigned *width, unsigned *height) {
  if (width != nullptr) {
    *width = 0u;
  }
  if (height != nullptr) {
    *height = 0u;
  }
  if (jpeg == nullptr || jpeg_size == 0u || rgb565 == nullptr ||
      width == nullptr || height == nullptr) {
    return false;
  }

  void *workspace = malloc(TJPGD_WORKSPACE_SIZE);
  if (workspace == nullptr) {
    return false;
  }
  image_jpeg_context_t context = {};
  context.input = jpeg;
  context.input_size = jpeg_size;
  context.output = rgb565;
  context.output_pixels = rgb565_pixels;
  context.output_valid = true;

  JDEC decoder = {};
  decoder.swap = 0u;
  JRESULT result = jd_prepare(&decoder, image_jpeg_input, workspace,
                              TJPGD_WORKSPACE_SIZE, &context);
  if (result != JDR_OK) {
    free(workspace);
    return false;
  }

  size_t pixels = 0u;
  if (!image_mul_size((size_t)decoder.width, (size_t)decoder.height, &pixels) ||
      pixels > rgb565_pixels) {
    free(workspace);
    return false;
  }
  context.output_width = (size_t)decoder.width;
  result = jd_decomp(&decoder, image_jpeg_output, 0u);
  free(workspace);
  if (result != JDR_OK || !context.output_valid) {
    return false;
  }
  *width = (unsigned)decoder.width;
  *height = (unsigned)decoder.height;
  return true;
}
#endif

#ifdef HAL_ENABLE_JPEG_AS_BASE64
bool hal_image_jpeg_base64_decoded_size(const char *base64, size_t base64_len,
                                        size_t *jpeg_size) {
  if (jpeg_size != nullptr) {
    *jpeg_size = 0u;
  }
  return jpeg_size != nullptr && (base64 != nullptr || base64_len == 0u) &&
         hal_base64_decode(base64, base64_len, nullptr, 0u, jpeg_size);
}

bool hal_image_jpeg_base64_decode_rgb565(const char *base64, size_t base64_len,
                                         uint8_t *jpeg_work,
                                         size_t jpeg_work_size,
                                         uint16_t *rgb565, size_t rgb565_pixels,
                                         unsigned *width, unsigned *height) {
  if (width != nullptr) {
    *width = 0u;
  }
  if (height != nullptr) {
    *height = 0u;
  }
  if (base64 == nullptr || jpeg_work == nullptr || rgb565 == nullptr ||
      width == nullptr || height == nullptr) {
    return false;
  }
  size_t jpeg_size = 0u;
  return hal_base64_decode(base64, base64_len, jpeg_work, jpeg_work_size,
                           &jpeg_size) &&
         hal_image_jpeg_decode_rgb565(jpeg_work, jpeg_size, rgb565,
                                      rgb565_pixels, width, height);
}
#endif
