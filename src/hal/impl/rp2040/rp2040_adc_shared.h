#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rp2040_adc_set_resolution(uint8_t bits);
int rp2040_adc_read_gpio(uint8_t pin);
uint16_t rp2040_adc_read_temperature_raw(void);

#ifdef __cplusplus
}
#endif
