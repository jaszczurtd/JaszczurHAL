#include "hal_config.h"

#if defined(HAL_ENABLE_MQTT) || defined(HAL_ENABLE_WIREGUARD)
static bool net_text_valid(const char *text) {
  return text != nullptr && text[0] != '\0';
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
