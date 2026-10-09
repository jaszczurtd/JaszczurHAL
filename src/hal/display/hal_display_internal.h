#ifndef JH_HAL_DISPLAY_INTERNAL_H
#define JH_HAL_DISPLAY_INTERNAL_H

#include "hal/display/hal_display.h"

#ifdef __cplusplus
extern "C" {
#endif

void jh_hal_display_get_dimensions(int *out_width, int *out_height);

#ifdef HAL_ENABLE_TFT
/* Backend part of hal_display_init_ili9341_ex(); config is already valid. */
hal_status_t
jh_hal_display_init_ili9341(const hal_display_ili9341_config_t *config);
#endif

#ifdef __cplusplus
}
#endif

#endif
