#include "hal/hal_http_server.h"
#include "hal/impl/.mock/hal_mock.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace {

hal_status_t sink_handler(const hal_http_request_t *, hal_http_response_t *,
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
  endpoint.addr[3] = 1u;
  endpoint.port = 50000u;
  return endpoint;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  hal_http_server_stop();
  hal_http_server_clear_routes();
  hal_mock_tcp_reset();
  hal_mock_set_millis(0u);
  (void)hal_http_server_route_prefix(HAL_HTTP_METHOD_GET, "/", sink_handler,
                                     nullptr);
  (void)hal_http_server_route_prefix(HAL_HTTP_METHOD_POST, "/", sink_handler,
                                     nullptr);
  if (hal_http_server_start(18080u) != HAL_OK) {
    return 0;
  }
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(18080u);
  hal_net_endpoint_t remote = remote_endpoint();
  if (listener != nullptr &&
      hal_mock_tcp_listener_inject_client(listener, &remote)) {
    hal_http_server_poll();
    hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
    const size_t bounded =
        std::min(size, (size_t)HAL_HTTP_SERVER_REQUEST_BUFFER_SIZE + 64u);
    if (socket != nullptr && bounded > 0u) {
      hal_mock_tcp_inject_rx(socket, data, (uint16_t)bounded);
      hal_http_server_poll();
    }
  }
  hal_http_server_stop();
  hal_http_server_clear_routes();
  return 0;
}
