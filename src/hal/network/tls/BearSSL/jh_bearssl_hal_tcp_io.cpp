#include "jh_bearssl_hal_tcp_io.h"
#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_TLS

#include <string.h>

static hal_status_t hal_tcp_send(void *context, const void *data, size_t length,
                                 size_t *out_sent) {
  jh_bearssl_hal_tcp_transport_t *adapter =
      static_cast<jh_bearssl_hal_tcp_transport_t *>(context);
  if (adapter == nullptr || adapter->socket == nullptr || out_sent == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t status =
      hal_tcp_socket_send_ex(adapter->socket, data, length, out_sent);
  if (status != HAL_OK || *out_sent > 0u) {
    return status;
  }
  return hal_tcp_socket_is_connected(adapter->socket) ? HAL_EAGAIN : HAL_EIO;
}

static hal_status_t hal_tcp_receive(void *context, void *buffer,
                                    size_t capacity, size_t *out_received) {
  jh_bearssl_hal_tcp_transport_t *adapter =
      static_cast<jh_bearssl_hal_tcp_transport_t *>(context);
  if (adapter == nullptr || adapter->socket == nullptr ||
      out_received == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t status = hal_tcp_socket_recv_ex(
      adapter->socket, buffer, capacity, 0u, out_received);
  if (status != HAL_OK || *out_received > 0u) {
    return status;
  }
  return hal_tcp_socket_is_connected(adapter->socket) ? HAL_EAGAIN : HAL_EPROTO;
}

hal_status_t
jh_bearssl_hal_tcp_transport_init(jh_bearssl_hal_tcp_transport_t *adapter,
                                  hal_tcp_socket_t socket) {
  if (adapter == nullptr || socket == nullptr) {
    return HAL_EINVAL;
  }
  memset(adapter, 0, sizeof(*adapter));
  adapter->socket = socket;
  adapter->transport.context = adapter;
  adapter->transport.send = hal_tcp_send;
  adapter->transport.receive = hal_tcp_receive;
  return HAL_OK;
}

#endif
