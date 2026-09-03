#include "hal/display/hal_pixel.h"

uint16_t hal_pixel_rgb888_to_rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  const uint16_t r5 = (uint16_t)(red >> 3u) & UINT16_C(0x1F);
  const uint16_t g6 = (uint16_t)(green >> 2u) & UINT16_C(0x3F);
  const uint16_t b5 = (uint16_t)(blue >> 3u) & UINT16_C(0x1F);
  return (uint16_t)((uint16_t)(r5 << 11u) | (uint16_t)(g6 << 5u) | b5);
}

hal_status_t hal_pixel_rgb888_buffer_to_rgb565_ex(const uint8_t *rgb,
                                                  uint16_t *rgb565,
                                                  size_t pixel_count) {
  if (rgb == nullptr || rgb565 == nullptr) {
    return HAL_EINVAL;
  }
  for (size_t i = 0u; i < pixel_count; ++i) {
    const size_t source = i * 3u;
    rgb565[i] = hal_pixel_rgb888_to_rgb565(rgb[source], rgb[source + 1u],
                                           rgb[source + 2u]);
  }
  return HAL_OK;
}

hal_status_t hal_pixel_rgba8888_buffer_to_rgb565_ex(const uint8_t *rgba,
                                                    uint16_t *rgb565,
                                                    size_t pixel_count) {
  if (rgba == nullptr || rgb565 == nullptr) {
    return HAL_EINVAL;
  }
  for (size_t i = 0u; i < pixel_count; ++i) {
    const size_t source = i * 4u;
    rgb565[i] = hal_pixel_rgb888_to_rgb565(rgba[source], rgba[source + 1u],
                                           rgba[source + 2u]);
  }
  return HAL_OK;
}
