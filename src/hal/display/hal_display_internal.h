#ifndef JH_HAL_DISPLAY_INTERNAL_H
#define JH_HAL_DISPLAY_INTERNAL_H

#include "hal/display/hal_display.h"

#ifdef __cplusplus
extern "C" {
#endif

void jh_hal_display_get_dimensions(int *out_width, int *out_height);

/**
 * @brief Compute readable bytes for strided rows/pages, excluding final
 * padding.
 * @param row_bytes Bytes used in each row/page; zero requires exactly one row.
 * @param stride Bytes between row/page starts, at least row_bytes.
 * @param rows Number of rows/pages, at least one.
 * @param out_size Non-NULL output; unchanged on error.
 * @return HAL_OK, or HAL_EINVAL for invalid arguments or size_t overflow.
 */
static inline hal_status_t jh_display_buffer_span(size_t row_bytes,
                                                  size_t stride, size_t rows,
                                                  size_t *out_size) {
  if (out_size == NULL || rows == 0u || stride < row_bytes ||
      (row_bytes == 0u && rows != 1u) ||
      (stride > 0u && rows - 1u > (SIZE_MAX - row_bytes) / stride)) {
    return HAL_EINVAL;
  }
  *out_size = (rows - 1u) * stride + row_bytes;
  return HAL_OK;
}

/* Shared validation/rendering. Caller serializes access to its backend. */
typedef hal_status_t (*jh_display_mono_page_fn)(void *context, int x, int y,
                                                uint8_t *columns,
                                                uint16_t width,
                                                uint16_t foreground,
                                                uint16_t background);
hal_status_t jh_display_mono_text(int x, int y, const char *text,
                                  const hal_display_mono_font_t *font,
                                  uint8_t scale, uint16_t foreground,
                                  uint16_t background, int width, int height,
                                  jh_display_mono_page_fn write_page,
                                  void *context);
#ifdef HAL_ENABLE_SSD1306
hal_status_t jh_display_validate_oled_config(
    const hal_display_ssd1306_family_config_t *config);
#endif

#ifdef HAL_ENABLE_TFT
/* Backend part of hal_display_init_ili9341_ex(); config is already valid. */
hal_status_t
jh_hal_display_init_ili9341(const hal_display_ili9341_config_t *config);
#endif

#ifdef __cplusplus
}
#endif

#endif
