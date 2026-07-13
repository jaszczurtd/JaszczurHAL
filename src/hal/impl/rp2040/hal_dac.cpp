#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#ifdef HAL_ENABLE_DAC

#include "../../hal_dac.h"

/*
 * The RP2040 has NO true DAC peripheral. Rather than silently fake it, this
 * backend reports the capability as absent so portable code can branch on
 * hal_dac_is_supported(). For an analog output on RP2040, use hal_pwm() into
 * an RC low-pass filter.
 *
 * (A PWM+filter emulation could be added here behind an opt-in flag, but it
 * would have PWM ripple/bandwidth characteristics, not DAC ones, so it is
 * intentionally not presented as a DAC.)
 */

bool hal_dac_is_supported(void) { return false; }
uint8_t hal_dac_resolution_bits(void) { return 0u; }
uint16_t hal_dac_max_value(void) { return 0u; }
hal_status_t hal_dac_init_ex(uint8_t channel) {
  (void)channel;
  return HAL_EUNSUPPORTED;
}
bool hal_dac_init(uint8_t channel) {
  return hal_status_to_bool(hal_dac_init_ex(channel));
}
hal_status_t hal_dac_write(uint8_t channel, uint16_t value) {
  (void)channel;
  (void)value;
  return HAL_EUNSUPPORTED;
}
hal_status_t hal_dac_write_millivolts(uint8_t channel, uint16_t millivolts) {
  (void)channel;
  (void)millivolts;
  return HAL_EUNSUPPORTED;
}
#endif // HAL_ENABLE_DAC
#endif // HAL_TARGET_IS_RP2040
