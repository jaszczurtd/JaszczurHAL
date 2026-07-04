#include <hal/hal_app.h>
#include <hal/hal_net_commands.h>
#include <hal/hal_system.h>
#include <hal/hal_wifi.h>
#include <stdio.h>
#include <string.h>
#include <tools_c.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";

static uint32_t last_wifi_check_ms = 0;
static uint32_t last_status_ms = 0;
static bool http_started = false;
static bool ws_started = false;
static uint32_t command_count = 0;

static const char *source_name(hal_net_commands_source_t source) {
  switch (source) {
  case HAL_NET_COMMANDS_SOURCE_HTTP:
    return "http";
  case HAL_NET_COMMANDS_SOURCE_WEBSOCKET:
    return "websocket";
  default:
    return "direct";
  }
}

static hal_status_t status_command(const hal_net_command_request_t *request,
                                   hal_net_command_response_t *response,
                                   void *user) {
  (void)user;
  command_count++;

  char ip[32] = {0};
  hal_wifi_get_local_ip(ip, sizeof(ip));

  cJSON *root = cJSON_CreateObject();
  if (!root) {
    return HAL_ENOMEM;
  }
  cJSON_AddStringToObject(root, "cmd", request->command);
  cJSON_AddStringToObject(root, "source", source_name(request->source));
  cJSON_AddNumberToObject(root, "uptime_ms", (double)hal_millis());
  cJSON_AddNumberToObject(root, "commands", (double)command_count);
  cJSON_AddStringToObject(root, "ip", ip);
  cJSON_AddNumberToObject(root, "rssi", (double)hal_wifi_rssi());
  cJSON_AddNumberToObject(root, "ws_clients",
                          (double)hal_websocket_client_count());

  hal_status_t status = hal_net_command_response_write_json(response, root);
  cJSON_Delete(root);
  return status;
}

static const char *json_text_arg(const hal_net_command_request_t *request) {
  if (request->json_args == NULL) {
    return NULL;
  }
  if (cJSON_IsString(request->json_args)) {
    return request->json_args->valuestring;
  }
  const cJSON *text =
      cJSON_GetObjectItemCaseSensitive(request->json_args, "text");
  return cJSON_IsString(text) ? text->valuestring : NULL;
}

static hal_status_t echo_command(const hal_net_command_request_t *request,
                                 hal_net_command_response_t *response,
                                 void *user) {
  (void)user;
  command_count++;

  const char *text = json_text_arg(request);
  if (!text || text[0] == '\0') {
    text = request->args_text ? request->args_text : "";
  }

  if (request->json_root != NULL) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
      return HAL_ENOMEM;
    }
    cJSON_AddStringToObject(root, "echo", text);
    cJSON_AddNumberToObject(root, "commands", (double)command_count);
    hal_status_t status = hal_net_command_response_write_json(response, root);
    cJSON_Delete(root);
    return status;
  }

  hal_status_t status = hal_net_command_response_write_str(response, text);
  if (status != HAL_OK) {
    return status;
  }
  return hal_net_command_response_write_str(response, "\n");
}

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
      "Commands</title></head><body><h1>JaszczurHAL Commands</h1>"
      "<pre id=\"log\">ready</pre><button onclick=\"postStatus()\">HTTP "
      "status</button> <button onclick=\"wsStatus()\">WS status</button>"
      "<script>"
      "const log=document.getElementById('log');"
      "const ws=new WebSocket('ws://'+location.hostname+':81/ws');"
      "ws.onmessage=e=>log.textContent+='\\nWS '+e.data;"
      "function show(x){log.textContent+='\\nHTTP '+x;}"
      "function postStatus(){fetch('/api/command',{method:'POST',body:"
      "JSON.stringify({cmd:'status'})}).then(r=>r.text()).then(show);}"
      "function wsStatus(){ws.send(JSON.stringify({cmd:'status'}));}"
      "</script></body></html>");
}

static void on_ws_message(hal_websocket_client_t client,
                          hal_websocket_message_type_t type,
                          const uint8_t *data, size_t len, void *user) {
  (void)user;
  hal_net_commands_handle_websocket_message(client, type, data, len,
                                            HAL_NET_COMMANDS_FORMAT_AUTO);
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
  hal_wifi_set_hostname("jaszczurhal-cmd");
  hal_wifi_begin_station(WIFI_SSID, WIFI_PASSWORD, true);
}

static void start_servers_when_ready(void) {
  if (!hal_wifi_is_connected()) {
    return;
  }

  if (!http_started) {
    hal_http_server_route(HAL_HTTP_METHOD_GET, "/", root_handler, NULL);
    hal_net_commands_register_http_route(HAL_NET_COMMANDS_DEFAULT_HTTP_PATH,
                                         HAL_NET_COMMANDS_FORMAT_AUTO);
    http_started = hal_http_server_start(80u) == HAL_OK;
  }

  if (!ws_started) {
    hal_websocket_callbacks_t callbacks = {0};
    callbacks.on_message = on_ws_message;
    hal_websocket_server_set_callbacks(&callbacks, NULL);
    ws_started = hal_websocket_server_start(81u, "/ws") == HAL_OK;
  }

  if (http_started && ws_started) {
    char ip[32] = {0};
    hal_wifi_get_local_ip(ip, sizeof(ip));
    deb("Commands: http://%s/  ws://%s:81/ws", ip, ip);
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

  deb("Commands: http=%d ws=%d registered=%lu commands=%lu clients=%lu",
      hal_http_server_is_running() ? 1 : 0,
      hal_websocket_server_is_running() ? 1 : 0,
      (unsigned long)hal_net_commands_count(), (unsigned long)command_count,
      (unsigned long)hal_websocket_client_count());
}

void app_start(void) {
  debugInit();
  hal_net_commands_register("status", status_command, NULL);
  hal_net_commands_register("echo", echo_command, NULL);
}

void app_task0(void) {
  connect_wifi();
  start_servers_when_ready();
  hal_http_server_poll();
  hal_websocket_server_poll();
  print_status();
}
