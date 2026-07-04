#include <hal/hal_app.h>
#include <hal/hal_http_server.h>
#include <hal/hal_system.h>
#include <hal/hal_websocket.h>
#include <hal/hal_wifi.h>
#include <stdio.h>
#include <string.h>
#include <tools_c.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";

static uint32_t last_wifi_check_ms = 0;
static uint32_t last_status_ms = 0;
static uint32_t last_broadcast_ms = 0;
static bool http_started = false;
static bool ws_started = false;
static uint32_t ws_messages = 0;

static hal_status_t root_handler(const hal_http_request_t *request,
                                 hal_http_response_t *response, void *user) {
  (void)request;
  (void)user;

  hal_status_t status =
      hal_http_response_set_content_type(response, "text/html; charset=utf-8");
  if (status != HAL_OK) {
    return status;
  }
  return hal_http_response_write_str(
      response,
      "<!doctype html><html><head><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><title>JaszczurHAL "
      "WebSocket</title></head><body><h1>JaszczurHAL WebSocket</h1>"
      "<pre id=\"log\">connecting...</pre><button onclick=\"sendPing()\">"
      "send ping</button><script>"
      "const log=document.getElementById('log');"
      "const ws=new WebSocket('ws://'+location.hostname+':81/ws');"
      "ws.onopen=()=>log.textContent='connected\\n';"
      "ws.onmessage=e=>log.textContent+=e.data+'\\n';"
      "ws.onclose=()=>log.textContent+='closed\\n';"
      "function sendPing(){ws.send('ping from browser');}"
      "</script></body></html>");
}

static void on_ws_connect(hal_websocket_client_t client, void *user) {
  (void)user;
  deb("WS: client %u connected", (unsigned)client);
  hal_websocket_send_text(client, "hello from JaszczurHAL");
}

static void on_ws_message(hal_websocket_client_t client,
                          hal_websocket_message_type_t type,
                          const uint8_t *data, size_t len, void *user) {
  (void)user;
  ws_messages++;

  if (type != HAL_WEBSOCKET_MESSAGE_TEXT) {
    hal_websocket_send_text(client, "binary message ignored");
    return;
  }

  char reply[96] = {0};
  size_t copy_len = len;
  if (copy_len > 48u) {
    copy_len = 48u;
  }
  char text[49] = {0};
  if (copy_len > 0u) {
    memcpy(text, data, copy_len);
  }
  snprintf(reply, sizeof(reply), "echo: %s", text);
  hal_websocket_send_text(client, reply);
}

static void on_ws_disconnect(hal_websocket_client_t client, uint16_t close_code,
                             void *user) {
  (void)user;
  deb("WS: client %u disconnected code=%u", (unsigned)client,
      (unsigned)close_code);
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
  hal_wifi_set_hostname("jaszczurhal-ws");
  hal_wifi_begin_station(WIFI_SSID, WIFI_PASSWORD, true);
}

static void start_servers_when_ready(void) {
  if (!hal_wifi_is_connected()) {
    return;
  }

  if (!http_started) {
    hal_http_server_route(HAL_HTTP_METHOD_GET, "/", root_handler, NULL);
    http_started = hal_http_server_start(80u) == HAL_OK;
  }

  if (!ws_started) {
    hal_websocket_callbacks_t callbacks = {0};
    callbacks.on_connect = on_ws_connect;
    callbacks.on_message = on_ws_message;
    callbacks.on_disconnect = on_ws_disconnect;
    hal_websocket_server_set_callbacks(&callbacks, NULL);
    ws_started = hal_websocket_server_start(81u, "/ws") == HAL_OK;
  }

  if (http_started && ws_started) {
    char ip[32] = {0};
    hal_wifi_get_local_ip(ip, sizeof(ip));
    deb("HTTP: http://%s/  WS: ws://%s:81/ws", ip, ip);
  }
}

static void broadcast_status(void) {
  if (!ws_started || hal_websocket_client_count() == 0u) {
    return;
  }

  const uint32_t now = hal_millis();
  if (now - last_broadcast_ms < 1000u) {
    return;
  }
  last_broadcast_ms = now;

  char payload[96] = {0};
  snprintf(payload, sizeof(payload),
           "uptime=%lu ms clients=%lu messages=%lu rssi=%ld",
           (unsigned long)now, (unsigned long)hal_websocket_client_count(),
           (unsigned long)ws_messages, (long)hal_wifi_rssi());
  hal_websocket_broadcast_text(payload, NULL);
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
  deb("WS: running=%d clients=%lu messages=%lu",
      hal_websocket_server_is_running() ? 1 : 0,
      (unsigned long)hal_websocket_client_count(), (unsigned long)ws_messages);
}

void app_start(void) { debugInit(); }

void app_task0(void) {
  connect_wifi();
  start_servers_when_ready();
  hal_http_server_poll();
  hal_websocket_server_poll();
  broadcast_status();
  print_status();
}
