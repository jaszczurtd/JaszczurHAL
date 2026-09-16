#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Shortest conversion the RP2040 ADC performs, in ADC clock cycles. */
#define JH_RP_ADC_CONVERSION_CYCLES 96u

/**
 * @brief ADC clock divider that starts one conversion every @p cycles.
 * @param cycles Requested conversion period in ADC clock cycles, at least
 * JH_RP_ADC_CONVERSION_CYCLES.
 * @return Value for adc_set_clkdiv().
 * @note The pacing timer starts a conversion every divider plus one cycles.
 * A divider equal to the conversion length itself lands the trigger on the
 * cycle the previous conversion ends and the ADC skips it, halving the rate;
 * the back-to-back mode selected with a divider of 0 is the only way to run
 * at the minimum period.
 */
static inline float jh_rp_adc_scan_clkdiv(uint32_t cycles) {
  if (cycles <= JH_RP_ADC_CONVERSION_CYCLES) {
    return 0.0f;
  }
  return (float)cycles - 1.0f;
}

#ifdef __cplusplus
}
#endif
