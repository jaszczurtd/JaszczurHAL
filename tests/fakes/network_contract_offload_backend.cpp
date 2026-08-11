#include "network_contract_control.h"

#include "hal/network/jh_network_backend.h"

#include <string.h>

namespace {

constexpr size_t kPayloadCapacity = 512u;

struct Socket {
  bool in_use;
  bool connected;
  bool bound;
  hal_net_endpoint_t endpoint;
  uint8_t rx[kPayloadCapacity];
  size_t rx_length;
  size_t rx_position;
  uint8_t tx[kPayloadCapacity];
  size_t tx_length;
};

struct Listener {
  bool in_use;
  bool bound;
  bool listening;
  hal_net_endpoint_t local;
};

Socket s_tcp[2] = {};
Socket s_udp[2] = {};
Listener s_listener = {};
hal_wifi_state_t s_wifi_state = HAL_WIFI_STATE_OFF;

hal_net_endpoint_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                        uint16_t port = 0u) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.addr[0] = a;
  endpoint.addr[1] = b;
  endpoint.addr[2] = c;
  endpoint.addr[3] = d;
  endpoint.port = port;
  return endpoint;
}

Socket *allocate(Socket (&sockets)[2]) {
  for (Socket &socket : sockets) {
    if (!socket.in_use) {
      socket = {};
      socket.in_use = true;
      return &socket;
    }
  }
  return nullptr;
}

bool valid(const Socket *socket) { return socket != nullptr && socket->in_use; }

hal_status_t service_ok(void) { return HAL_OK; }

hal_status_t wifi_set_mode(hal_wifi_mode_t mode) {
  if (mode == HAL_WIFI_MODE_OFF) {
    s_wifi_state = HAL_WIFI_STATE_OFF;
    return HAL_OK;
  }
  if (mode == HAL_WIFI_MODE_STA) {
    s_wifi_state = HAL_WIFI_STATE_IDLE;
    return HAL_OK;
  }
  return mode == HAL_WIFI_MODE_AP || mode == HAL_WIFI_MODE_AP_STA
             ? HAL_EUNSUPPORTED
             : HAL_EINVAL;
}

hal_status_t wifi_disconnect(bool erase_credentials) {
  (void)erase_credentials;
  s_wifi_state = HAL_WIFI_STATE_IDLE;
  return HAL_OK;
}

hal_status_t wifi_set_hostname(const char *hostname) {
  return hostname != nullptr && hostname[0] != '\0' ? HAL_OK : HAL_EINVAL;
}

hal_status_t wifi_join(const char *ssid, const char *password,
                       bool non_blocking, uint32_t timeout_ms) {
  (void)non_blocking;
  (void)timeout_ms;
  if (ssid == nullptr || ssid[0] == '\0' || password == nullptr) {
    return HAL_EINVAL;
  }
  s_wifi_state = HAL_WIFI_STATE_CONNECTED;
  return HAL_OK;
}

hal_status_t wifi_get_state(hal_wifi_state_t *out_state) {
  if (out_state == nullptr) {
    return HAL_EINVAL;
  }
  *out_state = s_wifi_state;
  return HAL_OK;
}

hal_status_t wifi_get_local(hal_net_endpoint_t *out_address) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  *out_address = ipv4(192u, 0u, 2u, 30u);
  return HAL_OK;
}

hal_status_t wifi_get_dns(hal_net_endpoint_t *out_address) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  *out_address = ipv4(192u, 0u, 2u, 53u);
  return HAL_OK;
}

hal_status_t wifi_get_mac(uint8_t out_mac[HAL_WIFI_BSSID_LEN]) {
  if (out_mac == nullptr) {
    return HAL_EINVAL;
  }
  const uint8_t mac[HAL_WIFI_BSSID_LEN] = {0x02u, 0x00u, 0x00u,
                                           0x00u, 0x00u, 0x30u};
  memcpy(out_mac, mac, sizeof(mac));
  return HAL_OK;
}

hal_status_t wifi_get_rssi(int32_t *out_rssi) {
  if (out_rssi == nullptr) {
    return HAL_EINVAL;
  }
  *out_rssi = -60;
  return HAL_OK;
}

hal_status_t unsupported_ping(const hal_net_endpoint_t *, uint32_t, int *) {
  return HAL_EUNSUPPORTED;
}

hal_status_t unsupported_scan(uint32_t, int *) { return HAL_EUNSUPPORTED; }

hal_status_t unsupported_scan_result(size_t, hal_wifi_scan_result_t *) {
  return HAL_EUNSUPPORTED;
}

hal_status_t resolve(const char *hostname, hal_net_family_t family_hint,
                     hal_net_endpoint_t *results, size_t capacity,
                     size_t *out_count) {
  if (out_count != nullptr) {
    *out_count = 0u;
  }
  if (hostname == nullptr || hostname[0] == '\0' || out_count == nullptr ||
      (capacity > 0u && results == nullptr)) {
    return HAL_EINVAL;
  }
  if (family_hint == HAL_NET_AF_INET6) {
    return HAL_EUNSUPPORTED;
  }
  if (family_hint != HAL_NET_AF_UNSPEC && family_hint != HAL_NET_AF_INET) {
    return HAL_EINVAL;
  }
  if (strcmp(hostname, "contract.test") != 0) {
    return HAL_ENOENT;
  }
  *out_count = 1u;
  if (capacity < 1u) {
    return HAL_EOVERFLOW;
  }
  results[0] = ipv4(203u, 0u, 113u, 30u);
  return HAL_OK;
}

hal_status_t tcp_open(void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = allocate(s_tcp);
  return *out_socket != nullptr ? HAL_OK : HAL_ENOMEM;
}

hal_status_t tcp_connect(void *token, const hal_net_endpoint_t *remote,
                         uint32_t timeout_ms) {
  (void)timeout_ms;
  Socket *socket = static_cast<Socket *>(token);
  if (!valid(socket) || remote == nullptr || remote->port == 0u) {
    return HAL_EINVAL;
  }
  if (remote->family != HAL_NET_AF_INET ||
      remote->addr_len != HAL_NET_IPV4_ADDR_LEN) {
    return HAL_EUNSUPPORTED;
  }
  socket->endpoint = *remote;
  socket->connected = true;
  return HAL_OK;
}

hal_status_t socket_send(Socket *socket, const void *data, size_t length,
                         size_t *out_sent, bool require_connected) {
  if (out_sent != nullptr) {
    *out_sent = 0u;
  }
  if (!valid(socket) || out_sent == nullptr ||
      (length > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  if (require_connected ? !socket->connected : !socket->bound) {
    return HAL_ESTATE;
  }
  const size_t copied = length < kPayloadCapacity ? length : kPayloadCapacity;
  if (copied > 0u) {
    memcpy(socket->tx, data, copied);
  }
  socket->tx_length = copied;
  *out_sent = copied;
  return HAL_OK;
}

hal_status_t tcp_send(void *token, const void *data, size_t length,
                      size_t *out_sent) {
  return socket_send(static_cast<Socket *>(token), data, length, out_sent,
                     true);
}

hal_status_t socket_recv(Socket *socket, void *buffer, size_t capacity,
                         size_t *out_received, bool require_connected) {
  if (out_received != nullptr) {
    *out_received = 0u;
  }
  if (!valid(socket) || out_received == nullptr ||
      (capacity > 0u && buffer == nullptr)) {
    return HAL_EINVAL;
  }
  if (require_connected ? !socket->connected : !socket->bound) {
    return HAL_ESTATE;
  }
  const size_t available = socket->rx_length - socket->rx_position;
  const size_t copied = capacity < available ? capacity : available;
  if (copied > 0u) {
    memcpy(buffer, socket->rx + socket->rx_position, copied);
    socket->rx_position += copied;
  }
  *out_received = copied;
  return copied > 0u ? HAL_OK : HAL_EAGAIN;
}

hal_status_t tcp_recv(void *token, void *buffer, size_t capacity,
                      uint32_t timeout_ms, size_t *out_received) {
  (void)timeout_ms;
  return socket_recv(static_cast<Socket *>(token), buffer, capacity,
                     out_received, true);
}

bool tcp_can_recv(void *token) {
  Socket *socket = static_cast<Socket *>(token);
  return valid(socket) && socket->connected &&
         socket->rx_position < socket->rx_length;
}

bool tcp_can_send(void *token) {
  Socket *socket = static_cast<Socket *>(token);
  return valid(socket) && socket->connected;
}

bool tcp_connected(void *token) { return tcp_can_send(token); }

void tcp_shutdown(void *token) {
  Socket *socket = static_cast<Socket *>(token);
  if (valid(socket)) {
    socket->connected = false;
  }
}

void socket_close(void *token) {
  Socket *socket = static_cast<Socket *>(token);
  if (valid(socket)) {
    *socket = {};
  }
}

hal_status_t listener_open(void **out_listener) {
  if (out_listener == nullptr) {
    return HAL_EINVAL;
  }
  if (s_listener.in_use) {
    *out_listener = nullptr;
    return HAL_ENOMEM;
  }
  s_listener = {};
  s_listener.in_use = true;
  *out_listener = &s_listener;
  return HAL_OK;
}

hal_status_t listener_bind(void *token, const hal_net_endpoint_t *local) {
  Listener *listener = static_cast<Listener *>(token);
  if (listener != &s_listener || !listener->in_use || local == nullptr ||
      local->port == 0u) {
    return HAL_EINVAL;
  }
  if (local->family != HAL_NET_AF_INET) {
    return HAL_EUNSUPPORTED;
  }
  listener->local = *local;
  listener->bound = true;
  return HAL_OK;
}

hal_status_t listener_listen(void *token, uint8_t backlog) {
  Listener *listener = static_cast<Listener *>(token);
  if (listener != &s_listener || !listener->in_use || !listener->bound ||
      backlog == 0u) {
    return HAL_ESTATE;
  }
  listener->listening = true;
  return HAL_OK;
}

hal_status_t listener_accept(void *token, hal_net_endpoint_t *remote,
                             uint32_t timeout_ms, void **out_socket) {
  (void)remote;
  (void)timeout_ms;
  Listener *listener = static_cast<Listener *>(token);
  if (out_socket != nullptr) {
    *out_socket = nullptr;
  }
  return listener == &s_listener && listener->in_use && listener->listening &&
                 out_socket != nullptr
             ? HAL_EAGAIN
             : HAL_EINVAL;
}

bool listener_can_accept(void *) { return false; }

void listener_close(void *token) {
  if (token == &s_listener) {
    s_listener = {};
  }
}

hal_status_t udp_open(void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = allocate(s_udp);
  return *out_socket != nullptr ? HAL_OK : HAL_ENOMEM;
}

hal_status_t udp_bind(void *token, const hal_net_endpoint_t *local) {
  Socket *socket = static_cast<Socket *>(token);
  if (!valid(socket) || local == nullptr || local->port == 0u) {
    return HAL_EINVAL;
  }
  if (local->family != HAL_NET_AF_INET) {
    return HAL_EUNSUPPORTED;
  }
  socket->endpoint = *local;
  socket->bound = true;
  return HAL_OK;
}

hal_status_t udp_send(void *token, const void *data, size_t length,
                      const hal_net_endpoint_t *remote, size_t *out_sent) {
  if (remote == nullptr || remote->family != HAL_NET_AF_INET ||
      remote->port == 0u) {
    return remote != nullptr && remote->family != HAL_NET_AF_INET
               ? HAL_EUNSUPPORTED
               : HAL_EINVAL;
  }
  return socket_send(static_cast<Socket *>(token), data, length, out_sent,
                     false);
}

hal_status_t udp_recv(void *token, void *buffer, size_t capacity,
                      hal_net_endpoint_t *remote, uint32_t timeout_ms,
                      size_t *out_received) {
  (void)timeout_ms;
  Socket *socket = static_cast<Socket *>(token);
  const hal_status_t status =
      socket_recv(socket, buffer, capacity, out_received, false);
  if (remote != nullptr && valid(socket)) {
    *remote = socket->endpoint;
  }
  return status;
}

bool udp_can_recv(void *token) {
  Socket *socket = static_cast<Socket *>(token);
  return valid(socket) && socket->bound &&
         socket->rx_position < socket->rx_length;
}

bool udp_can_send(void *token) {
  Socket *socket = static_cast<Socket *>(token);
  return valid(socket) && socket->bound;
}

const jh_network_service_ops_t s_service_ops = {
    service_ok, service_ok, service_ok, nullptr, nullptr,
};

const jh_network_wifi_ops_t s_wifi_ops = {
    wifi_set_mode,    wifi_disconnect,  wifi_set_hostname,
    wifi_join,        wifi_get_state,   wifi_get_local,
    wifi_get_dns,     wifi_get_mac,     wifi_get_rssi,
    unsupported_ping, unsupported_scan, unsupported_scan_result,
};

const jh_network_resolver_ops_t s_resolver_ops = {resolve};

const jh_network_tcp_ops_t s_tcp_ops = {
    tcp_open,        tcp_connect,         tcp_send,       tcp_recv,
    tcp_can_recv,    tcp_can_send,        tcp_connected,  tcp_shutdown,
    socket_close,    listener_open,       listener_bind,  listener_listen,
    listener_accept, listener_can_accept, listener_close,
};

const jh_network_udp_ops_t s_udp_ops = {
    udp_open,     udp_bind,     udp_send,     udp_recv,
    udp_can_recv, udp_can_send, socket_close,
};

const jh_network_backend_descriptor_t s_backend = {
    JH_NETWORK_BACKEND_ABI_VERSION,
    "mock-socket-offload",
    JH_NET_CAP_WIFI_STA | JH_NET_CAP_DNS | JH_NET_CAP_TCP_CLIENT |
        JH_NET_CAP_TCP_LISTENER | JH_NET_CAP_UDP | JH_NET_CAP_IPV4,
    JH_NETWORK_EXECUTION_OWNED_WORKER,
    &s_service_ops,
    &s_wifi_ops,
    &s_resolver_ops,
    &s_tcp_ops,
    &s_udp_ops,
};

} // namespace

extern "C" const jh_network_backend_descriptor_t *
jh_network_backend_selected(void) {
  return &s_backend;
}

extern "C" void jh_contract_backend_reset(void) {
  memset(s_tcp, 0, sizeof(s_tcp));
  memset(s_udp, 0, sizeof(s_udp));
  s_listener = {};
  s_wifi_state = HAL_WIFI_STATE_OFF;
}

extern "C" bool jh_contract_backend_is_socket_offload(void) { return true; }

extern "C" void jh_contract_backend_tcp_inject(void *token,
                                               const uint8_t *payload,
                                               size_t length) {
  Socket *socket = static_cast<Socket *>(token);
  if (!valid(socket)) {
    return;
  }
  const size_t copied = length < kPayloadCapacity ? length : kPayloadCapacity;
  if (copied > 0u && payload != nullptr) {
    memcpy(socket->rx, payload, copied);
  }
  socket->rx_length = copied;
  socket->rx_position = 0u;
}

extern "C" void jh_contract_backend_udp_inject(void *token,
                                               const hal_net_endpoint_t *remote,
                                               const uint8_t *payload,
                                               size_t length) {
  Socket *socket = static_cast<Socket *>(token);
  if (!valid(socket) || remote == nullptr) {
    return;
  }
  socket->endpoint = *remote;
  const size_t copied = length < kPayloadCapacity ? length : kPayloadCapacity;
  if (copied > 0u && payload != nullptr) {
    memcpy(socket->rx, payload, copied);
  }
  socket->rx_length = copied;
  socket->rx_position = 0u;
}
