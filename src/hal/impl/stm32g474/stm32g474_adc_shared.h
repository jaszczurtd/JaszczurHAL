#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void stm32g474_adc_set_resolution(uint8_t bits);
int stm32g474_adc_read_gpio(uint8_t pin);

/* Raw 12-bit ADC1 code from the internal die-temperature channel (IN16) /
 * VREFINT channel (IN18). Both force the conversion to 12-bit resolution
 * (matching the factory calibration bytes) regardless of the resolution set
 * via stm32g474_adc_set_resolution(), then restore it. Host-sanity builds
 * (no JH_STM32G474_HW) return 0. */
uint16_t stm32g474_adc_read_temp_sensor_raw(void);
uint16_t stm32g474_adc_read_vrefint_raw(void);

#ifdef __cplusplus
}
#endif
