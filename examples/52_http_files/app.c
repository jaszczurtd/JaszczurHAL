#include <hal/hal_app.h>
#include <hal/hal_http_files.h>
#include <hal/hal_http_server.h>
#include <hal/hal_system.h>
#include <hal/hal_wifi.h>
#include <stdio.h>
#include <string.h>
#include <tools_c.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";

typedef struct {
  bool used;
  char path[96];
  uint8_t data[512];
  size_t len;
  uint32_t mtime;
} ram_file_t;

static ram_file_t files[4];
static uint32_t last_wifi_check_ms = 0;
static uint32_t last_status_ms = 0;
static bool http_started = false;

static ram_file_t *find_file(const char *path) {
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
    if (files[i].used && strcmp(files[i].path, path) == 0) {
      return &files[i];
    }
  }
  return NULL;
}

static hal_status_t ram_stat(const char *path, hal_http_file_info_t *out,
                             void *user) {
  (void)user;
  ram_file_t *file = find_file(path);
  if (!file) {
    return HAL_ENOENT;
  }
  memset(out, 0, sizeof(*out));
  out->exists = true;
  out->size = file->len;
  out->mtime = file->mtime;
  return HAL_OK;
}

static hal_status_t ram_read(const char *path, size_t offset, void *buffer,
                             size_t max_len, size_t *out_len, void *user) {
  (void)user;
  ram_file_t *file = find_file(path);
  if (!file || !buffer || !out_len) {
    return HAL_EINVAL;
  }
  if (offset >= file->len) {
    *out_len = 0;
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

static hal_status_t ram_write(const char *path, size_t offset, const void *data,
                              size_t len, bool final, void *user) {
  (void) final;
  (void)user;
  if (!path || (!data && len > 0) || offset + len > sizeof(files[0].data)) {
    return HAL_EINVAL;
  }

  ram_file_t *file = find_file(path);
  if (!file) {
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
      if (!files[i].used) {
        file = &files[i];
        memset(file, 0, sizeof(*file));
        snprintf(file->path, sizeof(file->path), "%s", path);
        file->used = true;
        break;
      }
    }
  }
  if (!file) {
    return HAL_ENOMEM;
  }

  if (len > 0) {
    memcpy(file->data + offset, data, len);
  }
  file->len = offset + len;
  file->mtime = hal_millis();
  return HAL_OK;
}

static void seed_files(void) {
  const char index[] =
      "<!doctype html><html><head><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><title>JaszczurHAL "
      "Files</title></head><body><h1>JaszczurHAL Files</h1>"
      "<p><a href=\"/files/hello.txt\">hello.txt</a></p>"
      "<form method=\"post\" action=\"/upload\" "
      "enctype=\"multipart/form-data\">"
      "<input type=\"file\" name=\"file\"><button>upload</button></form>"
      "</body></html>";
  const char hello[] = "hello from RAM-backed HTTP files\n";

  memset(files, 0, sizeof(files));
  snprintf(files[0].path, sizeof(files[0].path), "/www/index.html");
  memcpy(files[0].data, index, sizeof(index) - 1u);
  files[0].len = sizeof(index) - 1u;
  files[0].mtime = 1u;
  files[0].used = true;

  snprintf(files[1].path, sizeof(files[1].path), "/www/hello.txt");
  memcpy(files[1].data, hello, sizeof(hello) - 1u);
  files[1].len = sizeof(hello) - 1u;
  files[1].mtime = 2u;
  files[1].used = true;
}

static hal_status_t root_handler(const hal_http_request_t *request,
                                 hal_http_response_t *response, void *user) {
  (void)request;
  (void)user;
  hal_http_response_set_status(response, 302u, "Found");
  hal_http_response_set_header(response, "Location", "/files/index.html");
  return hal_http_response_write_str(response, "Found\n");
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
  hal_wifi_set_hostname("jaszczurhal-files");
  hal_wifi_begin_station(WIFI_SSID, WIFI_PASSWORD, true);
}

static void start_http_when_ready(void) {
  if (http_started || !hal_wifi_is_connected()) {
    return;
  }

  hal_http_files_config_t cfg = {0};
  cfg.url_prefix = "/files";
  cfg.fs_root = "/www";
  cfg.upload_path = "/upload";
  cfg.enable_upload = true;
  cfg.stat = ram_stat;
  cfg.read = ram_read;
  cfg.write = ram_write;

  hal_http_server_route(HAL_HTTP_METHOD_GET, "/", root_handler, NULL);
  hal_status_t status = hal_http_files_mount(&cfg);
  if (status == HAL_OK) {
    status = hal_http_server_start(80u);
  }
  http_started = status == HAL_OK;

  if (http_started) {
    char ip[32] = {0};
    hal_wifi_get_local_ip(ip, sizeof(ip));
    deb("HTTP files: http://%s/", ip);
  } else {
    derr("HTTP files: failed, status=%s", hal_status_to_string(status));
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
  deb("HTTP files: running=%d files=%u", hal_http_server_is_running() ? 1 : 0,
      (unsigned)(sizeof(files) / sizeof(files[0])));
}

void app_start(void) {
  debugInit();
  seed_files();
}

void app_task0(void) {
  connect_wifi();
  start_http_when_ready();
  hal_http_server_poll();
  print_status();
}
