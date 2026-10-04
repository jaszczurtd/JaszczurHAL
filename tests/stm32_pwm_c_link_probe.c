/* hal_pwm_stm32g474.h links from C; built into test_stm32_pwm_clock with
 * the STM32 PWM backend. */
#include "hal/impl/stm32g474/hal_pwm_stm32g474.h"

#include "c_link_probe.h"

bool jh_stm32_pwm_header_links_from_c(void);

bool jh_stm32_pwm_header_links_from_c(void) {
  static const volatile c_link_probe_fn_t k_functions[] = {
      C_LINK_PROBE_FN(jh_stm32_pwm_prepare_pin),
      C_LINK_PROBE_FN(jh_stm32_pwm_pin_supported),
      C_LINK_PROBE_FN(jh_stm32_pwm_source_clock_hz),
      C_LINK_PROBE_FN(jh_stm32_pwm_prepare_frequency_pin),
      C_LINK_PROBE_FN(jh_stm32_pwm_write_compare),
      C_LINK_PROBE_FN(jh_stm32_pwm_start_output),
      C_LINK_PROBE_FN(jh_stm32_pwm_release_output),
      C_LINK_PROBE_FN(jh_stm32_pwm_compare_address),
      C_LINK_PROBE_FN(jh_stm32_pwm_timer_dma_request),
      C_LINK_PROBE_FN(jh_stm32_pwm_set_update_dma_request),
  };
  return C_LINK_PROBE_ALL(k_functions);
}
