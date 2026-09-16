#pragma once

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void stm32g474_adc_set_resolution(uint8_t bits);
int stm32g474_adc_read_gpio(uint8_t pin);

/* Reserve ADC1 for a long-running DMA scan. The reservation is shared with
 * the polled ADC and die-temperature paths so they cannot reprogram ADC1
 * while audio DMA is active. */
hal_status_t stm32g474_adc_acquire_dma(void);
void stm32g474_adc_release_dma(void);

/* While a DMA owner also publishes samples, on-demand reads of the pins it
 * scans return the newest scanned sample instead of 0. The reader runs under
 * the ADC mutex and must not take it. Cleared on release. */
typedef bool (*stm32g474_adc_scan_reader_fn)(uint8_t pin, uint16_t *raw);
void stm32g474_adc_set_scan_reader(stm32g474_adc_scan_reader_fn reader);

/* Raw 12-bit ADC1 code from the internal die-temperature channel (IN16) /
 * VREFINT channel (IN18). Both force the conversion to 12-bit resolution
 * (matching the factory calibration bytes) regardless of the resolution set
 * via stm32g474_adc_set_resolution(), then restore it. Host-sanity builds
 * (no JH_STM32G474_HW) return 0. */
uint16_t stm32g474_adc_read_temp_sensor_raw(void);
uint16_t stm32g474_adc_read_vrefint_raw(void);
hal_status_t stm32g474_adc_read_internal_pair(uint16_t *out_temp_raw,
                                              uint16_t *out_vref_raw);

#ifdef __cplusplus
}
#endif
