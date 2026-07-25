#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;

#define ADC_TEMPERATURE_CHANNEL_NUM 4u

#ifdef __cplusplus
extern "C" {
#endif

void adc_init(void);
void adc_gpio_init(uint gpio);
void adc_select_input(uint input);
uint16_t adc_read(void);
void adc_set_temp_sensor_enabled(bool enabled);

#ifdef __cplusplus
}
#endif
