/**
 * @file app.cpp
 * @brief ESP32-S3 OTA hardware test: raw ESP-IDF application OTA, trial
 *        boot, confirmation and rollback, driven by verify_ota.py.
 *
 * The firmware joins the network, starts the OTA service and answers single
 * byte commands on the debug serial port: `S` prints one `JH_ESP32_OTA`
 * status line, `C` confirms the running image and `R` restarts the board.
 * The IMAGE_B variant differs only in the image name it reports.
 */
#include <hal/network/hal_wifi.h>
#include <hal/network/ota/hal_ota.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#include <esp_system.h>

#include <stdio.h>
#include <string.h>

#if !__has_include("ota_test_config.h")
#error                                                                         \
    "Create ota_test_config.h with JH_OTA_TEST_SSID, JH_OTA_TEST_WIFI_PASSWORD and JH_OTA_TEST_PASSWORD (see README.md)"
#endif
#include "ota_test_config.h"

#if defined(JH_OTA_TEST_IMAGE_B)
#define JH_OTA_TEST_IMAGE "B"
#else
#define JH_OTA_TEST_IMAGE "A"
#endif

namespace {

constexpr const char *kHostname = "jh-ota-esp32s3";
constexpr uint16_t kOtaPort = 8266u;
constexpr uint32_t kJoinRetryMs = 3000u;

bool s_ota_started = false;
uint32_t s_last_join_ms = 0u;
unsigned s_ota_errors = 0u;
hal_ota_error_t s_last_error = HAL_OTA_ERROR_UNKNOWN;
unsigned s_ota_starts = 0u;
char s_line[320];

void ota_error(hal_ota_error_t error, void *) {
  ++s_ota_errors;
  s_last_error = error;
}

void ota_start(hal_ota_command_t, void *) { ++s_ota_starts; }

void join(void) {
  if (hal_wifi_is_connected() || hal_millis() - s_last_join_ms < kJoinRetryMs) {
    return;
  }
  s_last_join_ms = hal_millis();
  if (hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA) == HAL_OK &&
      hal_wifi_set_hostname_ex(kHostname) == HAL_OK) {
    (void)hal_wifi_begin_station_ex(JH_OTA_TEST_SSID, JH_OTA_TEST_WIFI_PASSWORD,
                                    false);
  }
}

void print_status(void) {
  hal_ota_boot_info_t info = {};
  const hal_status_t info_status = hal_ota_get_boot_info_ex(&info);
  char ip[20] = "0.0.0.0";
  (void)hal_wifi_get_local_ip_ex(ip, sizeof(ip));
  (void)snprintf(s_line, sizeof(s_line),
                 "JH_ESP32_OTA image=%s ip=%s wifi=%u ota=%u info=%s mode=%d "
                 "attempts=%u max=%u program_version=%s staging_version=%s "
                 "starts=%u errors=%u last_error=%d",
                 JH_OTA_TEST_IMAGE, ip, hal_wifi_is_connected() ? 1u : 0u,
                 s_ota_started ? 1u : 0u, hal_status_to_string(info_status),
                 static_cast<int>(info.mode), (unsigned)info.attempts,
                 (unsigned)info.max_attempts,
                 info.program_version[0] != '\0' ? info.program_version : "-",
                 info.staging_version[0] != '\0' ? info.staging_version : "-",
                 s_ota_starts, s_ota_errors, static_cast<int>(s_last_error));
  hal_serial_println(s_line);
}

void service_serial(void) {
  while (hal_serial_available() > 0) {
    const int command = hal_serial_read();
    if (command == 'S') {
      print_status();
    } else if (command == 'C') {
      const hal_status_t status = hal_ota_confirm_boot_ex();
      (void)snprintf(s_line, sizeof(s_line), "JH_ESP32_OTA confirm=%s",
                     hal_status_to_string(status));
      hal_serial_println(s_line);
    } else if (command == 'R') {
      hal_serial_println("JH_ESP32_OTA restarting=1");
      hal_delay_ms(50u);
      esp_restart();
    }
  }
}

} // namespace

extern "C" void app_start(void) {
  hal_serial_begin(115200u);
  hal_serial_set_flush(true);
  (void)hal_ota_set_hostname(kHostname);
  (void)hal_ota_set_port(kOtaPort);
  (void)hal_ota_set_password(JH_OTA_TEST_PASSWORD);
  (void)hal_ota_on_error(ota_error, nullptr);
  (void)hal_ota_on_start(ota_start, nullptr);
}

extern "C" void app_task0(void) {
  service_serial();
  join();
  if (hal_wifi_is_connected() && !s_ota_started) {
    s_ota_started = hal_ota_begin();
  }
  if (s_ota_started) {
    hal_ota_handle();
  }
  hal_delay_ms(1u);
}
