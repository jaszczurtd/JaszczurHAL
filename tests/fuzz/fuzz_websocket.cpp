#include "hal/hal_websocket.h"
#include "hal/impl/.mock/hal_mock.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

uint8_t s_actions;

void on_message(hal_websocket_client_t client, hal_websocket_message_type_t,
                const uint8_t *, size_t, void *) {
  if ((s_actions & 1u) != 0u) {
    (void)hal_websocket_close(client, 1000u);
  }
  if ((s_actions & 2u) != 0u) {
    hal_websocket_server_stop();
  }
}

hal_net_endpoint_t remote_endpoint(void) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.addr[0] = 192u;
  endpoint.addr[1] = 0u;
  endpoint.addr[2] = 2u;
  endpoint.addr[3] = 2u;
  endpoint.port = 50001u;
  return endpoint;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static const char handshake[] =
      "GET /ws HTTP/1.1\r\nHost: fuzz\r\nUpgrade: websocket\r\n"
      "Connection: Upgrade\r\nSec-WebSocket-Key: "
      "dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
  hal_websocket_server_stop();
  (void)hal_websocket_server_set_callbacks(nullptr, nullptr);
  hal_mock_tcp_reset();
  hal_mock_set_millis(0u);
  s_actions = size > 0u ? data[0] : 0u;
  hal_websocket_callbacks_t callbacks = {};
  callbacks.on_message = on_message;
  (void)hal_websocket_server_set_callbacks(&callbacks, nullptr);
  if (hal_websocket_server_start(18081u, "/ws") != HAL_OK) {
    return 0;
  }
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(18081u);
  hal_net_endpoint_t remote = remote_endpoint();
  if (listener != nullptr &&
      hal_mock_tcp_listener_inject_client(listener, &remote)) {
    hal_websocket_server_poll();
    hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
    if (socket != nullptr) {
      hal_mock_tcp_inject_rx(socket,
                             reinterpret_cast<const uint8_t *>(handshake),
                             (uint16_t)(sizeof(handshake) - 1u));
      hal_websocket_server_poll();
      const size_t bounded =
          std::min(size, (size_t)HAL_WEBSOCKET_FRAME_BUFFER_SIZE + 64u);
      if (bounded > 0u) {
        hal_mock_tcp_inject_rx(socket, data, (uint16_t)bounded);
        hal_websocket_server_poll();
      }
    }
  }
  hal_websocket_server_stop();
  (void)hal_websocket_server_set_callbacks(nullptr, nullptr);
  return 0;
}
