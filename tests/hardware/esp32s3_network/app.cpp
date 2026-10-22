/**
 * @file app.cpp
 * @brief ESP32-S3 network hardware test entry: waits for the host and runs
 *        the selected suite once.
 *
 * The default build runs the basic suite (basic_suite.cpp); the SERVICES
 * variant runs services_suite.cpp. The firmware announces the suite and its
 * ports, waits for `GO` on the debug serial port, prints one
 * `JH_ESP32_NET check=...` line per check and a final `check=result` line.
 * verify_network.py serves the host side. The status LED blinks blue while
 * the firmware waits for `GO`, stays blue while the checks run, then turns
 * green on PASS or red on FAIL. After the result the host may start a soak
 * run with `SOAK <seconds>`.
 */
#include "net_test_support.h"

#include <hal/gpio/hal_rgb_led.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#include <stdio.h>
#include <string.h>

namespace {

bool s_led_ready = false;

void set_led(hal_rgb_led_color_t color) {
  if (s_led_ready) {
    (void)hal_rgb_led_set_color(color);
  }
}

} // namespace

extern "C" void app_start(void) {
  hal_serial_begin(115200u);
  hal_serial_set_flush(true);
  s_led_ready = hal_rgb_led_init_board_status() == HAL_OK;
  hal_rgb_led_set_brightness(24u);
  set_led(HAL_RGB_LED_BLUE);
}

extern "C" void app_task0(void) {
  static bool s_finished = false;
  static uint32_t s_last_ms = 0u;
  static bool s_blink = false;
  if (s_finished) {
    // After the result the host may ask for a soak run: `SOAK <seconds>`.
    const char *command = net_test_read_line();
    unsigned seconds = 0u;
    if (command != nullptr && sscanf(command, "SOAK %u", &seconds) == 1 &&
        seconds > 0u) {
      set_led(HAL_RGB_LED_BLUE);
      net_test_run_soak(seconds);
      set_led(net_test_failed() == 0u ? HAL_RGB_LED_GREEN : HAL_RGB_LED_RED);
    }
    hal_delay_ms(10u);
    return;
  }
  const char *line = net_test_read_line();
  if (line != nullptr && strcmp(line, "GO") == 0) {
    set_led(HAL_RGB_LED_BLUE);
    net_test_run_suite();
    net_test_report_result();
    set_led(net_test_failed() == 0u ? HAL_RGB_LED_GREEN : HAL_RGB_LED_RED);
    s_finished = true;
    return;
  }
  if (hal_millis() - s_last_ms >= 1000u) {
    s_last_ms = hal_millis();
    s_blink = !s_blink;
    set_led(s_blink ? HAL_RGB_LED_BLUE : HAL_RGB_LED_NONE);
    char ready[320];
    (void)snprintf(ready, sizeof(ready), "ready=1 host=%s %s", JH_NET_TEST_HOST,
                   net_test_ready_fields());
    net_test_announce(ready);
  }
  hal_delay_ms(10u);
}
