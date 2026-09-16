#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* While a continuous scan owns ADC1, on-demand reads of the pins it scans
 * return the newest scanned sample instead of a one-shot conversion, which
 * the driver would refuse on a unit in continuous mode. Cleared on stop. */
typedef bool (*jh_esp32_adc_scan_reader_fn)(uint8_t pin, uint16_t *raw);
void jh_esp32_adc_set_scan_reader(jh_esp32_adc_scan_reader_fn reader);

#ifdef __cplusplus
}
#endif
