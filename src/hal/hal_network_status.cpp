#include "hal_config.h"

#include <limits.h>
#include <string.h>

#if defined(HAL_ENABLE_TCP) || defined(HAL_ENABLE_UDP)
#include "hal_net.h"

static bool endpoint_valid(const hal_net_endpoint_t *ep) {
  return ep != nullptr && ep->family == HAL_NET_AF_INET && ep->port != 0u;
}
#endif

#ifdef HAL_ENABLE_WIFI
#include "hal_net.h"
#include "hal_wifi.h"

static bool net_text_valid(const char *text) {
  return text != nullptr && text[0] != '\0';
}

hal_status_t hal_wifi_set_mode_ex(hal_wifi_mode_t mode) {
  if (mode < HAL_WIFI_MODE_OFF || mode > HAL_WIFI_MODE_AP_STA)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_wifi_set_mode(mode), HAL_EIO);
}
hal_status_t hal_wifi_disconnect_ex(bool erase_credentials) {
  return hal_status_from_bool(hal_wifi_disconnect(erase_credentials), HAL_EIO);
}
hal_status_t hal_wifi_set_hostname_ex(const char *hostname) {
  if (!net_text_valid(hostname))
    return HAL_EINVAL;
  return hal_status_from_bool(hal_wifi_set_hostname(hostname), HAL_EIO);
}
hal_status_t hal_wifi_begin_station_ex(const char *ssid, const char *password,
                                       bool non_blocking) {
  if (!net_text_valid(ssid) || password == nullptr)
    return HAL_EINVAL;
  return hal_status_from_bool(
      hal_wifi_begin_station(ssid, password, non_blocking), HAL_EIO);
}
static hal_status_t net_text_validate(char *out, size_t out_size) {
  if (out == nullptr || out_size == 0u)
    return HAL_EINVAL;
  return HAL_OK;
}
hal_status_t hal_wifi_get_local_ip_ex(char *out, size_t out_size) {
  if (hal_status_is_error(net_text_validate(out, out_size)))
    return HAL_EINVAL;
  return hal_status_from_bool(hal_wifi_get_local_ip(out, out_size), HAL_EIO);
}
hal_status_t hal_wifi_get_dns_ip_ex(char *out, size_t out_size) {
  if (hal_status_is_error(net_text_validate(out, out_size)))
    return HAL_EINVAL;
  return hal_status_from_bool(hal_wifi_get_dns_ip(out, out_size), HAL_EIO);
}
hal_status_t hal_wifi_get_mac_ex(char *out, size_t out_size) {
  if (hal_status_is_error(net_text_validate(out, out_size)))
    return HAL_EINVAL;
  return hal_status_from_bool(hal_wifi_get_mac(out, out_size), HAL_EIO);
}
hal_status_t hal_wifi_ping_status_ex(const char *host_or_ip,
                                     uint32_t timeout_ms, int *out_result) {
  if (!net_text_valid(host_or_ip) || out_result == nullptr)
    return HAL_EINVAL;
  const int result = hal_wifi_ping_ex(host_or_ip, timeout_ms);
  *out_result = result;
  return result >= 0 ? HAL_OK : HAL_EIO;
}
hal_status_t hal_wifi_scan_networks_ex(int *out_count) {
  if (out_count == nullptr)
    return HAL_EINVAL;
  const int result = hal_wifi_scan_networks();
  *out_count = result;
  return result >= 0 ? HAL_OK : HAL_EIO;
}
hal_status_t hal_wifi_get_scan_result_ex(size_t index,
                                         hal_wifi_scan_result_t *out) {
  if (out == nullptr)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_wifi_get_scan_result(index, out), HAL_ENOENT);
}
hal_status_t hal_net_resolve_ipv4_ex(const char *host_or_ip,
                                     uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (!net_text_valid(host_or_ip) || out_addr == nullptr)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_net_resolve_ipv4(host_or_ip, out_addr),
                              HAL_ENOENT);
}
#endif

#ifdef HAL_ENABLE_TCP
#include "hal_tcp.h"
hal_status_t hal_tcp_socket_connect_ex(hal_tcp_socket_t s,
                                       const hal_net_endpoint_t *r,
                                       uint32_t t) {
  if (s == nullptr || !endpoint_valid(r))
    return HAL_EINVAL;
  return hal_status_from_bool(hal_tcp_socket_connect(s, r, t), HAL_EIO);
}
hal_status_t hal_tcp_socket_send_ex(hal_tcp_socket_t s, const void *data,
                                    size_t len, size_t *out) {
  if (out == nullptr || s == nullptr || (len && data == nullptr))
    return HAL_EINVAL;
  const int n = hal_tcp_socket_send(s, data, len);
  *out = n > 0 ? (size_t)n : 0u;
  return n < 0 ? HAL_EIO : HAL_OK;
}
hal_status_t hal_tcp_socket_recv_ex(hal_tcp_socket_t s, void *buf, size_t len,
                                    uint32_t t, size_t *out) {
  if (out == nullptr || s == nullptr || (len && buf == nullptr))
    return HAL_EINVAL;
  const int n = hal_tcp_socket_recv(s, buf, len, t);
  *out = n > 0 ? (size_t)n : 0u;
  return n < 0 ? HAL_EIO : HAL_OK;
}
hal_status_t hal_tcp_listener_bind_ex(hal_tcp_listener_t l,
                                      const hal_net_endpoint_t *ep) {
  if (l == nullptr || !endpoint_valid(ep))
    return HAL_EINVAL;
  return hal_status_from_bool(hal_tcp_listener_bind(l, ep), HAL_EIO);
}
hal_status_t hal_tcp_listener_listen_ex(hal_tcp_listener_t l, uint8_t backlog) {
  if (l == nullptr || backlog == 0u)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_tcp_listener_listen(l, backlog), HAL_EIO);
}
hal_status_t hal_tcp_listener_accept_ex(hal_tcp_listener_t l,
                                        hal_net_endpoint_t *remote, uint32_t t,
                                        hal_tcp_socket_t *out) {
  if (l == nullptr || out == nullptr)
    return HAL_EINVAL;
  *out = hal_tcp_listener_accept(l, remote, t);
  return *out != nullptr ? HAL_OK : HAL_EAGAIN;
}
#endif

#ifdef HAL_ENABLE_UDP
#include "hal_udp.h"
hal_status_t hal_udp_socket_bind_ex(hal_udp_socket_t s,
                                    const hal_net_endpoint_t *ep) {
  if (s == nullptr || !endpoint_valid(ep))
    return HAL_EINVAL;
  return hal_status_from_bool(hal_udp_socket_bind(s, ep), HAL_EIO);
}
hal_status_t hal_udp_socket_sendto_ex(hal_udp_socket_t s, const void *data,
                                      size_t len, const hal_net_endpoint_t *ep,
                                      size_t *out) {
  if (s == nullptr || out == nullptr || !endpoint_valid(ep) ||
      (len && data == nullptr))
    return HAL_EINVAL;
  const int n = hal_udp_socket_sendto(s, data, len, ep);
  *out = n > 0 ? (size_t)n : 0u;
  return n < 0 ? HAL_EIO : HAL_OK;
}
hal_status_t hal_udp_socket_recvfrom_ex(hal_udp_socket_t s, void *buf,
                                        size_t len, hal_net_endpoint_t *remote,
                                        uint32_t t, size_t *out) {
  if (s == nullptr || out == nullptr || (len && buf == nullptr))
    return HAL_EINVAL;
  const int n = hal_udp_socket_recvfrom(s, buf, len, remote, t);
  *out = n > 0 ? (size_t)n : 0u;
  return n < 0 ? HAL_EIO : HAL_OK;
}
hal_status_t hal_udp_begin_ex(uint16_t port) {
  if (!port)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_udp_begin(port), HAL_EIO);
}
hal_status_t hal_udp_read_ex(uint8_t *buf, uint16_t len, uint16_t *out) {
  if (out == nullptr || (len && buf == nullptr))
    return HAL_EINVAL;
  const int n = hal_udp_read(buf, len);
  *out = n > 0 ? (uint16_t)n : 0u;
  return n < 0 ? HAL_EIO : HAL_OK;
}
hal_status_t hal_udp_remote_ip_ex(char *out, size_t size) {
  if (out == nullptr || size == 0u)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_udp_remote_ip(out, size), HAL_ENOENT);
}
hal_status_t hal_udp_begin_packet_ex(const char *host, uint16_t port) {
  if (!net_text_valid(host) || !port)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_udp_begin_packet(host, port), HAL_EIO);
}
hal_status_t hal_udp_begin_packet_remote_ex(void) {
  return hal_status_from_bool(hal_udp_begin_packet_remote(), HAL_ESTATE);
}
hal_status_t hal_udp_write_ex(const uint8_t *data, uint16_t len,
                              uint16_t *out) {
  if (out == nullptr || (len && data == nullptr))
    return HAL_EINVAL;
  *out = hal_udp_write(data, len);
  return *out == len ? HAL_OK : HAL_EIO;
}
hal_status_t hal_udp_write_str_ex(const char *text, uint16_t *out) {
  if (text == nullptr || out == nullptr)
    return HAL_EINVAL;
  const size_t len = strlen(text);
  if (len > UINT16_MAX)
    return HAL_EOVERFLOW;
  *out = hal_udp_write_str(text);
  return *out == (uint16_t)len ? HAL_OK : HAL_EIO;
}
hal_status_t hal_udp_end_packet_ex(void) {
  return hal_status_from_bool(hal_udp_end_packet(), HAL_EIO);
}
#endif

#ifdef HAL_ENABLE_MQTT
#include "hal_mqtt.h"
hal_status_t hal_mqtt_set_server_ex(const char *h, uint16_t p) {
  if (!net_text_valid(h) || !p)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_mqtt_set_server(h, p), HAL_EIO);
}
hal_status_t hal_mqtt_set_callback_ex(hal_mqtt_message_callback_t cb, void *u) {
  return hal_status_from_bool(hal_mqtt_set_callback(cb, u), HAL_EIO);
}
hal_status_t hal_mqtt_set_keepalive_ex(uint16_t v) {
  if (!v)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_mqtt_set_keepalive(v), HAL_EIO);
}
hal_status_t hal_mqtt_set_socket_timeout_ex(uint16_t v) {
  if (!v)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_mqtt_set_socket_timeout(v), HAL_EIO);
}
hal_status_t hal_mqtt_set_buffer_size_ex(uint16_t v) {
  if (!v)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_mqtt_set_buffer_size(v), HAL_EIO);
}
hal_status_t hal_mqtt_connect_ex(const char *id) {
  if (!net_text_valid(id))
    return HAL_EINVAL;
  return hal_status_from_bool(hal_mqtt_connect(id), HAL_EIO);
}
hal_status_t hal_mqtt_connect_auth_ex(const char *id, const char *u,
                                      const char *p) {
  if (!net_text_valid(id) || u == nullptr || p == nullptr)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_mqtt_connect_auth(id, u, p), HAL_EIO);
}
hal_status_t hal_mqtt_loop_ex(void) {
  return hal_status_from_bool(hal_mqtt_loop(), HAL_EIO);
}
hal_status_t hal_mqtt_publish_ex(const char *t, const uint8_t *p, uint16_t n,
                                 bool r) {
  if (!net_text_valid(t) || (n && p == nullptr))
    return HAL_EINVAL;
  return hal_status_from_bool(hal_mqtt_publish(t, p, n, r), HAL_EIO);
}
hal_status_t hal_mqtt_publish_str_ex(const char *t, const char *p, bool r) {
  if (!net_text_valid(t) || p == nullptr)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_mqtt_publish_str(t, p, r), HAL_EIO);
}
hal_status_t hal_mqtt_subscribe_ex(const char *t, uint8_t q) {
  if (!net_text_valid(t) || q > 1u)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_mqtt_subscribe(t, q), HAL_EIO);
}
hal_status_t hal_mqtt_unsubscribe_ex(const char *t) {
  if (!net_text_valid(t))
    return HAL_EINVAL;
  return hal_status_from_bool(hal_mqtt_unsubscribe(t), HAL_EIO);
}
#endif

#ifdef HAL_ENABLE_WIREGUARD
#include "hal_wireguard.h"
hal_status_t hal_wireguard_begin_ex(const uint8_t ip[4], const char *priv,
                                    const char *host, const char *pub,
                                    uint16_t port) {
  if (ip == nullptr || !net_text_valid(priv) || !net_text_valid(host) ||
      !net_text_valid(pub) || !port)
    return HAL_EINVAL;
  return hal_status_from_bool(hal_wireguard_begin(ip, priv, host, pub, port),
                              HAL_EIO);
}
hal_status_t hal_wireguard_begin_advanced_ex(const uint8_t ip[4],
                                             const char *priv, const char *host,
                                             const char *pub, uint16_t port,
                                             const uint8_t allowed[4],
                                             const uint8_t mask[4]) {
  if (ip == nullptr || allowed == nullptr || mask == nullptr ||
      !net_text_valid(priv) || !net_text_valid(host) || !net_text_valid(pub) ||
      !port)
    return HAL_EINVAL;
  return hal_status_from_bool(
      hal_wireguard_begin_advanced(ip, priv, host, pub, port, allowed, mask),
      HAL_EIO);
}
hal_status_t hal_wireguard_peer_up_ex(char *ip, size_t size, uint16_t *port,
                                      bool *up) {
  if (up == nullptr || (ip != nullptr && size == 0u))
    return HAL_EINVAL;
  *up = hal_wireguard_peer_up(ip, size, port);
  return hal_wireguard_is_initialized() ? HAL_OK : HAL_EUNINIT;
}
hal_status_t hal_wireguard_kick_handshake_ex(const uint8_t ip[4], uint16_t port,
                                             uint32_t interval) {
  if (ip == nullptr || !port)
    return HAL_EINVAL;
  if (!hal_wireguard_is_initialized())
    return HAL_EUNINIT;
  return hal_status_from_bool(hal_wireguard_kick_handshake(ip, port, interval),
                              HAL_EIO);
}
#endif
