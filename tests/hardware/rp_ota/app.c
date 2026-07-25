#include <hal/hal_app.h>
#include <hal/hal_board.h>
#include <hal/hal_ota.h>
#include <hal/hal_status.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <hal/hal_usb.h>
#include <hal/hal_wifi.h>
#include <hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_gspi.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if __has_include("ota_test_secrets.h")
#include "ota_test_secrets.h"
#endif

#ifndef JH_OTA_TEST_WIFI_SSID
#define JH_OTA_TEST_WIFI_SSID ""
#endif

#ifndef JH_OTA_TEST_WIFI_PASSWORD
#define JH_OTA_TEST_WIFI_PASSWORD ""
#endif

#ifndef JH_OTA_TEST_PASSWORD
#define JH_OTA_TEST_PASSWORD ""
#endif

#ifndef JH_OTA_TEST_PORT
#define JH_OTA_TEST_PORT 8266u
#endif

#if HAL_TARGET_IS_RP2040
#define JH_OTA_TEST_HOSTNAME "jh-ota-rp2040"
#elif HAL_TARGET_IS_RP2350_ARM
#define JH_OTA_TEST_HOSTNAME "jh-ota-rp2350-arm"
#else
#error "The RP OTA hardware fixture supports RP2040 and RP2350 ARM"
#endif

#ifdef HAL_ENABLE_FREERTOS
#define JH_OTA_TEST_RUNTIME "freertos"
#else
#define JH_OTA_TEST_RUNTIME "baremetal"
#endif

static uint8_t s_response[768];
static size_t s_response_length;
static size_t s_response_offset;
static bool s_ota_started;
static uint32_t s_last_connect_ms;
static uint32_t s_ota_errors;
static hal_ota_error_t s_last_ota_error;
static hal_status_t s_wifi_begin_status = HAL_NONE;

static bool configuration_available(void) {
  return sizeof(JH_OTA_TEST_WIFI_SSID) > 1u &&
         sizeof(JH_OTA_TEST_PASSWORD) > 1u;
}

static void set_response_length(int length) {
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

static void ota_error(hal_ota_error_t error, void *user) {
  (void)user;
  s_ota_errors++;
  s_last_ota_error = error;
}

static void connect_wifi(void) {
  if (!configuration_available() || hal_wifi_is_connected()) {
    return;
  }
  const uint32_t now = hal_millis();
  if (now - s_last_connect_ms < 3000u) {
    return;
  }
  s_wifi_begin_status = hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA);
  if (s_wifi_begin_status == HAL_OK) {
    s_wifi_begin_status = hal_wifi_set_hostname_ex(JH_OTA_TEST_HOSTNAME);
  }
  if (s_wifi_begin_status == HAL_OK) {
    s_wifi_begin_status = hal_wifi_begin_station_ex(
        JH_OTA_TEST_WIFI_SSID, JH_OTA_TEST_WIFI_PASSWORD, false);
  }
  s_last_connect_ms = hal_millis();
}

static void prepare_status(void) {
  hal_ota_boot_info_t info;
  memset(&info, 0, sizeof(info));
  const hal_status_t state_status = hal_ota_get_boot_info_ex(&info);
  hal_wifi_state_t wifi_state = HAL_WIFI_STATE_OFF;
  const hal_status_t wifi_state_status = hal_wifi_get_state_ex(&wifi_state);
  char local_ip[16] = "0.0.0.0";
  (void)hal_wifi_get_local_ip_ex(local_ip, sizeof(local_ip));
  jh_rp2040_cyw43_gspi_clock_t gspi_clock;
  memset(&gspi_clock, 0, sizeof(gspi_clock));
  const hal_status_t gspi_status = jh_rp2040_cyw43_gspi_get_clock(&gspi_clock);
  const int length = snprintf(
      (char *)s_response, sizeof(s_response),
      "JHOTA-HW1 target=%s board=%s runtime=%s ipv4=%s config=%u wifi=%u "
      "ota=%u wifi_begin=%d wifi_state_status=%d wifi_state=%d state=%d "
      "mode=%d attempts=%u max=%u program_generation=%lu "
      "staging_generation=%lu program_version=%.*s staging_version=%.*s "
      "errors=%lu last_error=%d gspi_status=%d clk_sys=%lu "
      "gspi_target=%lu gspi_actual=%lu gspi_div_int=%u gspi_div_frac8=%u "
      "gspi_program=%u\n",
      HAL_TARGET_NAME, HAL_BOARD_PROFILE_NAME, JH_OTA_TEST_RUNTIME, local_ip,
      configuration_available() ? 1u : 0u, hal_wifi_is_connected() ? 1u : 0u,
      s_ota_started ? 1u : 0u, (int)s_wifi_begin_status, (int)wifi_state_status,
      (int)wifi_state, (int)state_status, (int)info.mode,
      (unsigned)info.attempts, (unsigned)info.max_attempts,
      (unsigned long)info.program_generation,
      (unsigned long)info.staging_generation, HAL_OTA_VERSION_TEXT_SIZE,
      info.program_version, HAL_OTA_VERSION_TEXT_SIZE, info.staging_version,
      (unsigned long)s_ota_errors, (int)s_last_ota_error, (int)gspi_status,
      (unsigned long)gspi_clock.clk_sys_hz,
      (unsigned long)gspi_clock.target_gspi_hz,
      (unsigned long)gspi_clock.actual_gspi_hz,
      (unsigned)gspi_clock.divider_int, (unsigned)gspi_clock.divider_frac8,
      (unsigned)gspi_clock.program);
  set_response_length(length);
}

static void prepare_confirm(void) {
  const hal_status_t status = hal_ota_confirm_boot_ex();
  const int length = snprintf((char *)s_response, sizeof(s_response),
                              "JHOTA-CONFIRM status=%d\n", (int)status);
  set_response_length(length);
}

static void service_usb(void) {
  if (s_response_offset < s_response_length) {
    size_t written = 0u;
    (void)hal_usb_cdc_write(s_response + s_response_offset,
                            s_response_length - s_response_offset, 100u,
                            &written);
    s_response_offset += written;
    return;
  }

  uint8_t command = 0u;
  size_t received = 0u;
  if (hal_usb_cdc_read(&command, 1u, &received) != HAL_OK || received != 1u) {
    return;
  }
  if (command == (uint8_t)'S') {
    prepare_status();
  } else if (command == (uint8_t)'C') {
    prepare_confirm();
  } else if (command == (uint8_t)'R') {
    (void)hal_watchdog_enable(10u, false);
    hal_delay_ms(100u);
  }
}

void app_start(void) {
  if (!configuration_available()) {
    return;
  }
  (void)hal_ota_set_hostname(JH_OTA_TEST_HOSTNAME);
  (void)hal_ota_set_port((uint16_t)JH_OTA_TEST_PORT);
  (void)hal_ota_set_password(JH_OTA_TEST_PASSWORD);
  (void)hal_ota_on_error(ota_error, NULL);
}

void app_task0(void) {
  service_usb();
  connect_wifi();
  if (hal_wifi_is_connected() && !s_ota_started) {
    s_ota_started = hal_ota_begin();
  }
  if (s_ota_started) {
    hal_ota_handle();
  }
  hal_delay_ms(1u);
}
