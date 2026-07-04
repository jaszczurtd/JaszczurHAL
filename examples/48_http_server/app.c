#include <hal/hal_app.h>
#include <hal/hal_http_server.h>
#include <hal/hal_system.h>
#include <hal/hal_wifi.h>
#include <stdio.h>
#include <tools_c.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";

static uint32_t last_wifi_check_ms = 0;
static uint32_t last_status_ms = 0;
static uint32_t request_count = 0;
static bool http_started = false;

static hal_status_t root_handler(const hal_http_request_t *request,
                                 hal_http_response_t *response, void *user) {
  (void)request;
  (void)user;

  request_count++;
  hal_status_t status =
      hal_http_response_set_content_type(response, "text/html; charset=utf-8");
  if (status != HAL_OK) {
    return status;
  }
  return hal_http_response_write_str(
      response,
      "<!doctype html><html><head><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><title>JaszczurHAL</"
      "title>"
      "</head><body><h1>JaszczurHAL HTTP</h1><p>Server is running.</p>"
      "<p>Try <a href=\"/api/status\">/api/status</a>.</p></body></html>");
}

static hal_status_t status_handler(const hal_http_request_t *request,
                                   hal_http_response_t *response, void *user) {
  (void)request;
  (void)user;

  char ip[32] = {0};
  char payload[192] = {0};
  hal_wifi_get_local_ip(ip, sizeof(ip));

  snprintf(payload, sizeof(payload),
           "{\"uptime_ms\":%lu,\"requests\":%lu,\"ip\":\"%s\",\"rssi\":%ld}",
           (unsigned long)hal_millis(), (unsigned long)request_count, ip,
           (long)hal_wifi_rssi());

  request_count++;
  hal_status_t status =
      hal_http_response_set_content_type(response, "application/json");
  if (status != HAL_OK) {
    return status;
  }
  return hal_http_response_write_str(response, payload);
}

static void connect_wifi(void) {
  if (hal_wifi_is_connected()) {
    return;
  }

  const uint32_t now = hal_millis();
  if (now - last_wifi_check_ms < 5000u) {
    return;
  }
  last_wifi_check_ms = now;

  deb("WiFi: connecting to %s", WIFI_SSID);
  hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  hal_wifi_set_hostname("jaszczurhal-http");
  hal_wifi_begin_station(WIFI_SSID, WIFI_PASSWORD, true);
}

static void start_http_when_ready(void) {
  if (http_started || !hal_wifi_is_connected()) {
    return;
  }

  hal_http_server_route(HAL_HTTP_METHOD_GET, "/", root_handler, NULL);
  hal_http_server_route(HAL_HTTP_METHOD_GET, "/api/status", status_handler,
                        NULL);

  http_started = hal_http_server_start(80u) == HAL_OK;
  if (http_started) {
    char ip[32] = {0};
    hal_wifi_get_local_ip(ip, sizeof(ip));
    deb("HTTP: listening on http://%s/", ip);
  } else {
    derr("HTTP: failed to start");
  }
}

static void print_status(void) {
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
  hal_wifi_get_local_ip(ip, sizeof(ip));
  deb("HTTP: ip=%s running=%d requests=%lu", ip,
      hal_http_server_is_running() ? 1 : 0, (unsigned long)request_count);
}

void app_start(void) { debugInit(); }

void app_task0(void) {
  connect_wifi();
  start_http_when_ready();
  hal_http_server_poll();
  print_status();
}
