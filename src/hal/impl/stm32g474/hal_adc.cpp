#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/analog/hal_adc.h"
#include "port/stm32g474_adc_channels.h"
#include "stm32g474_adc_shared.h"

void hal_adc_set_resolution(uint8_t bits) {
  stm32g474_adc_set_resolution(bits);
}

bool hal_adc_is_pin_supported(uint8_t pin) {
  return jh_stm32g474_adc1_channel_for_pin(pin) != 0u;
}

int hal_adc_read(uint8_t pin) { return stm32g474_adc_read_gpio(pin); }

#endif // HAL_TARGET_IS_STM32G474
