#ifndef JH_HAL_RGB_LED_INTERNAL_H
#define JH_HAL_RGB_LED_INTERNAL_H

#include "hal/core/hal_status.h"

bool jh_hal_rgb_led_pin_valid(uint8_t pin);
hal_status_t jh_hal_rgb_led_prepare_transport(uint8_t pin, bool is800khz);
void jh_hal_rgb_led_release_transport(void);
bool jh_hal_rgb_led_write_pixels(const uint8_t *pixels, uint32_t num_bytes,
                                 bool is800khz, uint8_t pin, void *user);

#endif
