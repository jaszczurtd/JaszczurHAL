#include <hal/core/hal_config.h>
#include <hal/core/hal_target.h>

#if !defined(HAL_ENABLE_FREERTOS)
#error "18_freertos_suite network variant requires HAL_ENABLE_FREERTOS"
#endif

#if !defined(HAL_ENABLE_APP_TASK1)
#error "18_freertos_suite network variant requires HAL_ENABLE_APP_TASK1"
#endif

#if !defined(HAL_ENABLE_BSD_SOCKETS) || !defined(HAL_ENABLE_HTTP_CLIENT) ||    \
    !defined(HAL_ENABLE_HTTP_FILES) || !defined(HAL_ENABLE_HTTP_SERVER) ||     \
    !defined(HAL_ENABLE_NET_COMMANDS) || !defined(HAL_ENABLE_NET_CONSOLE) ||   \
    !defined(HAL_ENABLE_NOTIFY_TELEGRAM) || !defined(HAL_ENABLE_TIME) ||       \
    !defined(HAL_ENABLE_TLS) || !defined(HAL_ENABLE_WEBSOCKET) ||              \
    !defined(HAL_ENABLE_WIFI) || !defined(HAL_ENABLE_CJSON)
#error "18_freertos_suite network variant is missing required feature flags"
#endif

#if HAL_TARGET_IS_RP && !defined(__FREERTOS)
#error "18_freertos_suite network variant requires RP FreeRTOS mode"
#endif

#if !(HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474)
#error "18_freertos_suite network variant supports RP and STM32G474 targets"
#endif

#if !defined(HAL_FREERTOS_TASK0_STACK) || HAL_FREERTOS_TASK0_STACK < 1536u
#error "network server polling requires HAL_FREERTOS_TASK0_STACK >= 1536 words"
#endif

#if HAL_TCP_LISTENER_MAX_INSTANCES < 4u
#error "network suite requires at least four TCP listener slots"
#endif

#if HAL_TCP_SOCKET_MAX_INSTANCES < 6u
#error "network suite requires at least six TCP socket slots"
#endif

#if HAL_BSD_SOCKET_MAX_FDS < 4u
#error "network suite requires at least four BSD file descriptors"
#endif

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include <hal/codecs/cjson/cJSON.h>
#include <hal/codecs/cjson/cJSON_Utils.h>
#include <hal/core/hal_app.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/network/hal_wifi.h>
#include <hal/network/http/hal_http_client.h>
#include <hal/network/http/hal_http_files.h>
#include <hal/network/http/hal_http_server.h>
#include <hal/network/net_commands/hal_net_commands.h>
#include <hal/network/net_console/hal_net_console.h>
#include <hal/network/notify/hal_notify.h>
#include <hal/network/tls/hal_tls.h>
#include <hal/network/websocket/hal_websocket.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <hal/time/hal_time.h>
#include <tools_c.h>

#if HAL_HTTP_SERVER_MAX_ROUTES < 7u
#error "network suite requires at least seven HTTP routes"
#endif

#if HAL_HTTP_SERVER_MAX_CLIENTS < 1u || HAL_WEBSOCKET_MAX_CLIENTS < 1u ||      \
    HAL_NET_CONSOLE_MAX_CLIENTS < 1u
#error "network suite services require at least one client slot each"
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef HTTP_EXAMPLE_CA_AVAILABLE
#include "ca_certificate.h"
#endif

#ifndef NETWORK_SUITE_WIFI_SSID
#define NETWORK_SUITE_WIFI_SSID "your-ssid"
#endif

#ifndef NETWORK_SUITE_WIFI_PASSWORD
#define NETWORK_SUITE_WIFI_PASSWORD "your-password"
#endif

#ifndef NETWORK_SUITE_CONSOLE_PASSWORD
#define NETWORK_SUITE_CONSOLE_PASSWORD "change-me"
#endif

#ifndef NETWORK_SUITE_UPLOAD_TOKEN
#define NETWORK_SUITE_UPLOAD_TOKEN "change-me-upload"
#endif

#ifndef NETWORK_SUITE_REMOTE_HOST
#define NETWORK_SUITE_REMOTE_HOST "192.168.1.50"
#endif

#ifndef NETWORK_SUITE_HTTP_HOST
#define NETWORK_SUITE_HTTP_HOST "example.com"
#endif

#ifndef NETWORK_SUITE_BSD_TCP_PORT
#define NETWORK_SUITE_BSD_TCP_PORT 8080u
#endif

#ifndef NETWORK_SUITE_BSD_UDP_PORT
#define NETWORK_SUITE_BSD_UDP_PORT 9000u
#endif

namespace {

constexpr uint16_t kHttpPort = 80u;
constexpr uint16_t kWebSocketPort = 81u;
constexpr uint32_t kWifiRetryMs = 5000u;
constexpr uint32_t kStatusIntervalMs = 5000u;
constexpr uint32_t kBsdClientIntervalMs = 15000u;
constexpr uint32_t kSocketTimeoutMs = 750u;
constexpr uint32_t kHttpTimeoutMs = 5000u;
constexpr uint16_t kBsdWorkerStackWords = 768u;
constexpr uint16_t kHttpWorkerStackWords = 1536u;

struct SuiteStats {
  uint32_t app_task0_ticks;
  uint32_t app_task1_ticks;
  uint32_t http_requests;
  uint32_t command_requests;
  uint32_t websocket_messages;
  uint32_t console_lines;
  uint32_t bsd_tcp_server_sessions;
  uint32_t bsd_udp_server_datagrams;
  uint32_t bsd_tcp_client_runs;
  uint32_t bsd_udp_client_runs;
  uint32_t http_client_runs;
  uint32_t https_client_runs;
};

struct RamFile {
  bool used;
  char path[96];
  uint8_t data[512];
  size_t len;
  uint32_t mtime;
};

static SemaphoreHandle_t s_stats_mutex;
static SuiteStats s_stats;
static RamFile s_files[4];
static bool s_wifi_ready;
static bool s_wifi_attempted;
static bool s_services_configured;
static bool s_http_started;
static bool s_websocket_started;
static bool s_console_started;
static bool s_services_announced;
static uint32_t s_last_wifi_attempt_ms;
static uint32_t s_last_service_start_ms;
static uint32_t s_last_status_ms;
static uint32_t s_last_broadcast_ms;

static void increment_stat(uint32_t *counter) {
  if (s_stats_mutex != nullptr &&
      xSemaphoreTake(s_stats_mutex, pdMS_TO_TICKS(20u)) == pdTRUE) {
    ++(*counter);
    xSemaphoreGive(s_stats_mutex);
  }
}

static SuiteStats stats_snapshot(void) {
  SuiteStats snapshot = {};
  if (s_stats_mutex != nullptr &&
      xSemaphoreTake(s_stats_mutex, pdMS_TO_TICKS(20u)) == pdTRUE) {
    snapshot = s_stats;
    xSemaphoreGive(s_stats_mutex);
  }
  return snapshot;
}

static void set_wifi_ready(bool ready) {
  if (s_stats_mutex != nullptr &&
      xSemaphoreTake(s_stats_mutex, pdMS_TO_TICKS(20u)) == pdTRUE) {
    s_wifi_ready = ready;
    xSemaphoreGive(s_stats_mutex);
  }
}

static bool wifi_ready(void) {
  bool ready = false;
  if (s_stats_mutex != nullptr &&
      xSemaphoreTake(s_stats_mutex, pdMS_TO_TICKS(20u)) == pdTRUE) {
    ready = s_wifi_ready;
    xSemaphoreGive(s_stats_mutex);
  }
  return ready;
}

static const char *command_source_name(hal_net_commands_source_t source) {
  switch (source) {
  case HAL_NET_COMMANDS_SOURCE_HTTP:
    return "http";
  case HAL_NET_COMMANDS_SOURCE_WEBSOCKET:
    return "websocket";
  default:
    return "direct";
  }
}

static cJSON *make_status_json(const char *source) {
  char ip[32] = {};
  char mac[32] = {};
  hal_system_architecture_t architecture = {};
  const SuiteStats stats = stats_snapshot();

  (void)hal_wifi_get_local_ip(ip, sizeof(ip));
  (void)hal_wifi_get_mac(mac, sizeof(mac));
  (void)hal_system_get_current_architecture(&architecture);

  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) {
    return nullptr;
  }

  cJSON_AddNumberToObject(root, "app0_ticks", (double)stats.app_task0_ticks);
  cJSON_AddNumberToObject(root, "app1_ticks", (double)stats.app_task1_ticks);
  cJSON_AddNumberToObject(root, "commands", (double)stats.command_requests);
  cJSON_AddNumberToObject(root, "free_heap", (double)hal_get_free_heap());
  cJSON_AddNumberToObject(root, "http_requests", (double)stats.http_requests);
  cJSON_AddStringToObject(root, "ip", ip);
  cJSON_AddStringToObject(root, "mac", mac);
  cJSON_AddNumberToObject(root, "rssi", (double)hal_wifi_rssi());
  cJSON_AddStringToObject(root, "source", source != nullptr ? source : "api");
  cJSON_AddStringToObject(root, "target",
                          architecture.target_name != nullptr
                              ? architecture.target_name
                              : "unknown");
  cJSON_AddNumberToObject(root, "uptime_ms", (double)hal_millis());
  cJSON_AddNumberToObject(root, "ws_clients",
                          (double)hal_websocket_client_count());
  cJSONUtils_SortObject(root);
  return root;
}

static void run_cjson_self_test(void) {
  static const char kConfig[] =
      "{\"name\":\"18_freertos_suite\",\"sample_ms\":250,"
      "\"enabled\":true}";
  const char *parse_end = nullptr;
  cJSON *root = cJSON_ParseWithOpts(kConfig, &parse_end, 1);
  if (root == nullptr) {
    derr("network suite: cJSON parse failed near %.16s",
         parse_end != nullptr ? parse_end : "");
    return;
  }

  const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
  const cJSON *sample_ms = cJSON_GetObjectItemCaseSensitive(root, "sample_ms");
  const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
  if (!cJSON_IsString(name) || name->valuestring == nullptr ||
      !cJSON_IsNumber(sample_ms) || !cJSON_IsBool(enabled)) {
    derr("network suite: cJSON schema check failed");
    cJSON_Delete(root);
    return;
  }

  cJSON_AddNumberToObject(root, "boot_ms", (double)hal_millis());
  cJSONUtils_SortObject(root);
  char printed[192] = {};
  if (cJSON_PrintPreallocated(root, printed, (int)sizeof(printed), false)) {
    deb("network suite: cJSON self-test %s", printed);
  } else {
    derr("network suite: cJSON self-test output overflow");
  }
  cJSON_Delete(root);
}

static RamFile *find_file(const char *path) {
  for (size_t i = 0u; i < COUNTOF(s_files); ++i) {
    if (s_files[i].used && strcmp(s_files[i].path, path) == 0) {
      return &s_files[i];
    }
  }
  return nullptr;
}

static hal_status_t ram_file_stat(const char *path,
                                  hal_http_file_info_t *out_info, void *user) {
  (void)user;
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  RamFile *file = find_file(path);
  if (file == nullptr) {
    return HAL_ENOENT;
  }
  memset(out_info, 0, sizeof(*out_info));
  out_info->exists = true;
  out_info->size = file->len;
  out_info->mtime = file->mtime;
  return HAL_OK;
}

static hal_status_t ram_file_read(const char *path, size_t offset, void *buffer,
                                  size_t max_len, size_t *out_len, void *user) {
  (void)user;
  if (buffer == nullptr || out_len == nullptr) {
    return HAL_EINVAL;
  }
  RamFile *file = find_file(path);
  if (file == nullptr) {
    return HAL_ENOENT;
  }
  if (offset >= file->len) {
    *out_len = 0u;
    return HAL_OK;
  }

  size_t len = file->len - offset;
  if (len > max_len) {
    len = max_len;
  }
  memcpy(buffer, file->data + offset, len);
  *out_len = len;
  return HAL_OK;
}

static hal_status_t ram_file_write(const char *path, size_t offset,
                                   const void *data, size_t len, bool final,
                                   void *user) {
  (void) final;
  (void)user;
  if (path == nullptr || (data == nullptr && len > 0u) ||
      offset > sizeof(s_files[0].data) ||
      len > sizeof(s_files[0].data) - offset) {
    return HAL_EINVAL;
  }

  RamFile *file = find_file(path);
  if (file == nullptr) {
    for (size_t i = 0u; i < COUNTOF(s_files); ++i) {
      if (!s_files[i].used) {
        const int written =
            snprintf(s_files[i].path, sizeof(s_files[i].path), "%s", path);
        if (written < 0 || (size_t)written >= sizeof(s_files[i].path)) {
          return HAL_EOVERFLOW;
        }
        file = &s_files[i];
        file->used = true;
        break;
      }
    }
  }
  if (file == nullptr) {
    return HAL_ENOMEM;
  }

  if (len > 0u) {
    memcpy(file->data + offset, data, len);
  }
  file->len = offset + len;
  file->mtime = hal_millis();
  return HAL_OK;
}

static hal_status_t authorize_file_upload(const hal_http_request_t *request,
                                          hal_http_file_upload_t upload,
                                          void *user) {
  (void)upload;
  (void)user;
  const char *token = hal_http_request_get_header(request, "X-Upload-Token");
  return token != nullptr && strcmp(token, NETWORK_SUITE_UPLOAD_TOKEN) == 0
             ? HAL_OK
             : HAL_EAUTH;
}

static void seed_ram_files(void) {
  static const char kIndex[] =
      "<!doctype html><html><body><h1>JaszczurHAL RAM files</h1>"
      "<p><a href=\"/files/hello.txt\">hello.txt</a></p>"
      "<form method=\"post\" action=\"/upload\" "
      "enctype=\"multipart/form-data\"><input type=\"file\" name=\"file\">"
      "<button>upload</button></form></body></html>";
  static const char kHello[] = "hello from the network suite RAM backend\n";

  memset(s_files, 0, sizeof(s_files));
  (void)snprintf(s_files[0].path, sizeof(s_files[0].path), "/www/index.html");
  memcpy(s_files[0].data, kIndex, sizeof(kIndex) - 1u);
  s_files[0].len = sizeof(kIndex) - 1u;
  s_files[0].mtime = 1u;
  s_files[0].used = true;

  (void)snprintf(s_files[1].path, sizeof(s_files[1].path), "/www/hello.txt");
  memcpy(s_files[1].data, kHello, sizeof(kHello) - 1u);
  s_files[1].len = sizeof(kHello) - 1u;
  s_files[1].mtime = 2u;
  s_files[1].used = true;
}

static hal_status_t root_handler(const hal_http_request_t *request,
                                 hal_http_response_t *response, void *user) {
  (void)request;
  (void)user;
  increment_stat(&s_stats.http_requests);

  hal_status_t status =
      hal_http_response_set_content_type(response, "text/html; charset=utf-8");
  if (status != HAL_OK) {
    return status;
  }
  return hal_http_response_write_str(
      response,
      "<!doctype html><html><head><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><title>JaszczurHAL "
      "network suite</title></head><body><h1>JaszczurHAL network suite</h1>"
      "<p><a href=\"/api/status\">status JSON</a> | "
      "<a href=\"/files/index.html\">RAM files</a></p>"
      "<pre id=\"log\">connecting WebSocket...</pre>"
      "<button onclick=\"cmd('status')\">status</button>"
      "<button onclick=\"cmd('echo','hello')\">echo</button><script>"
      "const log=document.getElementById('log');"
      "const ws=new WebSocket('ws://'+location.hostname+':81/ws');"
      "ws.onopen=()=>log.textContent='connected';"
      "ws.onmessage=e=>log.textContent+='\\n'+e.data;"
      "function cmd(c,a){ws.send(JSON.stringify({cmd:c,args:a||{}}));}"
      "</script></body></html>");
}

static hal_status_t status_handler(const hal_http_request_t *request,
                                   hal_http_response_t *response, void *user) {
  (void)request;
  (void)user;
  increment_stat(&s_stats.http_requests);

  cJSON *root = make_status_json("http-get");
  if (root == nullptr) {
    return HAL_ENOMEM;
  }
  char payload[640] = {};
  const bool printed =
      cJSON_PrintPreallocated(root, payload, (int)sizeof(payload), false);
  cJSON_Delete(root);
  if (!printed) {
    return HAL_EOVERFLOW;
  }

  hal_status_t status =
      hal_http_response_set_content_type(response, "application/json");
  if (status != HAL_OK) {
    return status;
  }
  return hal_http_response_write_str(response, payload);
}

static hal_status_t status_command(const hal_net_command_request_t *request,
                                   hal_net_command_response_t *response,
                                   void *user) {
  (void)user;
  increment_stat(&s_stats.command_requests);
  cJSON *root = make_status_json(command_source_name(request->source));
  if (root == nullptr) {
    return HAL_ENOMEM;
  }
  cJSON_AddStringToObject(root, "command", request->command);
  const hal_status_t status =
      hal_net_command_response_write_json(response, root);
  cJSON_Delete(root);
  return status;
}

static const char *
json_text_argument(const hal_net_command_request_t *request) {
  if (cJSON_IsString(request->json_args)) {
    return request->json_args->valuestring;
  }
  if (request->json_args != nullptr) {
    const cJSON *text =
        cJSON_GetObjectItemCaseSensitive(request->json_args, "text");
    if (cJSON_IsString(text)) {
      return text->valuestring;
    }
  }
  return request->args_text;
}

static hal_status_t echo_command(const hal_net_command_request_t *request,
                                 hal_net_command_response_t *response,
                                 void *user) {
  (void)user;
  increment_stat(&s_stats.command_requests);
  const char *text = json_text_argument(request);
  if (text == nullptr) {
    text = "";
  }

  if (request->json_root != nullptr) {
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
      return HAL_ENOMEM;
    }
    cJSON_AddStringToObject(root, "echo", text);
    const hal_status_t status =
        hal_net_command_response_write_json(response, root);
    cJSON_Delete(root);
    return status;
  }

  hal_status_t status = hal_net_command_response_write_str(response, text);
  if (status == HAL_OK) {
    status = hal_net_command_response_write_str(response, "\n");
  }
  return status;
}

static void websocket_connect(hal_websocket_client_t client, void *user) {
  (void)user;
  deb("network suite: WebSocket client %u connected", (unsigned)client);
  (void)hal_websocket_send_text(
      client, "connected; commands: status, echo <text>, or JSON");
}

static void websocket_message(hal_websocket_client_t client,
                              hal_websocket_message_type_t type,
                              const uint8_t *data, size_t len, void *user) {
  (void)user;
  increment_stat(&s_stats.websocket_messages);
  (void)hal_net_commands_handle_websocket_message(client, type, data, len,
                                                  HAL_NET_COMMANDS_FORMAT_AUTO);
}

static void websocket_disconnect(hal_websocket_client_t client,
                                 uint16_t close_code, void *user) {
  (void)user;
  deb("network suite: WebSocket client %u disconnected code=%u",
      (unsigned)client, (unsigned)close_code);
}

static void console_event(hal_net_console_client_t client,
                          hal_net_console_event_t event, void *user) {
  (void)user;
  const char *name = "connected";
  if (event == HAL_NET_CONSOLE_EVENT_AUTHENTICATED) {
    name = "authenticated";
  } else if (event == HAL_NET_CONSOLE_EVENT_DISCONNECT) {
    name = "disconnected";
  }
  deb("network suite: console client %u %s", (unsigned)client, name);
}

static hal_status_t console_line(hal_net_console_client_t client,
                                 const char *line, void *user) {
  (void)user;
  increment_stat(&s_stats.console_lines);

  if (strcmp(line, "help") == 0) {
    return hal_net_console_write_text_to(
        client, "commands: help, status, echo <text>\r\n");
  }

  hal_net_command_response_t response = {};
  (void)hal_net_commands_execute_text(line, &response);
  hal_status_t status =
      hal_net_console_write_to(client, response.body, response.body_len);
  if (status == HAL_OK && (response.body_len == 0u ||
                           response.body[response.body_len - 1u] != '\n')) {
    status = hal_net_console_write_text_to(client, "\r\n");
  }
  return status;
}

static bool require_status(hal_status_t status, const char *operation) {
  if (status == HAL_OK) {
    return true;
  }
  derr("network suite: %s failed: %s", operation, hal_status_to_string(status));
  return false;
}

static bool configure_services(void) {
  seed_ram_files();
  run_cjson_self_test();
  hal_http_server_clear_routes();
  hal_http_files_clear();

  if (!require_status(hal_net_commands_clear(), "clear net commands") ||
      !require_status(
          hal_net_commands_register("status", status_command, nullptr),
          "register status command") ||
      !require_status(hal_net_commands_register("echo", echo_command, nullptr),
                      "register echo command") ||
      !require_status(hal_http_server_route(HAL_HTTP_METHOD_GET, "/",
                                            root_handler, nullptr),
                      "register root route") ||
      !require_status(hal_http_server_route(HAL_HTTP_METHOD_GET, "/api/status",
                                            status_handler, nullptr),
                      "register status route") ||
      !require_status(
          hal_net_commands_register_http_route(
              HAL_NET_COMMANDS_DEFAULT_HTTP_PATH, HAL_NET_COMMANDS_FORMAT_AUTO),
          "register command route")) {
    return false;
  }

  hal_http_files_config_t files = {};
  files.url_prefix = "/files";
  files.fs_root = "/www";
  files.upload_path = "/upload";
  files.enable_upload = true;
  files.stat = ram_file_stat;
  files.read = ram_file_read;
  files.write = ram_file_write;
  files.authorize_upload = authorize_file_upload;
  if (!require_status(hal_http_files_mount(&files), "mount RAM files")) {
    return false;
  }

  hal_websocket_callbacks_t websocket = {};
  websocket.on_connect = websocket_connect;
  websocket.on_message = websocket_message;
  websocket.on_disconnect = websocket_disconnect;
  if (!require_status(hal_websocket_server_set_callbacks(&websocket, nullptr),
                      "set WebSocket callbacks") ||
      !require_status(
          hal_net_console_set_callbacks(console_event, console_line, nullptr),
          "set console callbacks")) {
    return false;
  }

  deb("network suite: configured 7 HTTP routes and shared commands");
  return true;
}

static void connect_wifi(void) {
  if (hal_wifi_is_connected() && hal_wifi_has_local_ip()) {
    set_wifi_ready(true);
    return;
  }
  set_wifi_ready(false);

  const uint32_t now = hal_millis();
  if (s_wifi_attempted &&
      (uint32_t)(now - s_last_wifi_attempt_ms) < kWifiRetryMs) {
    return;
  }
  s_wifi_attempted = true;
  s_last_wifi_attempt_ms = now;

  (void)hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  (void)hal_wifi_set_hostname("jaszczurhal-suite");
  const bool accepted = hal_wifi_begin_station(
      NETWORK_SUITE_WIFI_SSID, NETWORK_SUITE_WIFI_PASSWORD, true);
  deb("network suite: WiFi join %s (%s)", NETWORK_SUITE_WIFI_SSID,
      accepted ? "accepted" : "rejected");
}

static void start_services(void) {
  if (!s_services_configured || !wifi_ready() ||
      (s_http_started && s_websocket_started && s_console_started)) {
    return;
  }

  const uint32_t now = hal_millis();
  if (s_last_service_start_ms != 0u &&
      (uint32_t)(now - s_last_service_start_ms) < kWifiRetryMs) {
    return;
  }
  s_last_service_start_ms = now;

  if (!s_http_started) {
    const hal_status_t status = hal_http_server_start(kHttpPort);
    s_http_started = status == HAL_OK;
    if (!s_http_started) {
      derr("network suite: HTTP start failed: %s",
           hal_status_to_string(status));
    }
  }
  if (!s_websocket_started) {
    const hal_status_t status =
        hal_websocket_server_start(kWebSocketPort, "/ws");
    s_websocket_started = status == HAL_OK;
    if (!s_websocket_started) {
      derr("network suite: WebSocket start failed: %s",
           hal_status_to_string(status));
    }
  }
  if (!s_console_started) {
    const hal_status_t status = hal_net_console_start(
        HAL_NET_CONSOLE_DEFAULT_PORT, NETWORK_SUITE_CONSOLE_PASSWORD);
    s_console_started = status == HAL_OK;
    if (!s_console_started) {
      derr("network suite: console start failed: %s",
           hal_status_to_string(status));
    }
  }

  if (s_http_started && s_websocket_started && s_console_started &&
      !s_services_announced) {
    char ip[32] = {};
    (void)hal_wifi_get_local_ip(ip, sizeof(ip));
    deb("network suite: HTTP http://%s/ WS ws://%s:%u/ws console %s:%u", ip, ip,
        (unsigned)kWebSocketPort, ip, (unsigned)HAL_NET_CONSOLE_DEFAULT_PORT);
    s_services_announced = true;
  }
}

static void poll_services(void) {
  if (s_http_started) {
    hal_http_server_poll();
  }
  if (s_websocket_started) {
    hal_websocket_server_poll();
  }
  if (s_console_started) {
    hal_net_console_poll();
  }
}

static void broadcast_status(void) {
  if (!s_websocket_started || hal_websocket_client_count() == 0u) {
    return;
  }
  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_last_broadcast_ms) < 1000u) {
    return;
  }
  s_last_broadcast_ms = now;

  char payload[128] = {};
  const SuiteStats stats = stats_snapshot();
  (void)snprintf(payload, sizeof(payload),
                 "uptime=%lu heap=%lu app=[%lu,%lu] commands=%lu",
                 (unsigned long)now, (unsigned long)hal_get_free_heap(),
                 (unsigned long)stats.app_task0_ticks,
                 (unsigned long)stats.app_task1_ticks,
                 (unsigned long)stats.command_requests);
  (void)hal_websocket_broadcast_text(payload, nullptr);
}

static void print_status(void) {
  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_last_status_ms) < kStatusIntervalMs) {
    return;
  }
  s_last_status_ms = now;

  if (!wifi_ready()) {
    deb("network suite: WiFi disconnected status=%d heap=%lu",
        hal_wifi_status(), (unsigned long)hal_get_free_heap());
    return;
  }

  char ip[32] = {};
  char mac[32] = {};
  (void)hal_wifi_get_local_ip(ip, sizeof(ip));
  (void)hal_wifi_get_mac(mac, sizeof(mac));
  const SuiteStats stats = stats_snapshot();
  deb("network suite: ip=%s mac=%s rssi=%ld bars=%d heap=%lu "
      "services=[%u,%u,%u] bsd=[%lu,%lu,%lu,%lu] clients=[%lu,%lu]",
      ip, mac, (long)hal_wifi_rssi(), hal_wifi_get_strength(),
      (unsigned long)hal_get_free_heap(), s_http_started ? 1u : 0u,
      s_websocket_started ? 1u : 0u, s_console_started ? 1u : 0u,
      (unsigned long)stats.bsd_tcp_server_sessions,
      (unsigned long)stats.bsd_udp_server_datagrams,
      (unsigned long)stats.bsd_tcp_client_runs,
      (unsigned long)stats.bsd_udp_client_runs,
      (unsigned long)stats.http_client_runs,
      (unsigned long)stats.https_client_runs);
}

static bool socket_would_block(int error) {
  if (error == EAGAIN) {
    return true;
  }
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
  if (error == EWOULDBLOCK) {
    return true;
  }
#endif
  return false;
}

static void set_socket_timeout(int fd, uint32_t timeout_ms) {
  struct timeval timeout = {};
  timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(timeout_ms / 1000u);
  timeout.tv_usec =
      static_cast<decltype(timeout.tv_usec)>((timeout_ms % 1000u) * 1000u);
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static bool set_nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int open_tcp_server(void) {
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    derr("network suite: BSD TCP socket failed errno=%d", errno);
    return -1;
  }

  const int reuse = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  struct sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons((uint16_t)NETWORK_SUITE_BSD_TCP_PORT);
  if (bind(fd, reinterpret_cast<struct sockaddr *>(&address),
           (socklen_t)sizeof(address)) < 0 ||
      listen(fd, 2) < 0 || !set_nonblocking(fd)) {
    derr("network suite: BSD TCP bind/listen failed errno=%d", errno);
    close(fd);
    return -1;
  }
  deb("network suite: BSD TCP echo port %u",
      (unsigned)NETWORK_SUITE_BSD_TCP_PORT);
  return fd;
}

static int open_udp_server(void) {
  const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    derr("network suite: BSD UDP socket failed errno=%d", errno);
    return -1;
  }

  const int reuse = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  struct sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons((uint16_t)NETWORK_SUITE_BSD_UDP_PORT);
  if (bind(fd, reinterpret_cast<struct sockaddr *>(&address),
           (socklen_t)sizeof(address)) < 0 ||
      !set_nonblocking(fd)) {
    derr("network suite: BSD UDP bind failed errno=%d", errno);
    close(fd);
    return -1;
  }
  deb("network suite: BSD UDP echo port %u",
      (unsigned)NETWORK_SUITE_BSD_UDP_PORT);
  return fd;
}

static void service_tcp_server(int server_fd) {
  struct sockaddr_in peer = {};
  socklen_t peer_len = (socklen_t)sizeof(peer);
  const int client_fd =
      accept(server_fd, reinterpret_cast<struct sockaddr *>(&peer), &peer_len);
  if (client_fd < 0) {
    if (!socket_would_block(errno)) {
      derr("network suite: BSD accept failed errno=%d", errno);
    }
    return;
  }

  set_socket_timeout(client_fd, kSocketTimeoutMs);
  char buffer[128] = {};
  const ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1u, 0);
  if (received > 0) {
    buffer[received] = '\0';
    const char reply[] = "JaszczurHAL BSD TCP echo: ";
    (void)send(client_fd, reply, sizeof(reply) - 1u, 0);
    (void)send(client_fd, buffer, (size_t)received, 0);
    increment_stat(&s_stats.bsd_tcp_server_sessions);
  }
  close(client_fd);
}

static void service_udp_server(int udp_fd) {
  struct sockaddr_in peer = {};
  socklen_t peer_len = (socklen_t)sizeof(peer);
  char buffer[128] = {};
  const ssize_t received =
      recvfrom(udp_fd, buffer, sizeof(buffer) - 1u, MSG_DONTWAIT,
               reinterpret_cast<struct sockaddr *>(&peer), &peer_len);
  if (received < 0) {
    if (!socket_would_block(errno)) {
      derr("network suite: BSD recvfrom failed errno=%d", errno);
    }
    return;
  }
  if (received > 0) {
    buffer[received] = '\0';
    (void)sendto(udp_fd, buffer, (size_t)received, 0,
                 reinterpret_cast<struct sockaddr *>(&peer), peer_len);
    increment_stat(&s_stats.bsd_udp_server_datagrams);
  }
}

static bool resolve_remote(uint16_t port, int socket_type,
                           struct sockaddr_in *out) {
  char service[8] = {};
  (void)snprintf(service, sizeof(service), "%u", (unsigned)port);
  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = socket_type;

  struct addrinfo *resolved = nullptr;
  const int result =
      getaddrinfo(NETWORK_SUITE_REMOTE_HOST, service, &hints, &resolved);
  if (result != 0 || resolved == nullptr ||
      resolved->ai_addrlen < (socklen_t)sizeof(*out)) {
    derr("network suite: getaddrinfo(%s:%s) failed: %s",
         NETWORK_SUITE_REMOTE_HOST, service, gai_strerror(result));
    if (resolved != nullptr) {
      freeaddrinfo(resolved);
    }
    return false;
  }
  memcpy(out, resolved->ai_addr, sizeof(*out));
  freeaddrinfo(resolved);
  return true;
}

static void run_tcp_client_probe(void) {
  struct sockaddr_in remote = {};
  if (!resolve_remote((uint16_t)NETWORK_SUITE_BSD_TCP_PORT, SOCK_STREAM,
                      &remote)) {
    return;
  }
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    derr("network suite: BSD TCP client socket failed errno=%d", errno);
    return;
  }
  set_socket_timeout(fd, kSocketTimeoutMs);
  if (connect(fd, reinterpret_cast<struct sockaddr *>(&remote),
              (socklen_t)sizeof(remote)) < 0) {
    derr("network suite: BSD TCP connect %s:%u failed errno=%d",
         NETWORK_SUITE_REMOTE_HOST, (unsigned)NETWORK_SUITE_BSD_TCP_PORT,
         errno);
    close(fd);
    return;
  }

  const char request[] = "hello from the consolidated BSD TCP client";
  (void)send(fd, request, sizeof(request) - 1u, 0);
  char response[128] = {};
  const ssize_t received = recv(fd, response, sizeof(response) - 1u, 0);
  if (received > 0) {
    response[received] = '\0';
    deb("network suite: BSD TCP client RX: %s", response);
  }
  increment_stat(&s_stats.bsd_tcp_client_runs);
  (void)shutdown(fd, SHUT_RDWR);
  close(fd);
}

static void run_udp_client_probe(void) {
  struct sockaddr_in remote = {};
  if (!resolve_remote((uint16_t)NETWORK_SUITE_BSD_UDP_PORT, SOCK_DGRAM,
                      &remote)) {
    return;
  }
  const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    derr("network suite: BSD UDP client socket failed errno=%d", errno);
    return;
  }
  set_socket_timeout(fd, kSocketTimeoutMs);

  const char request[] = "hello from the consolidated BSD UDP client";
  if (sendto(fd, request, sizeof(request) - 1u, 0,
             reinterpret_cast<struct sockaddr *>(&remote),
             (socklen_t)sizeof(remote)) >= 0) {
    struct sockaddr_in peer = {};
    socklen_t peer_len = (socklen_t)sizeof(peer);
    char response[128] = {};
    const ssize_t received =
        recvfrom(fd, response, sizeof(response) - 1u, 0,
                 reinterpret_cast<struct sockaddr *>(&peer), &peer_len);
    if (received > 0) {
      response[received] = '\0';
      deb("network suite: BSD UDP client RX: %s", response);
    }
    increment_stat(&s_stats.bsd_udp_client_runs);
  } else {
    derr("network suite: BSD UDP send failed errno=%d", errno);
  }
  close(fd);
}

static void bsd_worker(void *arg) {
  (void)arg;
  int tcp_server = -1;
  int udp_server = -1;
  uint32_t last_client_run_ms = 0u;

  for (;;) {
    if (!wifi_ready()) {
      hal_delay_ms(100u);
      continue;
    }
    if (tcp_server < 0) {
      tcp_server = open_tcp_server();
    }
    if (udp_server < 0) {
      udp_server = open_udp_server();
    }
    if (tcp_server >= 0) {
      service_tcp_server(tcp_server);
    }
    if (udp_server >= 0) {
      service_udp_server(udp_server);
    }

    const uint32_t now = hal_millis();
    if (last_client_run_ms == 0u ||
        (uint32_t)(now - last_client_run_ms) >= kBsdClientIntervalMs) {
      last_client_run_ms = now;
      run_tcp_client_probe();
      run_udp_client_probe();
    }
    hal_delay_ms(20u);
  }
}

static hal_status_t
perform_http_request(hal_http_client_transport_t transport, uint16_t port,
                     const hal_tls_security_config_t *security) {
  hal_http_client_request_t request = {};
  hal_status_t status = hal_http_client_request_init(&request);
  if (status != HAL_OK) {
    return status;
  }
  request.transport = transport;
  request.host = NETWORK_SUITE_HTTP_HOST;
  request.port = port;
  request.path = "/";
  request.timeout_ms = kHttpTimeoutMs;
  request.tls_security = security;

  uint8_t body[512] = {};
  hal_http_client_response_t response = {};
  status = hal_http_client_perform_ex(&request, body, sizeof(body), &response);
  deb("network suite: %s client status=%s HTTP=%u body=%u",
      transport == HAL_HTTP_CLIENT_TRANSPORT_TLS ? "HTTPS" : "HTTP",
      hal_status_to_string(status), (unsigned)response.status_code,
      (unsigned)response.body_length);
  return status;
}

static void http_client_worker(void *arg) {
  (void)arg;
  bool http_complete = false;
  bool https_complete = false;
#ifdef HTTP_EXAMPLE_CA_AVAILABLE
  bool ntp_requested = false;
  static hal_tls_trust_anchor_storage_t anchor_storage;
#endif

  for (;;) {
    if (!wifi_ready()) {
      hal_delay_ms(100u);
      continue;
    }

    if (!http_complete) {
      http_complete = true;
      (void)perform_http_request(HAL_HTTP_CLIENT_TRANSPORT_PLAINTEXT, 80u,
                                 nullptr);
      increment_stat(&s_stats.http_client_runs);
    }

#ifdef HTTP_EXAMPLE_CA_AVAILABLE
    if (!ntp_requested) {
      ntp_requested = hal_time_sync_ntp("pool.ntp.org", "time.google.com");
    }
    if (!https_complete && hal_time_is_synced(HAL_TLS_MIN_VALID_UNIX_TIME)) {
      https_complete = true;
      const hal_status_t anchor_status = hal_tls_trust_anchor_from_der_ex(
          http_example_ca_der, http_example_ca_der_len, &anchor_storage);
      if (anchor_status != HAL_OK) {
        derr("network suite: HTTPS CA decode failed: %s",
             hal_status_to_string(anchor_status));
      } else {
        hal_tls_security_config_t security = {};
        security.trust_anchors = &anchor_storage.anchor;
        security.trust_anchor_count = 1u;
        security.get_time = hal_tls_default_time;
        security.get_entropy = hal_tls_default_entropy;
        (void)perform_http_request(HAL_HTTP_CLIENT_TRANSPORT_TLS, 443u,
                                   &security);
        increment_stat(&s_stats.https_client_runs);
      }
    }
#else
    if (!https_complete) {
      https_complete = true;
      deb("network suite: HTTPS not executed: define "
          "HTTP_EXAMPLE_CA_AVAILABLE and provide ca_certificate.h; TLS is "
          "compiled but verified HTTPS requires an explicit trust anchor");
    }
#endif
    hal_delay_ms(1000u);
  }
}

} // namespace

extern "C" void app_start(void) {
  debugInit();
  hal_deb_set_prefix("18_freertos_suite");
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);

  s_stats_mutex = xSemaphoreCreateMutex();
  if (s_stats_mutex == nullptr) {
    derr("network suite: stats mutex allocation failed");
    return;
  }
  s_services_configured = configure_services();

  BaseType_t created = xTaskCreate(bsd_worker, "jh_bsd", kBsdWorkerStackWords,
                                   nullptr, tskIDLE_PRIORITY + 1u, nullptr);
  if (created != pdPASS) {
    derr("network suite: BSD worker allocation failed");
  }
  created =
      xTaskCreate(http_client_worker, "jh_http_cli", kHttpWorkerStackWords,
                  nullptr, tskIDLE_PRIORITY + 1u, nullptr);
  if (created != pdPASS) {
    derr("network suite: HTTP/HTTPS worker allocation failed");
  }

  deb("network suite: started FreeRTOS network workers");
}

extern "C" void app_task0(void) {
  if (s_stats_mutex == nullptr) {
    hal_delay_ms(100u);
    return;
  }
  increment_stat(&s_stats.app_task0_ticks);
  connect_wifi();
  start_services();
  poll_services();
  broadcast_status();
  print_status();
  hal_debug_loop();
  hal_delay_ms(5u);
}

extern "C" void app_task1(void) {
  if (s_stats_mutex == nullptr) {
    hal_delay_ms(100u);
    return;
  }
  increment_stat(&s_stats.app_task1_ticks);
  const SuiteStats stats = stats_snapshot();
  hal_gpio_write(HAL_LED_BUILTIN,
                 ((stats.app_task1_ticks + stats.command_requests) & 1u) != 0u);
  hal_delay_ms(25u);
}
