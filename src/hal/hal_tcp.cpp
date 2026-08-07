#include "hal_config.h"

#if defined(HAL_ENABLE_TCP) && defined(HAL_NETWORK_BACKEND_CYW43)

#include "hal_serial.h"
#include "hal_sync.h"
#include "hal_tcp.h"
#include "impl/shared/hal_mutex_once.h"
#include "impl/shared/network/jh_net_address_utils.h"
#include "impl/shared/network/jh_network_backend.h"
#include "impl/shared/network/jh_network_handle_pool.h"
#include "impl/shared/network/jh_network_runtime.h"

static jh_network_handle_slot_t s_socket_slots[HAL_TCP_SOCKET_MAX_INSTANCES];
static jh_network_handle_slot_t
    s_listener_slots[HAL_TCP_LISTENER_MAX_INSTANCES];
static jh_network_handle_pool_t s_socket_pool = {};
static jh_network_handle_pool_t s_listener_pool = {};
static hal_mutex_t s_mutex = NULL;
static bool s_pools_initialized = false;

static void ensure_pools(void) {
  (void)jh_hal_mutex_create_once(&s_mutex);
  hal_mutex_lock(s_mutex);
  if (!s_pools_initialized) {
    (void)jh_network_handle_pool_init(&s_socket_pool, s_socket_slots,
                                      HAL_TCP_SOCKET_MAX_INSTANCES, 1u);
    (void)jh_network_handle_pool_init(&s_listener_pool, s_listener_slots,
                                      HAL_TCP_LISTENER_MAX_INSTANCES, 2u);
    s_pools_initialized = true;
  }
  hal_mutex_unlock(s_mutex);
}

static const jh_network_tcp_ops_t *tcp_ops(void) {
  const jh_network_backend_descriptor_t *backend =
      jh_network_backend_selected();
  return jh_network_backend_validate(
             backend, JH_NET_CAP_TCP_CLIENT | JH_NET_CAP_TCP_LISTENER) == HAL_OK
             ? backend->tcp
             : nullptr;
}

static hal_status_t acquire_socket(hal_tcp_socket_t socket,
                                   jh_network_handle_lease_t *out_lease) {
  ensure_pools();
  hal_mutex_lock(s_mutex);
  const hal_status_t status = jh_network_handle_acquire(
      &s_socket_pool, reinterpret_cast<const void *>(socket), out_lease);
  hal_mutex_unlock(s_mutex);
  return status;
}

static hal_status_t acquire_listener(hal_tcp_listener_t listener,
                                     jh_network_handle_lease_t *out_lease) {
  ensure_pools();
  hal_mutex_lock(s_mutex);
  const hal_status_t status = jh_network_handle_acquire(
      &s_listener_pool, reinterpret_cast<const void *>(listener), out_lease);
  hal_mutex_unlock(s_mutex);
  return status;
}

static void finish_socket_operation(jh_network_handle_lease_t *lease) {
  void *deferred_token = nullptr;
  hal_mutex_lock(s_mutex);
  (void)jh_network_handle_end_operation(&s_socket_pool, lease, &deferred_token);
  hal_mutex_unlock(s_mutex);
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (deferred_token != nullptr && ops != nullptr &&
      ops->socket_close != nullptr) {
    ops->socket_close(deferred_token);
  }
}

static void finish_listener_operation(jh_network_handle_lease_t *lease) {
  void *deferred_token = nullptr;
  hal_mutex_lock(s_mutex);
  (void)jh_network_handle_end_operation(&s_listener_pool, lease,
                                        &deferred_token);
  hal_mutex_unlock(s_mutex);
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (deferred_token != nullptr && ops != nullptr &&
      ops->listener_close != nullptr) {
    ops->listener_close(deferred_token);
  }
}

static hal_status_t validate_endpoint(const hal_net_endpoint_t *endpoint,
                                      bool allow_unspecified) {
  const hal_status_t shape =
      jh_net_validate_endpoint_shape(endpoint, true, allow_unspecified);
  if (shape != HAL_OK) {
    return shape;
  }
  const hal_net_capabilities_t family =
      endpoint->family == HAL_NET_AF_INET ? HAL_NET_CAP_IPV4 : HAL_NET_CAP_IPV6;
  hal_net_capabilities_t capabilities = 0u;
  const hal_status_t capability_status =
      hal_net_get_capabilities_ex(&capabilities);
  if (capability_status != HAL_OK) {
    return capability_status;
  }
  return (capabilities & family) != 0u ? HAL_OK : HAL_EUNSUPPORTED;
}

hal_status_t hal_tcp_socket_open_ex(hal_tcp_socket_t *out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (ops == nullptr || ops->socket_open == nullptr ||
      ops->socket_close == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  void *backend_token = nullptr;
  hal_status_t status = ops->socket_open(&backend_token);
  if (status != HAL_OK) {
    return status;
  }
  ensure_pools();
  void *handle = nullptr;
  hal_mutex_lock(s_mutex);
  status = jh_network_handle_allocate(&s_socket_pool, backend_token, &handle);
  hal_mutex_unlock(s_mutex);
  if (status != HAL_OK) {
    ops->socket_close(backend_token);
    return status;
  }
  *out_socket = reinterpret_cast<hal_tcp_socket_t>(handle);
  return HAL_OK;
}

hal_tcp_socket_t hal_tcp_socket_open(void) {
  hal_tcp_socket_t socket = nullptr;
  (void)hal_tcp_socket_open_ex(&socket);
  return socket;
}

hal_status_t hal_tcp_socket_connect_ex(hal_tcp_socket_t socket,
                                       const hal_net_endpoint_t *remote,
                                       uint32_t timeout_ms) {
  const hal_status_t endpoint_status = validate_endpoint(remote, false);
  if (endpoint_status != HAL_OK) {
    return endpoint_status;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  jh_network_handle_lease_t lease = {};
  if (acquire_socket(socket, &lease) != HAL_OK) {
    return HAL_EINVAL;
  }
  const jh_network_tcp_ops_t *ops = tcp_ops();
  const hal_status_t status =
      ops != nullptr && ops->socket_connect != nullptr
          ? ops->socket_connect(lease.backend_token, remote, timeout_ms)
          : HAL_EUNSUPPORTED;
  finish_socket_operation(&lease);
  return status;
}

bool hal_tcp_socket_connect(hal_tcp_socket_t socket,
                            const hal_net_endpoint_t *remote,
                            uint32_t timeout_ms) {
  return hal_status_to_bool(
      hal_tcp_socket_connect_ex(socket, remote, timeout_ms));
}

hal_status_t hal_tcp_socket_send_ex(hal_tcp_socket_t socket, const void *data,
                                    size_t len, size_t *out_sent) {
  if (out_sent != nullptr) {
    *out_sent = 0u;
  }
  if (out_sent == nullptr || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  jh_network_handle_lease_t lease = {};
  if (acquire_socket(socket, &lease) != HAL_OK) {
    return HAL_EINVAL;
  }
  const jh_network_tcp_ops_t *ops = tcp_ops();
  const hal_status_t status =
      ops != nullptr && ops->socket_send != nullptr
          ? ops->socket_send(lease.backend_token, data, len, out_sent)
          : HAL_EUNSUPPORTED;
  finish_socket_operation(&lease);
  return status;
}

int hal_tcp_socket_send(hal_tcp_socket_t socket, const void *data, size_t len) {
  size_t sent = 0u;
  return hal_tcp_socket_send_ex(socket, data, len, &sent) == HAL_OK ? (int)sent
                                                                    : -1;
}

hal_status_t hal_tcp_socket_recv_ex(hal_tcp_socket_t socket, void *buffer,
                                    size_t max_len, uint32_t timeout_ms,
                                    size_t *out_received) {
  if (out_received != nullptr) {
    *out_received = 0u;
  }
  if (out_received == nullptr || (max_len > 0u && buffer == nullptr)) {
    return HAL_EINVAL;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  jh_network_handle_lease_t lease = {};
  if (acquire_socket(socket, &lease) != HAL_OK) {
    return HAL_EINVAL;
  }
  const jh_network_tcp_ops_t *ops = tcp_ops();
  const hal_status_t status =
      ops != nullptr && ops->socket_recv != nullptr
          ? ops->socket_recv(lease.backend_token, buffer, max_len, timeout_ms,
                             out_received)
          : HAL_EUNSUPPORTED;
  finish_socket_operation(&lease);
  return status;
}

int hal_tcp_socket_recv(hal_tcp_socket_t socket, void *buffer, size_t max_len,
                        uint32_t timeout_ms) {
  size_t received = 0u;
  const hal_status_t status =
      hal_tcp_socket_recv_ex(socket, buffer, max_len, timeout_ms, &received);
  return status == HAL_OK || status == HAL_EAGAIN || status == HAL_ETIMEOUT
             ? (int)received
             : -1;
}

bool hal_tcp_socket_can_recv(hal_tcp_socket_t socket) {
  if (jh_network_require_ready() != HAL_OK) {
    return false;
  }
  jh_network_handle_lease_t lease = {};
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (acquire_socket(socket, &lease) != HAL_OK) {
    return false;
  }
  const bool result = ops != nullptr && ops->socket_can_recv != nullptr &&
                      ops->socket_can_recv(lease.backend_token);
  finish_socket_operation(&lease);
  return result;
}

bool hal_tcp_socket_can_send(hal_tcp_socket_t socket) {
  if (jh_network_require_ready() != HAL_OK) {
    return false;
  }
  jh_network_handle_lease_t lease = {};
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (acquire_socket(socket, &lease) != HAL_OK) {
    return false;
  }
  const bool result = ops != nullptr && ops->socket_can_send != nullptr &&
                      ops->socket_can_send(lease.backend_token);
  finish_socket_operation(&lease);
  return result;
}

bool hal_tcp_socket_is_connected(hal_tcp_socket_t socket) {
  if (jh_network_require_ready() != HAL_OK) {
    return false;
  }
  jh_network_handle_lease_t lease = {};
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (acquire_socket(socket, &lease) != HAL_OK) {
    return false;
  }
  const bool result = ops != nullptr && ops->socket_is_connected != nullptr &&
                      ops->socket_is_connected(lease.backend_token);
  finish_socket_operation(&lease);
  return result;
}

void hal_tcp_socket_shutdown(hal_tcp_socket_t socket) {
  jh_network_handle_lease_t lease = {};
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (acquire_socket(socket, &lease) != HAL_OK) {
    return;
  }
  if (ops != nullptr && ops->socket_shutdown != nullptr) {
    ops->socket_shutdown(lease.backend_token);
  }
  finish_socket_operation(&lease);
}

void hal_tcp_socket_close(hal_tcp_socket_t socket) {
  ensure_pools();
  void *token = nullptr;
  hal_mutex_lock(s_mutex);
  const hal_status_t status = jh_network_handle_begin_close(
      &s_socket_pool, reinterpret_cast<const void *>(socket), &token);
  hal_mutex_unlock(s_mutex);
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (status == HAL_OK && token != nullptr && ops != nullptr &&
      ops->socket_close != nullptr) {
    ops->socket_close(token);
  }
}

hal_status_t hal_tcp_listener_open_ex(hal_tcp_listener_t *out_listener) {
  if (out_listener == nullptr) {
    return HAL_EINVAL;
  }
  *out_listener = nullptr;
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (ops == nullptr || ops->listener_open == nullptr ||
      ops->listener_close == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  void *backend_token = nullptr;
  hal_status_t status = ops->listener_open(&backend_token);
  if (status != HAL_OK) {
    return status;
  }
  ensure_pools();
  void *handle = nullptr;
  hal_mutex_lock(s_mutex);
  status = jh_network_handle_allocate(&s_listener_pool, backend_token, &handle);
  hal_mutex_unlock(s_mutex);
  if (status != HAL_OK) {
    ops->listener_close(backend_token);
    return status;
  }
  *out_listener = reinterpret_cast<hal_tcp_listener_t>(handle);
  return HAL_OK;
}

hal_tcp_listener_t hal_tcp_listener_open(void) {
  hal_tcp_listener_t listener = nullptr;
  (void)hal_tcp_listener_open_ex(&listener);
  return listener;
}

hal_status_t hal_tcp_listener_bind_ex(hal_tcp_listener_t listener,
                                      const hal_net_endpoint_t *local) {
  const hal_status_t endpoint_status = validate_endpoint(local, true);
  if (endpoint_status != HAL_OK) {
    return endpoint_status;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  jh_network_handle_lease_t lease = {};
  if (acquire_listener(listener, &lease) != HAL_OK) {
    return HAL_EINVAL;
  }
  const jh_network_tcp_ops_t *ops = tcp_ops();
  const hal_status_t status =
      ops != nullptr && ops->listener_bind != nullptr
          ? ops->listener_bind(lease.backend_token, local)
          : HAL_EUNSUPPORTED;
  finish_listener_operation(&lease);
  return status;
}

bool hal_tcp_listener_bind(hal_tcp_listener_t listener,
                           const hal_net_endpoint_t *local) {
  return hal_status_to_bool(hal_tcp_listener_bind_ex(listener, local));
}

hal_status_t hal_tcp_listener_listen_ex(hal_tcp_listener_t listener,
                                        uint8_t backlog) {
  if (backlog == 0u) {
    return HAL_EINVAL;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  jh_network_handle_lease_t lease = {};
  if (acquire_listener(listener, &lease) != HAL_OK) {
    return HAL_EINVAL;
  }
  const jh_network_tcp_ops_t *ops = tcp_ops();
  const hal_status_t status =
      ops != nullptr && ops->listener_listen != nullptr
          ? ops->listener_listen(lease.backend_token, backlog)
          : HAL_EUNSUPPORTED;
  finish_listener_operation(&lease);
  return status;
}

bool hal_tcp_listener_listen(hal_tcp_listener_t listener, uint8_t backlog) {
  return hal_status_to_bool(hal_tcp_listener_listen_ex(listener, backlog));
}

hal_status_t hal_tcp_listener_accept_ex(hal_tcp_listener_t listener,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        hal_tcp_socket_t *out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  jh_network_handle_lease_t listener_lease = {};
  if (acquire_listener(listener, &listener_lease) != HAL_OK) {
    return HAL_EINVAL;
  }
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (ops == nullptr || ops->listener_accept == nullptr ||
      ops->socket_close == nullptr) {
    finish_listener_operation(&listener_lease);
    return HAL_EUNSUPPORTED;
  }
  void *socket_token = nullptr;
  hal_status_t status = ops->listener_accept(listener_lease.backend_token,
                                             remote, timeout_ms, &socket_token);
  finish_listener_operation(&listener_lease);
  if (status != HAL_OK) {
    return status;
  }
  ensure_pools();
  void *handle = nullptr;
  hal_mutex_lock(s_mutex);
  status = jh_network_handle_allocate(&s_socket_pool, socket_token, &handle);
  hal_mutex_unlock(s_mutex);
  if (status != HAL_OK) {
    ops->socket_close(socket_token);
    return status;
  }
  *out_socket = reinterpret_cast<hal_tcp_socket_t>(handle);
  return HAL_OK;
}

hal_tcp_socket_t hal_tcp_listener_accept(hal_tcp_listener_t listener,
                                         hal_net_endpoint_t *remote,
                                         uint32_t timeout_ms) {
  hal_tcp_socket_t socket = nullptr;
  (void)hal_tcp_listener_accept_ex(listener, remote, timeout_ms, &socket);
  return socket;
}

bool hal_tcp_listener_can_accept(hal_tcp_listener_t listener) {
  if (jh_network_require_ready() != HAL_OK) {
    return false;
  }
  jh_network_handle_lease_t lease = {};
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (acquire_listener(listener, &lease) != HAL_OK) {
    return false;
  }
  const bool result = ops != nullptr && ops->listener_can_accept != nullptr &&
                      ops->listener_can_accept(lease.backend_token);
  finish_listener_operation(&lease);
  return result;
}

void hal_tcp_listener_close(hal_tcp_listener_t listener) {
  ensure_pools();
  void *token = nullptr;
  hal_mutex_lock(s_mutex);
  const hal_status_t status = jh_network_handle_begin_close(
      &s_listener_pool, reinterpret_cast<const void *>(listener), &token);
  hal_mutex_unlock(s_mutex);
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (status == HAL_OK && token != nullptr && ops != nullptr &&
      ops->listener_close != nullptr) {
    ops->listener_close(token);
  }
}

extern "C" void jh_network_facade_tcp_reset_all(void) {
  ensure_pools();
  void *socket_tokens[HAL_TCP_SOCKET_MAX_INSTANCES] = {};
  void *listener_tokens[HAL_TCP_LISTENER_MAX_INSTANCES] = {};
  size_t socket_count = 0u;
  size_t listener_count = 0u;
  hal_mutex_lock(s_mutex);
  socket_count = jh_network_handle_begin_close_all(
      &s_socket_pool, socket_tokens, HAL_TCP_SOCKET_MAX_INSTANCES);
  listener_count = jh_network_handle_begin_close_all(
      &s_listener_pool, listener_tokens, HAL_TCP_LISTENER_MAX_INSTANCES);
  hal_mutex_unlock(s_mutex);
  const jh_network_tcp_ops_t *ops = tcp_ops();
  if (ops != nullptr) {
    if (ops->socket_close != nullptr) {
      for (size_t index = 0u; index < socket_count; ++index) {
        ops->socket_close(socket_tokens[index]);
      }
    }
    if (ops->listener_close != nullptr) {
      for (size_t index = 0u; index < listener_count; ++index) {
        ops->listener_close(listener_tokens[index]);
      }
    }
  }
}

#endif
