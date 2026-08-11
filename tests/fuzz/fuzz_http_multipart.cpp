#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/http/hal_http_files.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

hal_status_t file_stat(const char *, hal_http_file_info_t *, void *) {
  return HAL_ENOENT;
}

hal_status_t file_read(const char *, size_t, void *, size_t, size_t *out_len,
                       void *) {
  *out_len = 0u;
  return HAL_OK;
}

hal_status_t file_write(const char *, size_t, const void *, size_t, bool,
                        void *) {
  return HAL_OK;
}

hal_status_t authorize(const hal_http_request_t *, hal_http_file_upload_t,
                       void *) {
  return HAL_OK;
}

hal_net_endpoint_t remote_endpoint(void) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.addr[0] = 192u;
  endpoint.addr[1] = 0u;
  endpoint.addr[2] = 2u;
  endpoint.addr[3] = 3u;
  endpoint.port = 50002u;
  return endpoint;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  hal_http_server_stop();
  hal_http_server_clear_routes();
  hal_http_files_clear();
  hal_mock_tcp_reset();
  hal_mock_set_millis(0u);
  hal_http_files_config_t config = {};
  config.url_prefix = "/files";
  config.fs_root = "/fuzz";
  config.upload_path = "/upload";
  config.enable_upload = true;
  config.stat = file_stat;
  config.read = file_read;
  config.write = file_write;
  config.authorize_upload = authorize;
  if (hal_http_files_mount(&config) != HAL_OK ||
      hal_http_server_start(18082u) != HAL_OK) {
    return 0;
  }

  const size_t body_len =
      std::min(size, (size_t)HAL_HTTP_SERVER_REQUEST_BUFFER_SIZE / 2u);
  uint8_t request[HAL_HTTP_SERVER_REQUEST_BUFFER_SIZE + 64u];
  const int header_len = std::snprintf(
      reinterpret_cast<char *>(request), sizeof(request),
      "POST /upload HTTP/1.1\r\nHost: fuzz\r\nContent-Type: "
      "multipart/form-data; boundary=fuzz\r\nContent-Length: %zu\r\n\r\n",
      body_len);
  if (header_len <= 0 || (size_t)header_len + body_len >= sizeof(request)) {
    return 0;
  }
  if (body_len > 0u) {
    std::memcpy(request + header_len, data, body_len);
  }
  const size_t request_len = (size_t)header_len + body_len;
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(18082u);
  hal_net_endpoint_t remote = remote_endpoint();
  if (listener != nullptr &&
      hal_mock_tcp_listener_inject_client(listener, &remote)) {
    hal_http_server_poll();
    hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
    if (socket != nullptr) {
      hal_mock_tcp_inject_rx(socket, request, (uint16_t)request_len);
      hal_http_server_poll();
    }
  }
  hal_http_server_stop();
  hal_http_server_clear_routes();
  hal_http_files_clear();
  return 0;
}
