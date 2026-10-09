/* The ILI9341 of the nucleo-display example as a HAL configuration. */
#include "jh_hardware.h"

#include "hal/display/hal_display.h"

static const hal_display_ili9341_config_t display =
    JH_HW_NODE_DISPLAY_CONFIG_INIT;

int main(void) {
  int failures = 0;
  failures += display.bus != JH_HW_NODE_DISPLAY_BUS_INDEX;
  failures += display.cs_pin != JH_HW_NODE_DISPLAY_PIN_CS;
  failures += display.dc_pin != JH_HW_NODE_DISPLAY_PIN_DC;
  failures += display.rst_pin != JH_HW_NODE_DISPLAY_PIN_RESET;
  failures += display.clock_hz != JH_HW_NODE_DISPLAY_FREQUENCY_HZ;
  failures += JH_HW_NODE_WIFI_RADIO_PIN_WL_ON_OWNER != 1 ||
              JH_HW_NODE_WIFI_PIN_WL_ON_OWNER != 0;
  return failures;
}
