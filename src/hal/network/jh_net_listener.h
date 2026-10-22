#pragma once

/**
 * @file jh_net_listener.h
 * @brief Listener start shared by the HAL TCP services (HTTP server,
 *        WebSocket server, network console).
 */

#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_TCP)

#include "hal/network/hal_tcp.h"

#include <stdint.h>
#include <string.h>

/** @brief Fill @p out with the IPv4 wildcard address 0.0.0.0 and @p port. */
static inline void jh_net_any_ipv4_endpoint(uint16_t port,
                                            hal_net_endpoint_t *out) {
  memset(out, 0, sizeof(*out));
  out->family = HAL_NET_AF_INET;
  out->addr_len = HAL_NET_IPV4_ADDR_LEN;
  out->port = port;
}

/**
 * @brief Open a TCP listener, bind it to @p local and start listening.
 *
 * @param local Local IPv4/IPv6 address and non-zero port; the unspecified
 *        address listens on every interface of that family.
 * @param backlog Pending-connection depth passed to the backend.
 * @param out_listener Receives the listener; set to NULL on failure, when the
 *        partially opened listener is already closed.
 * @return HAL_OK or the status of the failed open/bind/listen step: the bind
 *         reports HAL_EINVAL for a NULL or malformed endpoint and port 0,
 *         HAL_EBUSY for an address in use and HAL_EUNSUPPORTED for an
 *         unsupported family.
 */
static inline hal_status_t
jh_net_listener_start(const hal_net_endpoint_t *local, uint8_t backlog,
                      hal_tcp_listener_t *out_listener) {
  *out_listener = NULL;
  hal_tcp_listener_t listener = NULL;
  hal_status_t status = hal_tcp_listener_open_ex(&listener);
  if (status == HAL_OK) {
    status = hal_tcp_listener_bind_ex(listener, local);
  }
  if (status == HAL_OK) {
    status = hal_tcp_listener_listen_ex(listener, backlog);
  }
  if (status != HAL_OK) {
    if (listener != NULL) {
      hal_tcp_listener_close(listener);
    }
    return status;
  }
  *out_listener = listener;
  return HAL_OK;
}

#endif /* HAL_ENABLE_TCP */
