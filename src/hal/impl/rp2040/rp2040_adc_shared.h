#pragma once

#include "hal/core/hal_status.h"

#include <stdbool.h>
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

/* While a DMA owner also publishes samples, on-demand reads of the inputs it
 * scans return the newest scanned sample instead of 0. Input 4 is the chip
 * temperature. The reader runs under the ADC mutex and must not take it. */
typedef bool (*rp2040_adc_scan_reader_fn)(uint8_t input, uint16_t *raw);
void rp2040_adc_set_scan_reader(rp2040_adc_scan_reader_fn reader);

#ifdef __cplusplus
}
#endif
