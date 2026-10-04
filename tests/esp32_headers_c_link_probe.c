/* jh_esp32_fault.h, jh_esp32_ledc.h and hal_rgb_led_internal.h link from C;
 * built into test_esp32_backend_lifecycle with the ESP32 backend. */
#include "hal/gpio/hal_rgb_led_internal.h"
#include "hal/impl/esp32/jh_esp32_fault.h"
#include "hal/impl/esp32/jh_esp32_ledc.h"

#include "c_link_probe.h"

bool jh_esp32_headers_link_from_c(void);

bool jh_esp32_headers_link_from_c(void) {
  static const volatile c_link_probe_fn_t k_functions[] = {
      C_LINK_PROBE_FN(jh_esp32_fault_init),
      C_LINK_PROBE_FN(jh_esp32_fault_available),
      C_LINK_PROBE_FN(jh_esp32_fault_get),
      C_LINK_PROBE_FN(jh_esp32_fault_clear),
      C_LINK_PROBE_FN(jh_esp32_ledc_acquire),
      C_LINK_PROBE_FN(jh_esp32_ledc_write),
      C_LINK_PROBE_FN(jh_esp32_ledc_write_from_isr),
      C_LINK_PROBE_FN(jh_esp32_ledc_stop),
      C_LINK_PROBE_FN(jh_esp32_ledc_release),
      C_LINK_PROBE_FN(jh_esp32_ledc_source_clock_hz),
      C_LINK_PROBE_FN(jh_hal_rgb_led_pin_valid),
      C_LINK_PROBE_FN(jh_hal_rgb_led_prepare_transport),
      C_LINK_PROBE_FN(jh_hal_rgb_led_release_transport),
      C_LINK_PROBE_FN(jh_hal_rgb_led_write_pixels),
  };
  return C_LINK_PROBE_ALL(k_functions);
}
