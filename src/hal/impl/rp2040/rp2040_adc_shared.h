#pragma once

#include "hal/core/hal_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rp2040_adc_set_resolution(uint8_t bits);
int rp2040_adc_read_gpio(uint8_t pin);
uint16_t rp2040_adc_read_temperature_raw(void);
hal_status_t rp2040_adc_read_temperature_raw_ex(uint16_t *out_raw);

/* Reserve the shared ADC/FIFO for a long-running DMA scan. */
hal_status_t rp2040_adc_acquire_dma(void);
void rp2040_adc_release_dma(void);

#ifdef __cplusplus
}
#endif
