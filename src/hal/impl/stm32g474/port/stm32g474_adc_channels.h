#ifndef JH_STM32G474_ADC_CHANNELS_H
#define JH_STM32G474_ADC_CHANNELS_H

#include <stdint.h>

/* Map a JaszczurHAL pin id (port*16 + pin) to its ADC1 external channel.
 * Channel zero is reserved here as the unsupported-pin sentinel. */
static inline uint32_t jh_stm32g474_adc1_channel_for_pin(uint8_t pin) {
  switch (pin) {
  case 0x00:
    return 1u; /* PA0  -> ADC1_IN1  */
  case 0x01:
    return 2u; /* PA1  -> ADC1_IN2  */
  case 0x02:
    return 3u; /* PA2  -> ADC1_IN3  */
  case 0x03:
    return 4u; /* PA3  -> ADC1_IN4  */
  case 0x10:
    return 15u; /* PB0  -> ADC1_IN15 */
  case 0x11:
    return 12u; /* PB1  -> ADC1_IN12 */
  case 0x1B:
    return 14u; /* PB11 -> ADC1_IN14 */
  case 0x1C:
    return 11u; /* PB12 -> ADC1_IN11 */
  case 0x1E:
    return 5u; /* PB14 -> ADC1_IN5  */
  case 0x20:
    return 6u; /* PC0  -> ADC1_IN6  */
  case 0x21:
    return 7u; /* PC1  -> ADC1_IN7  */
  case 0x22:
    return 8u; /* PC2  -> ADC1_IN8  */
  case 0x23:
    return 9u; /* PC3  -> ADC1_IN9  */
  default:
    return 0u;
  }
}

#endif
