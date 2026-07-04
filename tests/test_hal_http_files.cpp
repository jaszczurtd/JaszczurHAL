#include "hal/hal_http_files.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  bool used;
  char path[80];
  uint8_t data[256];
  size_t len;
  uint32_t mtime;
} mem_file_t;

static mem_file_t s_files[4];

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_tcp_reset();
  hal_http_server_stop();
  hal_http_server_clear_routes();
  hal_http_files_clear();
  memset(s_files, 0, sizeof(s_files));

  snprintf(s_files[0].path, sizeof(s_files[0].path), "/www/index.html");
  const char html[] = "<h1>hello</h1>";
  memcpy(s_files[0].data, html, sizeof(html) - 1u);
  s_files[0].len = sizeof(html) - 1u;
  s_files[0].mtime = 1234u;
  s_files[0].used = true;
}

void tearDown(void) {
  hal_http_server_stop();
  hal_http_server_clear_routes();
  hal_http_files_clear();
}

static mem_file_t *find_file(const char *path) {
  for (size_t i = 0u; i < sizeof(s_files) / sizeof(s_files[0]); ++i) {
    if (s_files[i].used && strcmp(s_files[i].path, path) == 0) {
      return &s_files[i];
    }
  }
  return NULL;
}

static hal_status_t mem_stat(const char *path, hal_http_file_info_t *out,
                             void *user) {
  (void)user;
  mem_file_t *file = find_file(path);
  if (!file) {
    return HAL_ENOENT;
  }
  memset(out, 0, sizeof(*out));
  out->exists = true;
  out->size = file->len;
  out->mtime = file->mtime;
  return HAL_OK;
}

static hal_status_t mem_read(const char *path, size_t offset, void *buffer,
                             size_t max_len, size_t *out_len, void *user) {
  (void)user;
  mem_file_t *file = find_file(path);
  if (!file || !buffer || !out_len) {
    return HAL_EINVAL;
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

static hal_status_t mem_write(const char *path, size_t offset, const void *data,
                              size_t len, bool final, void *user) {
  (void) final;
  (void)user;
  if (!path || (!data && len > 0u) || offset + len > sizeof(s_files[0].data)) {
    return HAL_EINVAL;
  }
  mem_file_t *file = find_file(path);
  if (!file) {
    for (size_t i = 0u; i < sizeof(s_files) / sizeof(s_files[0]); ++i) {
      if (!s_files[i].used) {
        file = &s_files[i];
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
  if (len > 0u) {
    memcpy(file->data + offset, data, len);
  }
  file->len = offset + len;
  file->mtime++;
  return HAL_OK;
}

static hal_net_endpoint_t make_endpoint(uint8_t a, uint8_t b, uint8_t c,
                                        uint8_t d, uint16_t port) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr[0] = a;
  endpoint.addr[1] = b;
  endpoint.addr[2] = c;
  endpoint.addr[3] = d;
  endpoint.port = port;
  return endpoint;
}

static void mount_files(void) {
  hal_http_files_config_t cfg = {};
  cfg.url_prefix = "/fs";
  cfg.fs_root = "/www";
  cfg.upload_path = "/upload";
  cfg.enable_upload = true;
  cfg.stat = mem_stat;
  cfg.read = mem_read;
  cfg.write = mem_write;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_http_files_mount(&cfg));
}

static hal_tcp_socket_t send_request(uint16_t port, const char *request) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_http_server_start(port));
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(port);
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t remote = make_endpoint(192u, 168u, 1u, 70u, 53000u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
  hal_http_server_poll();

  hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
  TEST_ASSERT_NOT_NULL(socket);
  hal_mock_tcp_inject_rx(socket, (const uint8_t *)request,
                         (uint16_t)strlen(request));
  hal_http_server_poll();
  return socket;
}

static void socket_text(hal_tcp_socket_t socket, char *out, size_t out_size) {
  uint16_t len = hal_mock_tcp_get_last_tx_len(socket);
  TEST_ASSERT_LESS_THAN(out_size, len);
  memcpy(out, hal_mock_tcp_get_last_tx_payload(socket), len);
  out[len] = '\0';
}

static void assert_response_contains(hal_tcp_socket_t socket,
                                     const char *needle) {
  char text[768];
  socket_text(socket, text, sizeof(text));
  TEST_ASSERT_NOT_NULL(strstr(text, needle));
}

void test_get_file_serves_body_content_type_and_etag(void) {
  mount_files();

  hal_tcp_socket_t socket =
      send_request(8110u, "GET /fs/index.html HTTP/1.1\r\nHost: unit\r\n\r\n");

  assert_response_contains(socket, "HTTP/1.1 200 OK\r\n");
  assert_response_contains(socket,
                           "Content-Type: text/html; charset=utf-8\r\n");
  assert_response_contains(socket, "ETag: W/\"");
  assert_response_contains(socket, "\r\n\r\n<h1>hello</h1>");
}

void test_if_none_match_returns_not_modified(void) {
  mount_files();
  hal_http_file_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, mem_stat("/www/index.html", &info, NULL));
  char etag[HAL_HTTP_FILES_ETAG_MAX];
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_http_files_make_etag("/www/index.html", &info, etag, sizeof(etag)));

  char request[256];
  int written = snprintf(request, sizeof(request),
                         "GET /fs/index.html HTTP/1.1\r\nHost: unit\r\n"
                         "If-None-Match: %s\r\n\r\n",
                         etag);
  TEST_ASSERT_GREATER_THAN(0, written);
  TEST_ASSERT_LESS_THAN((int)sizeof(request), written);

  hal_tcp_socket_t socket = send_request(8111u, request);
  char text[768];
  socket_text(socket, text, sizeof(text));
  TEST_ASSERT_NOT_NULL(strstr(text, "HTTP/1.1 304 Not Modified\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(text, "Content-Length: 0\r\n"));
  TEST_ASSERT_NULL(strstr(text, "<h1>hello</h1>"));
}

void test_raw_put_writes_file_under_mounted_root(void) {
  mount_files();
  const char request[] =
      "PUT /fs/raw.txt HTTP/1.1\r\nHost: unit\r\nContent-Length: 8\r\n\r\n"
      "raw body";

  hal_tcp_socket_t socket = send_request(8112u, request);

  assert_response_contains(socket, "HTTP/1.1 201 Created\r\n");
  mem_file_t *file = find_file("/www/raw.txt");
  TEST_ASSERT_NOT_NULL(file);
  TEST_ASSERT_EQUAL_UINT(8u, file->len);
  TEST_ASSERT_EQUAL_MEMORY("raw body", file->data, 8u);
}

void test_multipart_upload_writes_file_part(void) {
  mount_files();
  const char body[] =
      "--AaB03x\r\n"
      "Content-Disposition: form-data; name=\"path\"\r\n\r\n"
      "logs\r\n"
      "--AaB03x\r\n"
      "Content-Disposition: form-data; name=\"file\"; filename=\"new.txt\"\r\n"
      "Content-Type: text/plain\r\n\r\n"
      "hello upload\r\n"
      "--AaB03x--\r\n";

  char request[512];
  int written =
      snprintf(request, sizeof(request),
               "POST /upload HTTP/1.1\r\nHost: unit\r\n"
               "Content-Type: multipart/form-data; boundary=AaB03x\r\n"
               "Content-Length: %u\r\n\r\n%s",
               (unsigned)strlen(body), body);
  TEST_ASSERT_GREATER_THAN(0, written);
  TEST_ASSERT_LESS_THAN((int)sizeof(request), written);

  hal_tcp_socket_t socket = send_request(8113u, request);

  assert_response_contains(socket, "HTTP/1.1 201 Created\r\n");
  mem_file_t *file = find_file("/www/logs/new.txt");
  TEST_ASSERT_NOT_NULL(file);
  TEST_ASSERT_EQUAL_UINT(strlen("hello upload"), file->len);
  TEST_ASSERT_EQUAL_MEMORY("hello upload", file->data, strlen("hello upload"));
}

void test_path_traversal_is_rejected(void) {
  mount_files();

  hal_tcp_socket_t socket = send_request(
      8114u, "GET /fs/../secret.txt HTTP/1.1\r\nHost: unit\r\n\r\n");

  assert_response_contains(socket, "HTTP/1.1 400 Bad Request\r\n");
}

void test_api_rejects_invalid_configuration(void) {
  hal_http_files_config_t cfg = {};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_http_files_mount(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_http_files_mount(&cfg));
  cfg.url_prefix = "/fs";
  cfg.stat = mem_stat;
  cfg.read = mem_read;
  cfg.enable_upload = true;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_http_files_mount(&cfg));
  TEST_ASSERT_EQUAL_STRING("application/json",
                           hal_http_files_content_type_for_path("x.json"));
  TEST_ASSERT_EQUAL_STRING("application/octet-stream",
                           hal_http_files_content_type_for_path("x.unknown"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_get_file_serves_body_content_type_and_etag);
  RUN_TEST(test_if_none_match_returns_not_modified);
  RUN_TEST(test_raw_put_writes_file_under_mounted_root);
  RUN_TEST(test_multipart_upload_writes_file_part);
  RUN_TEST(test_path_traversal_is_rejected);
  RUN_TEST(test_api_rejects_invalid_configuration);
  return UNITY_END();
}
