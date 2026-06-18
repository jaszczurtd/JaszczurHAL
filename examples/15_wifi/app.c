#include <hal/hal_app.h>
#include <hal/hal_system.h>
#include <hal/hal_wifi.h>
#include <tools_c.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";

static uint32_t last_connect_ms = 0;
static uint32_t last_status_ms = 0;

static void connectWifi(void) {
  if (hal_wifi_is_connected()) {
    return;
  }

  const uint32_t now = hal_millis();
  if (now - last_connect_ms < 5000u) {
    return;
  }
  last_connect_ms = now;

  hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  hal_wifi_set_hostname("jaszczurhal-wifi");
  hal_wifi_begin_station(WIFI_SSID, WIFI_PASSWORD, true);
  deb("WiFi: connecting to %s", WIFI_SSID);
}

static void printWifiStatus(void) {
  const uint32_t now = hal_millis();
  if (now - last_status_ms < 5000u) {
    return;
  }
  last_status_ms = now;

  if (!hal_wifi_is_connected()) {
    deb("WiFi: disconnected, status=%d", hal_wifi_status());
    return;
  }

  char ip[32] = {0};
  char mac[32] = {0};
  hal_wifi_get_local_ip(ip, sizeof(ip));
  hal_wifi_get_mac(mac, sizeof(mac));

  deb("WiFi: ip=%s mac=%s rssi=%ld bars=%d", ip, mac, (long)hal_wifi_rssi(),
      hal_wifi_get_strength());
}

void app_start(void) { debugInit(); }

void app_task0(void) {
  connectWifi();
  printWifiStatus();
}
