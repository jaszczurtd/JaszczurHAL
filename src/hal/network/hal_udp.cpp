#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"

#if defined(HAL_ENABLE_UDP) && (defined(HAL_NETWORK_BACKEND_CYW43) ||          \
                                defined(HAL_NETWORK_BACKEND_ESP_IDF))

#include "hal/core/hal_mutex_once.h"
#include "hal/network/hal_udp.h"
#include "hal/network/jh_net_address_utils.h"
#include "hal/network/jh_net_validation.h"
#include "hal/network/jh_network_backend.h"
#include "hal/network/jh_network_handle_pool.h"
#include "hal/network/jh_network_runtime.h"
#include "hal/system/hal_sync.h"

static jh_network_handle_slot_t s_slots[HAL_UDP_SOCKET_MAX_INSTANCES];
static jh_network_handle_pool_t s_pool = {};
static hal_mutex_t s_mutex = NULL;
static bool s_initialized = false;

static void ensure_pool(void) {
  (void)jh_hal_mutex_create_once(&s_mutex);
  hal_mutex_lock(s_mutex);
  if (!s_initialized) {
    (void)jh_network_handle_pool_init(&s_pool, s_slots,
                                      HAL_UDP_SOCKET_MAX_INSTANCES, 3u);
    s_initialized = true;
  }
  hal_mutex_unlock(s_mutex);
}

static const jh_network_udp_ops_t *udp_ops(void) {
  const jh_network_backend_descriptor_t *backend =
      jh_network_backend_selected();
  return jh_network_backend_validate(backend, JH_NET_CAP_UDP) == HAL_OK
             ? backend->udp
             : nullptr;
}

static hal_status_t acquire_socket(hal_udp_socket_t socket,
                                   jh_network_handle_lease_t *out_lease) {
  ensure_pool();
  hal_mutex_lock(s_mutex);
  const hal_status_t status = jh_network_handle_acquire(
      &s_pool, reinterpret_cast<const void *>(socket), out_lease);
  hal_mutex_unlock(s_mutex);
  return status;
}

static void finish_socket_operation(jh_network_handle_lease_t *lease) {
  void *deferred_token = nullptr;
  hal_mutex_lock(s_mutex);
  (void)jh_network_handle_end_operation(&s_pool, lease, &deferred_token);
  hal_mutex_unlock(s_mutex);
  const jh_network_udp_ops_t *ops = udp_ops();
  if (deferred_token != nullptr && ops != nullptr &&
      ops->socket_close != nullptr) {
    ops->socket_close(deferred_token);
  }
}

static hal_status_t validate_endpoint(const hal_net_endpoint_t *endpoint,
                                      bool allow_unspecified) {
  return jh_net_validate_supported_endpoint(endpoint, allow_unspecified);
}

hal_status_t hal_udp_socket_open_ex(hal_udp_socket_t *out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  const jh_network_udp_ops_t *ops = udp_ops();
  if (ops == nullptr || ops->socket_open == nullptr ||
      ops->socket_close == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  void *backend_token = nullptr;
  hal_status_t status = ops->socket_open(&backend_token);
  if (status != HAL_OK) {
    return status;
  }
  ensure_pool();
  void *handle = nullptr;
  hal_mutex_lock(s_mutex);
  status = jh_network_handle_allocate(&s_pool, backend_token, &handle);
  hal_mutex_unlock(s_mutex);
  if (status != HAL_OK) {
    ops->socket_close(backend_token);
    return status;
  }
  *out_socket = reinterpret_cast<hal_udp_socket_t>(handle);
  return HAL_OK;
}

hal_udp_socket_t hal_udp_socket_open(void) {
  hal_udp_socket_t socket = nullptr;
  (void)hal_udp_socket_open_ex(&socket);
  return socket;
}

hal_status_t hal_udp_socket_bind_ex(hal_udp_socket_t socket,
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
  if (acquire_socket(socket, &lease) != HAL_OK) {
    return HAL_EINVAL;
  }
  const jh_network_udp_ops_t *ops = udp_ops();
  const hal_status_t status = ops != nullptr && ops->socket_bind != nullptr
                                  ? ops->socket_bind(lease.backend_token, local)
                                  : HAL_EUNSUPPORTED;
  finish_socket_operation(&lease);
  return status;
}

bool hal_udp_socket_bind(hal_udp_socket_t socket,
                         const hal_net_endpoint_t *local) {
  return hal_status_to_bool(hal_udp_socket_bind_ex(socket, local));
}

hal_status_t hal_udp_socket_sendto_ex(hal_udp_socket_t socket, const void *data,
                                      size_t len,
                                      const hal_net_endpoint_t *remote,
                                      size_t *out_sent) {
  if (out_sent != nullptr) {
    *out_sent = 0u;
  }
  if (out_sent == nullptr || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
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
  const jh_network_udp_ops_t *ops = udp_ops();
  const hal_status_t status =
      ops != nullptr && ops->socket_sendto != nullptr
          ? ops->socket_sendto(lease.backend_token, data, len, remote, out_sent)
          : HAL_EUNSUPPORTED;
  finish_socket_operation(&lease);
  return status;
}

int hal_udp_socket_sendto(hal_udp_socket_t socket, const void *data, size_t len,
                          const hal_net_endpoint_t *remote) {
  size_t sent = 0u;
  return hal_udp_socket_sendto_ex(socket, data, len, remote, &sent) == HAL_OK
             ? (int)sent
             : -1;
}

hal_status_t hal_udp_socket_recvfrom_ex(hal_udp_socket_t socket, void *buffer,
                                        size_t max_len,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
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
  const jh_network_udp_ops_t *ops = udp_ops();
  const hal_status_t status =
      ops != nullptr && ops->socket_recvfrom != nullptr
          ? ops->socket_recvfrom(lease.backend_token, buffer, max_len, remote,
                                 timeout_ms, out_received)
          : HAL_EUNSUPPORTED;
  finish_socket_operation(&lease);
  return status;
}

int hal_udp_socket_recvfrom(hal_udp_socket_t socket, void *buffer,
                            size_t max_len, hal_net_endpoint_t *remote,
                            uint32_t timeout_ms) {
  size_t received = 0u;
  const hal_status_t status = hal_udp_socket_recvfrom_ex(
      socket, buffer, max_len, remote, timeout_ms, &received);
  return status == HAL_OK || status == HAL_EAGAIN || status == HAL_ETIMEOUT
             ? (int)received
             : -1;
}

bool hal_udp_socket_can_recv(hal_udp_socket_t socket) {
  if (jh_network_require_ready() != HAL_OK) {
    return false;
  }
  jh_network_handle_lease_t lease = {};
  const jh_network_udp_ops_t *ops = udp_ops();
  if (acquire_socket(socket, &lease) != HAL_OK) {
    return false;
  }
  const bool result = ops != nullptr && ops->socket_can_recv != nullptr &&
                      ops->socket_can_recv(lease.backend_token);
  finish_socket_operation(&lease);
  return result;
}

bool hal_udp_socket_can_send(hal_udp_socket_t socket) {
  if (jh_network_require_ready() != HAL_OK) {
    return false;
  }
  jh_network_handle_lease_t lease = {};
  const jh_network_udp_ops_t *ops = udp_ops();
  if (acquire_socket(socket, &lease) != HAL_OK) {
    return false;
  }
  const bool result = ops != nullptr && ops->socket_can_send != nullptr &&
                      ops->socket_can_send(lease.backend_token);
  finish_socket_operation(&lease);
  return result;
}

void hal_udp_socket_close(hal_udp_socket_t socket) {
  ensure_pool();
  void *token = nullptr;
  hal_mutex_lock(s_mutex);
  const hal_status_t status = jh_network_handle_begin_close(
      &s_pool, reinterpret_cast<const void *>(socket), &token);
  hal_mutex_unlock(s_mutex);
  const jh_network_udp_ops_t *ops = udp_ops();
  if (status == HAL_OK && token != nullptr && ops != nullptr &&
      ops->socket_close != nullptr) {
    ops->socket_close(token);
  }
}

extern "C" void jh_network_facade_udp_reset_all(void) {
  ensure_pool();
  void *tokens[HAL_UDP_SOCKET_MAX_INSTANCES] = {};
  size_t count = 0u;
  hal_mutex_lock(s_mutex);
  count = jh_network_handle_begin_close_all(&s_pool, tokens,
                                            HAL_UDP_SOCKET_MAX_INSTANCES);
  hal_mutex_unlock(s_mutex);
  const jh_network_udp_ops_t *ops = udp_ops();
  if (ops != nullptr && ops->socket_close != nullptr) {
    for (size_t index = 0u; index < count; ++index) {
      ops->socket_close(tokens[index]);
    }
  }
}

#endif

#if defined(HAL_ENABLE_UDP) && (HAL_TARGET_IS_MOCK || HAL_TARGET_IS_RP) &&     \
    !defined(JH_HAL_UDP_SOCKET_API_ONLY)

#include "hal/network/hal_udp.h"
#include "hal/serial/hal_serial.h"

#include <limits.h>
#include <string.h>

uint16_t hal_udp_write(const uint8_t *data, uint16_t len) {
  uint16_t written = 0u;
  (void)hal_udp_write_ex(data, len, &written);
  return written;
}

hal_status_t hal_udp_write_str_ex(const char *text, uint16_t *out_written) {
  if (text == NULL) {
    hal_derr("hal_udp_write_str: text is NULL");
    return HAL_EINVAL;
  }
  if (out_written == NULL) {
    return HAL_EINVAL;
  }
  const size_t len = strlen(text);
  if (len > UINT16_MAX) {
    *out_written = 0u;
    return HAL_EOVERFLOW;
  }
  return hal_udp_write_ex((const uint8_t *)text, (uint16_t)len, out_written);
}

uint16_t hal_udp_write_str(const char *text) {
  uint16_t written = 0u;
  (void)hal_udp_write_str_ex(text, &written);
  return written;
}

#endif
