#include "jh_public_network_backend_adapter.h"

#if defined(HAL_ENABLE_NETWORK_CORE) && !defined(HAL_NETWORK_BACKEND_CYW43)

#include "hal/network/hal_tcp.h"
#include "hal/network/hal_udp.h"
#include "jh_net_address_utils.h"

#include <stdio.h>
#include <string.h>

#if defined(HAL_ENABLE_WIFI)
static hal_status_t wifi_set_mode(hal_wifi_mode_t mode) {
  return hal_wifi_set_mode_ex(mode);
}

static hal_status_t wifi_disconnect(bool erase_credentials) {
  return hal_wifi_disconnect_ex(erase_credentials);
}

static hal_status_t wifi_set_hostname(const char *hostname) {
  return hal_wifi_set_hostname_ex(hostname);
}

static hal_status_t wifi_join(const char *ssid, const char *password,
                              bool non_blocking, uint32_t timeout_ms) {
  const hal_status_t timeout_status = hal_wifi_set_timeout_ms_ex(timeout_ms);
  return timeout_status == HAL_OK
             ? hal_wifi_begin_station_ex(ssid, password, non_blocking)
             : timeout_status;
}

static hal_status_t wifi_get_state(hal_wifi_state_t *out_state) {
  return hal_wifi_get_state_ex(out_state);
}

static hal_status_t parse_ipv4_getter(hal_status_t (*getter)(char *out,
                                                             size_t out_size),
                                      hal_net_endpoint_t *out_address) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  char text[16] = {};
  const hal_status_t status = getter(text, sizeof(text));
  if (status != HAL_OK) {
    return status;
  }
  hal_net_endpoint_t result = {};
  if (!jh_net_parse_ipv4_literal(text, result.addr)) {
    return HAL_EIO;
  }
  result.family = HAL_NET_AF_INET;
  result.addr_len = HAL_NET_IPV4_ADDR_LEN;
  *out_address = result;
  return HAL_OK;
}

static hal_status_t wifi_get_local(hal_net_endpoint_t *out_address) {
  return parse_ipv4_getter(hal_wifi_get_local_ip_ex, out_address);
}

static hal_status_t wifi_get_dns(hal_net_endpoint_t *out_address) {
  return parse_ipv4_getter(hal_wifi_get_dns_ip_ex, out_address);
}

static hal_status_t wifi_get_mac(uint8_t out_mac[HAL_WIFI_BSSID_LEN]) {
  if (out_mac == nullptr) {
    return HAL_EINVAL;
  }
  char text[18] = {};
  const hal_status_t status = hal_wifi_get_mac_ex(text, sizeof(text));
  if (status != HAL_OK) {
    return status;
  }
  unsigned octets[HAL_WIFI_BSSID_LEN] = {};
  if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x", &octets[0], &octets[1],
             &octets[2], &octets[3], &octets[4],
             &octets[5]) != (int)HAL_WIFI_BSSID_LEN) {
    return HAL_EIO;
  }
  for (size_t index = 0u; index < HAL_WIFI_BSSID_LEN; ++index) {
    out_mac[index] = (uint8_t)octets[index];
  }
  return HAL_OK;
}

static hal_status_t wifi_get_rssi(int32_t *out_rssi) {
  if (out_rssi == nullptr) {
    return HAL_EINVAL;
  }
  *out_rssi = hal_wifi_rssi();
  return HAL_OK;
}

static hal_status_t wifi_ping(const hal_net_endpoint_t *remote,
                              uint32_t timeout_ms, int *out_result) {
  if (remote == nullptr || out_result == nullptr) {
    return HAL_EINVAL;
  }
  if (remote->family != HAL_NET_AF_INET ||
      remote->addr_len != HAL_NET_IPV4_ADDR_LEN) {
    return HAL_EUNSUPPORTED;
  }
  char text[16] = {};
  const int written =
      snprintf(text, sizeof(text), "%u.%u.%u.%u", (unsigned)remote->addr[0],
               (unsigned)remote->addr[1], (unsigned)remote->addr[2],
               (unsigned)remote->addr[3]);
  return written > 0 && (size_t)written < sizeof(text)
             ? hal_wifi_ping_status_ex(text, timeout_ms, out_result)
             : HAL_EOVERFLOW;
}

static hal_status_t wifi_scan(uint32_t timeout_ms, int *out_count) {
  (void)timeout_ms;
  return hal_wifi_scan_networks_ex(out_count);
}

static hal_status_t wifi_get_scan_result(size_t index,
                                         hal_wifi_scan_result_t *out_result) {
  return hal_wifi_get_scan_result_ex(index, out_result);
}

static const jh_network_wifi_ops_t s_wifi_ops = {
    wifi_set_mode,  wifi_disconnect, wifi_set_hostname, wifi_join,
    wifi_get_state, wifi_get_local,  wifi_get_dns,      wifi_get_mac,
    wifi_get_rssi,  wifi_ping,       wifi_scan,         wifi_get_scan_result,
};
#endif

static hal_status_t resolver_resolve(const char *hostname,
                                     hal_net_family_t family_hint,
                                     hal_net_endpoint_t *results,
                                     size_t capacity, size_t *out_count) {
  return hal_net_resolve_ex(hostname, family_hint, results, capacity,
                            out_count);
}

static const jh_network_resolver_ops_t s_resolver_ops = {resolver_resolve};

#if defined(HAL_ENABLE_TCP)
static hal_status_t tcp_socket_open(void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  hal_tcp_socket_t socket = nullptr;
  const hal_status_t status = hal_tcp_socket_open_ex(&socket);
  if (status == HAL_OK) {
    *out_socket = reinterpret_cast<void *>(socket);
  }
  return status;
}

static hal_status_t tcp_socket_connect(void *socket,
                                       const hal_net_endpoint_t *remote,
                                       uint32_t timeout_ms) {
  return hal_tcp_socket_connect_ex(reinterpret_cast<hal_tcp_socket_t>(socket),
                                   remote, timeout_ms);
}

static hal_status_t tcp_socket_send(void *socket, const void *data, size_t len,
                                    size_t *out_sent) {
  return hal_tcp_socket_send_ex(reinterpret_cast<hal_tcp_socket_t>(socket),
                                data, len, out_sent);
}

static hal_status_t tcp_socket_recv(void *socket, void *buffer, size_t max_len,
                                    uint32_t timeout_ms, size_t *out_received) {
  return hal_tcp_socket_recv_ex(reinterpret_cast<hal_tcp_socket_t>(socket),
                                buffer, max_len, timeout_ms, out_received);
}

static bool tcp_socket_can_recv(void *socket) {
  return hal_tcp_socket_can_recv(reinterpret_cast<hal_tcp_socket_t>(socket));
}

static bool tcp_socket_can_send(void *socket) {
  return hal_tcp_socket_can_send(reinterpret_cast<hal_tcp_socket_t>(socket));
}

static bool tcp_socket_is_connected(void *socket) {
  return hal_tcp_socket_is_connected(
      reinterpret_cast<hal_tcp_socket_t>(socket));
}

static void tcp_socket_shutdown(void *socket) {
  hal_tcp_socket_shutdown(reinterpret_cast<hal_tcp_socket_t>(socket));
}

static void tcp_socket_close(void *socket) {
  hal_tcp_socket_close(reinterpret_cast<hal_tcp_socket_t>(socket));
}

static hal_status_t tcp_listener_open(void **out_listener) {
  if (out_listener == nullptr) {
    return HAL_EINVAL;
  }
  *out_listener = nullptr;
  hal_tcp_listener_t listener = nullptr;
  const hal_status_t status = hal_tcp_listener_open_ex(&listener);
  if (status == HAL_OK) {
    *out_listener = reinterpret_cast<void *>(listener);
  }
  return status;
}

static hal_status_t tcp_listener_bind(void *listener,
                                      const hal_net_endpoint_t *local) {
  return hal_tcp_listener_bind_ex(
      reinterpret_cast<hal_tcp_listener_t>(listener), local);
}

static hal_status_t tcp_listener_listen(void *listener, uint8_t backlog) {
  return hal_tcp_listener_listen_ex(
      reinterpret_cast<hal_tcp_listener_t>(listener), backlog);
}

static hal_status_t tcp_listener_accept(void *listener,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  hal_tcp_socket_t socket = nullptr;
  const hal_status_t status =
      hal_tcp_listener_accept_ex(reinterpret_cast<hal_tcp_listener_t>(listener),
                                 remote, timeout_ms, &socket);
  if (status == HAL_OK) {
    *out_socket = reinterpret_cast<void *>(socket);
  }
  return status;
}

static bool tcp_listener_can_accept(void *listener) {
  return hal_tcp_listener_can_accept(
      reinterpret_cast<hal_tcp_listener_t>(listener));
}

static void tcp_listener_close(void *listener) {
  hal_tcp_listener_close(reinterpret_cast<hal_tcp_listener_t>(listener));
}

static const jh_network_tcp_ops_t s_tcp_ops = {
    tcp_socket_open,         tcp_socket_connect,      tcp_socket_send,
    tcp_socket_recv,         tcp_socket_can_recv,     tcp_socket_can_send,
    tcp_socket_is_connected, tcp_socket_shutdown,     tcp_socket_close,
    tcp_listener_open,       tcp_listener_bind,       tcp_listener_listen,
    tcp_listener_accept,     tcp_listener_can_accept, tcp_listener_close,
};
#endif

#if defined(HAL_ENABLE_UDP)
static hal_status_t udp_socket_open(void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  hal_udp_socket_t socket = nullptr;
  const hal_status_t status = hal_udp_socket_open_ex(&socket);
  if (status == HAL_OK) {
    *out_socket = reinterpret_cast<void *>(socket);
  }
  return status;
}

static hal_status_t udp_socket_bind(void *socket,
                                    const hal_net_endpoint_t *local) {
  return hal_udp_socket_bind_ex(reinterpret_cast<hal_udp_socket_t>(socket),
                                local);
}

static hal_status_t udp_socket_sendto(void *socket, const void *data,
                                      size_t len,
                                      const hal_net_endpoint_t *remote,
                                      size_t *out_sent) {
  return hal_udp_socket_sendto_ex(reinterpret_cast<hal_udp_socket_t>(socket),
                                  data, len, remote, out_sent);
}

static hal_status_t udp_socket_recvfrom(void *socket, void *buffer,
                                        size_t max_len,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        size_t *out_received) {
  return hal_udp_socket_recvfrom_ex(reinterpret_cast<hal_udp_socket_t>(socket),
                                    buffer, max_len, remote, timeout_ms,
                                    out_received);
}

static bool udp_socket_can_recv(void *socket) {
  return hal_udp_socket_can_recv(reinterpret_cast<hal_udp_socket_t>(socket));
}

static bool udp_socket_can_send(void *socket) {
  return hal_udp_socket_can_send(reinterpret_cast<hal_udp_socket_t>(socket));
}

static void udp_socket_close(void *socket) {
  hal_udp_socket_close(reinterpret_cast<hal_udp_socket_t>(socket));
}

static const jh_network_udp_ops_t s_udp_ops = {
    udp_socket_open,     udp_socket_bind,     udp_socket_sendto,
    udp_socket_recvfrom, udp_socket_can_recv, udp_socket_can_send,
    udp_socket_close,
};
#endif

extern "C" const jh_network_wifi_ops_t *jh_public_network_wifi_ops(void) {
#if defined(HAL_ENABLE_WIFI)
  return &s_wifi_ops;
#else
  return nullptr;
#endif
}

extern "C" const jh_network_resolver_ops_t *
jh_public_network_resolver_ops(void) {
  return &s_resolver_ops;
}

extern "C" const jh_network_tcp_ops_t *jh_public_network_tcp_ops(void) {
#if defined(HAL_ENABLE_TCP)
  return &s_tcp_ops;
#else
  return nullptr;
#endif
}

extern "C" const jh_network_udp_ops_t *jh_public_network_udp_ops(void) {
#if defined(HAL_ENABLE_UDP)
  return &s_udp_ops;
#else
  return nullptr;
#endif
}

#endif
