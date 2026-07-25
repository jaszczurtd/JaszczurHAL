#include "../../hal_target.h"
#if HAL_TARGET_IS_RP
#include "../../hal_adc.h"
#include "../../hal_config.h"
#include "rp2040_adc_shared.h"

static uint8_t clamp_resolution(uint8_t bits) {
  if (bits < 1u) {
    HAL_ASSERT(false, "hal_adc_set_resolution: resolution is below 1 bit");
    return 1u;
  }
  if (bits > 16u) {
    HAL_ASSERT(false, "hal_adc_set_resolution: resolution is above 16 bits");
    return 16u;
  }
  return bits;
}

static bool adc_pin_valid(uint8_t pin) { return pin >= 26u && pin <= 29u; }

void hal_adc_set_resolution(uint8_t bits) {
  rp2040_adc_set_resolution(clamp_resolution(bits));
}

int hal_adc_read(uint8_t pin) {
  if (!adc_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_adc_read: unsupported ADC pin");
    return 0;
  }

  return rp2040_adc_read_gpio(pin);
}
#endif // HAL_TARGET_IS_RP
