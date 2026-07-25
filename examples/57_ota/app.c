#include <hal/hal_app.h>
#include <hal/hal_ota.h>
#include <hal/hal_system.h>
#include <hal/hal_wifi.h>
#include <tools_c.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";
static const char *OTA_HOSTNAME = "jaszczurhal-ota";
static const char *OTA_PASSWORD = "change-this-ota-password";
static const uint16_t OTA_PORT = 8266u;

static bool ota_started;
static uint32_t last_connect_ms;

static void ota_error(hal_ota_error_t error, void *user) {
  (void)user;
  deb("OTA error: %d", (int)error);
}

static void connect_wifi(void) {
  if (hal_wifi_is_connected()) {
    return;
  }
  const uint32_t now = hal_millis();
  if (now - last_connect_ms < 5000u) {
    return;
  }
  last_connect_ms = now;
  hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  hal_wifi_set_hostname(OTA_HOSTNAME);
  hal_wifi_begin_station(WIFI_SSID, WIFI_PASSWORD, true);
}

void app_start(void) {
  debugInit();
  hal_ota_set_hostname(OTA_HOSTNAME);
  hal_ota_set_port(OTA_PORT);
  hal_ota_set_password(OTA_PASSWORD);
  hal_ota_on_error(ota_error, NULL);
}

void app_task0(void) {
  connect_wifi();
  if (hal_wifi_is_connected() && !ota_started) {
    if (hal_ota_confirm_boot_ex() != HAL_OK) {
      deb("OTA: no trial boot to confirm");
    }
    ota_started = hal_ota_begin();
    deb("OTA: %s on UDP %u", ota_started ? "ready" : "start failed",
        (unsigned)OTA_PORT);
  }
  if (ota_started) {
    hal_ota_handle();
  }
  hal_delay_ms(1u);
}
