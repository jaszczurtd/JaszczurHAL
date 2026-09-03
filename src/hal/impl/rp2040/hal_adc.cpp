#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/analog/hal_adc.h"
#include "hal/core/hal_config.h"
#include "hal/core/jh_resolution.h"
#include "rp2040_adc_shared.h"

static bool adc_pin_valid(uint8_t pin) { return pin >= 26u && pin <= 29u; }

void hal_adc_set_resolution(uint8_t bits) {
  rp2040_adc_set_resolution(jh_resolution_clamp_1_16(
      bits, "hal_adc_set_resolution: resolution is below 1 bit",
      "hal_adc_set_resolution: resolution is above 16 bits"));
}

int hal_adc_read(uint8_t pin) {
  if (!adc_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_adc_read: unsupported ADC pin");
    return 0;
  }

  return rp2040_adc_read_gpio(pin);
}
#endif // HAL_TARGET_IS_RP
