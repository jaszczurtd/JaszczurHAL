#ifndef JH_HAL_DISPLAY_INTERNAL_H
#define JH_HAL_DISPLAY_INTERNAL_H

#include "hal/display/hal_display.h"

#ifdef __cplusplus
extern "C" {
#endif

void jh_hal_display_get_dimensions(int *out_width, int *out_height);

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
