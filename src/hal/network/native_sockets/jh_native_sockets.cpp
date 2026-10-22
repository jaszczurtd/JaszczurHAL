#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) &&                                        \
    (defined(HAL_NETWORK_BACKEND_ESP_IDF) ||                                   \
     defined(HAL_NETWORK_BACKEND_HOST))

#include "hal/core/hal_mutex_once.h"
#include "hal/network/jh_network_backend.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "jh_native_socket_platform.h"
#include "jh_native_sockets.h"

#include <string.h>

namespace {

enum class SocketKind : uint8_t { kUnused, kTcp, kTcpListener, kUdp };

struct NativeSocket {
  jh_native_fd_t descriptor;
  int family;
  SocketKind kind;
  uint32_t generation;
  bool in_use;
};

constexpr size_t kNativeSocketCapacity = HAL_TCP_SOCKET_MAX_INSTANCES +
                                         HAL_TCP_LISTENER_MAX_INSTANCES +
                                         HAL_UDP_SOCKET_MAX_INSTANCES;

NativeSocket s_sockets[kNativeSocketCapacity];
hal_mutex_t s_socket_pool_mutex;
uint32_t s_socket_generation = 1u;

hal_status_t ensure_pool(void) {
  return jh_hal_mutex_create_once(&s_socket_pool_mutex) != nullptr ? HAL_OK
                                                                   : HAL_ENOMEM;
}

hal_status_t status_from_class(jh_native_error_t error, bool timed_operation,
                               uint32_t timeout_ms) {
  switch (error) {
  case JH_NATIVE_ERR_NONE:
    return HAL_OK;
  case JH_NATIVE_ERR_INVALID:
    return HAL_EINVAL;
  case JH_NATIVE_ERR_UNSUPPORTED:
    return HAL_EUNSUPPORTED;
  case JH_NATIVE_ERR_NOMEM:
    return HAL_ENOMEM;
  case JH_NATIVE_ERR_BUSY:
  case JH_NATIVE_ERR_IN_PROGRESS:
    return HAL_EBUSY;
  case JH_NATIVE_ERR_WOULD_BLOCK:
    return timed_operation && timeout_ms != 0u ? HAL_ETIMEOUT : HAL_EAGAIN;
  case JH_NATIVE_ERR_TIMEOUT:
    return HAL_ETIMEOUT;
  case JH_NATIVE_ERR_INTERRUPTED:
  case JH_NATIVE_ERR_IO:
  default:
    return HAL_EIO;
  }
}

hal_status_t status_from_native(int error, bool timed_operation,
                                uint32_t timeout_ms) {
  return status_from_class(jh_native_classify(error), timed_operation,
                           timeout_ms);
}

hal_status_t status_from_last_error(bool timed_operation, uint32_t timeout_ms) {
  return status_from_native(jh_native_last_error(), timed_operation,
                            timeout_ms);
}

NativeSocket *allocate_token(SocketKind kind) {
  if (ensure_pool() != HAL_OK) {
    return nullptr;
  }
  hal_mutex_lock(s_socket_pool_mutex);
  NativeSocket *token = nullptr;
  for (size_t index = 0u; index < kNativeSocketCapacity; ++index) {
    if (!s_sockets[index].in_use) {
      token = &s_sockets[index];
      token->descriptor = JH_NATIVE_FD_INVALID;
      token->family = AF_UNSPEC;
      token->kind = kind;
      token->generation = s_socket_generation;
      token->in_use = true;
      break;
    }
  }
  hal_mutex_unlock(s_socket_pool_mutex);
  return token;
}

bool token_is_member(const NativeSocket *token) {
  if (token == nullptr) {
    return false;
  }
  for (size_t index = 0u; index < kNativeSocketCapacity; ++index) {
    if (token == &s_sockets[index]) {
      return true;
    }
  }
  return false;
}

bool token_is_valid_locked(const NativeSocket *token, SocketKind kind) {
  return token_is_member(token) && token->in_use && token->kind == kind &&
         token->generation == s_socket_generation;
}

bool has_descriptor(const NativeSocket *token) {
  return token->descriptor != JH_NATIVE_FD_INVALID;
}

void release_token(NativeSocket *token, SocketKind kind) {
  if (token == nullptr) {
    return;
  }
  if (ensure_pool() != HAL_OK) {
    return;
  }
  hal_mutex_lock(s_socket_pool_mutex);
  if (token_is_member(token) && token->in_use && token->kind == kind) {
    if (has_descriptor(token)) {
      (void)jh_native_close(token->descriptor);
    }
    token->descriptor = JH_NATIVE_FD_INVALID;
    token->family = AF_UNSPEC;
    token->kind = SocketKind::kUnused;
    token->generation = 0u;
    token->in_use = false;
  }
  hal_mutex_unlock(s_socket_pool_mutex);
}

bool token_is_valid(const NativeSocket *token, SocketKind kind) {
  if (ensure_pool() != HAL_OK) {
    return false;
  }
  hal_mutex_lock(s_socket_pool_mutex);
  const bool valid = token_is_valid_locked(token, kind);
  hal_mutex_unlock(s_socket_pool_mutex);
  return valid;
}

hal_status_t sockaddr_from_endpoint(const hal_net_endpoint_t *endpoint,
                                    sockaddr_storage *storage,
                                    jh_native_socklen_t *out_length) {
  if (endpoint == nullptr || storage == nullptr || out_length == nullptr) {
    return HAL_EINVAL;
  }
  memset(storage, 0, sizeof(*storage));
  if (endpoint->family == HAL_NET_AF_INET &&
      endpoint->addr_len == HAL_NET_IPV4_ADDR_LEN) {
    auto *ipv4 = reinterpret_cast<sockaddr_in *>(storage);
    ipv4->sin_family = AF_INET;
    ipv4->sin_port = htons(endpoint->port);
    memcpy(&ipv4->sin_addr.s_addr, endpoint->addr, HAL_NET_IPV4_ADDR_LEN);
    *out_length = static_cast<jh_native_socklen_t>(sizeof(*ipv4));
    return HAL_OK;
  }
#if JH_NATIVE_HAS_IPV6
  if (endpoint->family == HAL_NET_AF_INET6 &&
      endpoint->addr_len == HAL_NET_IPV6_ADDR_LEN) {
    auto *ipv6 = reinterpret_cast<sockaddr_in6 *>(storage);
    ipv6->sin6_family = AF_INET6;
    ipv6->sin6_port = htons(endpoint->port);
    memcpy(ipv6->sin6_addr.s6_addr, endpoint->addr, HAL_NET_IPV6_ADDR_LEN);
    ipv6->sin6_scope_id = endpoint->scope_id;
    *out_length = static_cast<jh_native_socklen_t>(sizeof(*ipv6));
    return HAL_OK;
  }
#endif
  return HAL_EUNSUPPORTED;
}

hal_status_t endpoint_from_sockaddr(const sockaddr *address,
                                    jh_native_socklen_t address_length,
                                    hal_net_endpoint_t *endpoint) {
  if (address == nullptr || endpoint == nullptr) {
    return HAL_EINVAL;
  }
  memset(endpoint, 0, sizeof(*endpoint));
  if (address->sa_family == AF_INET &&
      address_length >= static_cast<jh_native_socklen_t>(sizeof(sockaddr_in))) {
    const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
    endpoint->family = HAL_NET_AF_INET;
    endpoint->addr_len = HAL_NET_IPV4_ADDR_LEN;
    endpoint->port = ntohs(ipv4->sin_port);
    memcpy(endpoint->addr, &ipv4->sin_addr.s_addr, HAL_NET_IPV4_ADDR_LEN);
    return HAL_OK;
  }
#if JH_NATIVE_HAS_IPV6
  if (address->sa_family == AF_INET6 &&
      address_length >=
          static_cast<jh_native_socklen_t>(sizeof(sockaddr_in6))) {
    const auto *ipv6 = reinterpret_cast<const sockaddr_in6 *>(address);
    endpoint->family = HAL_NET_AF_INET6;
    endpoint->addr_len = HAL_NET_IPV6_ADDR_LEN;
    endpoint->port = ntohs(ipv6->sin6_port);
    endpoint->scope_id = ipv6->sin6_scope_id;
    memcpy(endpoint->addr, ipv6->sin6_addr.s6_addr, HAL_NET_IPV6_ADDR_LEN);
    return HAL_OK;
  }
#endif
  return HAL_EUNSUPPORTED;
}

int native_family(hal_net_family_t family) {
  if (family == HAL_NET_AF_INET) {
    return AF_INET;
  }
#if JH_NATIVE_HAS_IPV6
  if (family == HAL_NET_AF_INET6) {
    return AF_INET6;
  }
#endif
  return AF_UNSPEC;
}

hal_status_t create_descriptor(NativeSocket *token, SocketKind kind, int family,
                               int type, int protocol) {
  if (token == nullptr || family == AF_UNSPEC) {
    return family == AF_UNSPEC ? HAL_EUNSUPPORTED : HAL_EINVAL;
  }
  const hal_status_t pool_status = ensure_pool();
  if (pool_status != HAL_OK) {
    return pool_status;
  }
  hal_mutex_lock(s_socket_pool_mutex);
  if (!token_is_valid_locked(token, kind)) {
    hal_mutex_unlock(s_socket_pool_mutex);
    return HAL_ESTATE;
  }
  if (has_descriptor(token)) {
    const hal_status_t status = token->family == family ? HAL_OK : HAL_ESTATE;
    hal_mutex_unlock(s_socket_pool_mutex);
    return status;
  }
  const jh_native_fd_t descriptor = jh_native_socket(family, type, protocol);
  if (descriptor == JH_NATIVE_FD_INVALID) {
    const hal_status_t status = status_from_last_error(false, 0u);
    hal_mutex_unlock(s_socket_pool_mutex);
    return status;
  }
  if (jh_native_make_nonblocking(descriptor) != 0) {
    const hal_status_t status = status_from_last_error(false, 0u);
    (void)jh_native_close(descriptor);
    hal_mutex_unlock(s_socket_pool_mutex);
    return status;
  }
  token->descriptor = descriptor;
  token->family = family;
  hal_mutex_unlock(s_socket_pool_mutex);
  return HAL_OK;
}

#if defined(HAL_ENABLE_TCP)
hal_status_t adopt_descriptor(NativeSocket *token, SocketKind kind,
                              jh_native_fd_t descriptor, int family) {
  if (token == nullptr || descriptor == JH_NATIVE_FD_INVALID) {
    return HAL_EINVAL;
  }
  const hal_status_t pool_status = ensure_pool();
  if (pool_status != HAL_OK) {
    return pool_status;
  }
  hal_mutex_lock(s_socket_pool_mutex);
  if (!token_is_valid_locked(token, kind) || has_descriptor(token)) {
    hal_mutex_unlock(s_socket_pool_mutex);
    return HAL_ESTATE;
  }
  token->descriptor = descriptor;
  token->family = family;
  hal_mutex_unlock(s_socket_pool_mutex);
  return HAL_OK;
}
#endif

hal_status_t wait_for_descriptor(jh_native_fd_t descriptor, bool read,
                                 bool write, uint32_t timeout_ms) {
  bool failed = false;
  const int status =
      jh_native_wait(descriptor, read, write, timeout_ms, &failed);
  if (status < 0) {
    return status_from_last_error(true, timeout_ms);
  }
  if (status == 0) {
    return timeout_ms == 0u ? HAL_EAGAIN : HAL_ETIMEOUT;
  }
  if (failed) {
    int error = 0;
    if (jh_native_socket_error(descriptor, &error) != 0) {
      return status_from_last_error(false, 0u);
    }
    return error == 0 ? HAL_EIO : status_from_native(error, false, 0u);
  }
  return HAL_OK;
}

// Longest single native wait. Longer waits are split so that a teardown
// (jh_native_sockets_shutdown_all) from another task ends them promptly: the
// stack does not always wake a waiter on shutdown(), lwIP never for UDP.
constexpr uint32_t kWaitSliceMs = 50u;

// Wait for @p token like wait_for_descriptor(), but end with HAL_ECANCELED
// once a teardown has invalidated the handle.
hal_status_t wait_for_token(const NativeSocket *token, SocketKind kind,
                            bool read, bool write, uint32_t timeout_ms) {
  const bool forever = timeout_ms == HAL_NET_TIMEOUT_FOREVER;
  const uint32_t started = hal_millis();
  for (;;) {
    uint32_t slice = kWaitSliceMs;
    if (!forever) {
      const uint32_t waited = hal_millis() - started;
      const uint32_t left = waited < timeout_ms ? timeout_ms - waited : 0u;
      slice = left < kWaitSliceMs ? left : kWaitSliceMs;
    }
    const hal_status_t status =
        wait_for_descriptor(token->descriptor, read, write, slice);
    if (!token_is_valid(token, kind)) {
      return HAL_ECANCELED;
    }
    if (status != HAL_ETIMEOUT && status != HAL_EAGAIN) {
      return status;
    }
    if (!forever && hal_millis_deadline_expired(started, timeout_ms)) {
      return timeout_ms == 0u ? HAL_EAGAIN : HAL_ETIMEOUT;
    }
  }
}

bool descriptor_ready(jh_native_fd_t descriptor, bool read, bool write) {
  return descriptor != JH_NATIVE_FD_INVALID &&
         wait_for_descriptor(descriptor, read, write, 0u) == HAL_OK;
}

hal_status_t open_token(SocketKind kind, void **out_token) {
  if (out_token == nullptr) {
    return HAL_EINVAL;
  }
  *out_token = nullptr;
  const hal_status_t ready = jh_native_sockets_require_ready();
  if (ready != HAL_OK) {
    return ready;
  }
  NativeSocket *token = allocate_token(kind);
  if (token == nullptr) {
    return HAL_ENOMEM;
  }
  const hal_status_t still_ready = jh_native_sockets_require_ready();
  if (still_ready != HAL_OK) {
    release_token(token, kind);
    return still_ready;
  }
  *out_token = token;
  return HAL_OK;
}

hal_status_t bind_token(NativeSocket *token, SocketKind kind,
                        const hal_net_endpoint_t *local, int type,
                        int protocol) {
  if (!token_is_valid(token, kind) || local == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t ready = jh_native_sockets_require_ready();
  if (ready != HAL_OK) {
    return ready;
  }
  sockaddr_storage address = {};
  jh_native_socklen_t address_length = 0;
  hal_status_t status =
      sockaddr_from_endpoint(local, &address, &address_length);
  if (status != HAL_OK) {
    return status;
  }
  status = create_descriptor(token, kind, native_family(local->family), type,
                             protocol);
  if (status != HAL_OK) {
    return status;
  }
  if (kind == SocketKind::kTcpListener) {
    jh_native_prepare_listener(token->descriptor);
  }
  return jh_native_bind(token->descriptor,
                        reinterpret_cast<sockaddr *>(&address),
                        address_length) == 0
             ? HAL_OK
             : status_from_last_error(false, 0u);
}

#if defined(HAL_ENABLE_TCP)
hal_status_t connect_with_timeout(const NativeSocket *token,
                                  const sockaddr *address,
                                  jh_native_socklen_t address_length,
                                  uint32_t timeout_ms) {
  const jh_native_fd_t descriptor = token->descriptor;
  const int status = jh_native_connect(descriptor, address, address_length);
  const int connect_error = status == 0 ? 0 : jh_native_last_error();
  const jh_native_error_t connect_class = jh_native_classify(connect_error);
  hal_status_t result = HAL_OK;
  if (status != 0 && connect_class != JH_NATIVE_ERR_IN_PROGRESS &&
      connect_class != JH_NATIVE_ERR_WOULD_BLOCK) {
    result = status_from_class(connect_class, false, timeout_ms);
  } else if (status != 0) {
    result = wait_for_token(token, SocketKind::kTcp, false, true, timeout_ms);
    if (result == HAL_OK) {
      int socket_error = 0;
      if (jh_native_socket_error(descriptor, &socket_error) != 0) {
        result = status_from_last_error(false, 0u);
      } else if (socket_error != 0) {
        result = status_from_native(socket_error, false, timeout_ms);
      }
    }
  }
  return result;
}

hal_status_t tcp_socket_open(void **out_socket) {
  return open_token(SocketKind::kTcp, out_socket);
}

hal_status_t tcp_socket_connect(void *socket, const hal_net_endpoint_t *remote,
                                uint32_t timeout_ms) {
  auto *token = static_cast<NativeSocket *>(socket);
  if (!token_is_valid(token, SocketKind::kTcp) || remote == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t ready = jh_native_sockets_require_ready();
  if (ready != HAL_OK) {
    return ready;
  }
  sockaddr_storage address = {};
  jh_native_socklen_t address_length = 0;
  hal_status_t status =
      sockaddr_from_endpoint(remote, &address, &address_length);
  if (status != HAL_OK) {
    return status;
  }
  const int family = native_family(remote->family);
  status = create_descriptor(token, SocketKind::kTcp, family, SOCK_STREAM,
                             IPPROTO_TCP);
  return status == HAL_OK
             ? connect_with_timeout(token,
                                    reinterpret_cast<sockaddr *>(&address),
                                    address_length, timeout_ms)
             : status;
}

hal_status_t tcp_socket_send(void *socket, const void *data, size_t length,
                             size_t *out_sent) {
  auto *token = static_cast<NativeSocket *>(socket);
  if (out_sent != nullptr) {
    *out_sent = 0u;
  }
  if (!token_is_valid(token, SocketKind::kTcp) || out_sent == nullptr ||
      (length > 0u && data == nullptr) || !has_descriptor(token)) {
    return HAL_EINVAL;
  }
  if (length == 0u) {
    return HAL_OK;
  }
  const intptr_t sent = jh_native_send(token->descriptor, data, length);
  if (sent < 0) {
    return status_from_last_error(false, 0u);
  }
  *out_sent = static_cast<size_t>(sent);
  return HAL_OK;
}

hal_status_t tcp_socket_recv(void *socket, void *buffer, size_t max_length,
                             uint32_t timeout_ms, size_t *out_received) {
  auto *token = static_cast<NativeSocket *>(socket);
  if (out_received != nullptr) {
    *out_received = 0u;
  }
  if (!token_is_valid(token, SocketKind::kTcp) || out_received == nullptr ||
      (max_length > 0u && buffer == nullptr) || !has_descriptor(token)) {
    return HAL_EINVAL;
  }
  if (max_length == 0u) {
    return HAL_OK;
  }
  const hal_status_t ready =
      wait_for_token(token, SocketKind::kTcp, true, false, timeout_ms);
  if (ready != HAL_OK) {
    return ready;
  }
  const intptr_t received =
      jh_native_recv(token->descriptor, buffer, max_length);
  if (received < 0) {
    return status_from_last_error(true, timeout_ms);
  }
  *out_received = static_cast<size_t>(received);
  return HAL_OK;
}

bool tcp_socket_can_recv(void *socket) {
  auto *token = static_cast<NativeSocket *>(socket);
  return token_is_valid(token, SocketKind::kTcp) &&
         descriptor_ready(token->descriptor, true, false);
}

bool tcp_socket_can_send(void *socket) {
  auto *token = static_cast<NativeSocket *>(socket);
  return token_is_valid(token, SocketKind::kTcp) &&
         descriptor_ready(token->descriptor, false, true);
}

bool tcp_socket_is_connected(void *socket) {
  auto *token = static_cast<NativeSocket *>(socket);
  if (!token_is_valid(token, SocketKind::kTcp) || !has_descriptor(token)) {
    return false;
  }
  sockaddr_storage peer = {};
  jh_native_socklen_t size = static_cast<jh_native_socklen_t>(sizeof(peer));
  return jh_native_getpeername(token->descriptor,
                               reinterpret_cast<sockaddr *>(&peer), &size) == 0;
}

void tcp_socket_shutdown(void *socket) {
  auto *token = static_cast<NativeSocket *>(socket);
  if (token_is_valid(token, SocketKind::kTcp) && has_descriptor(token)) {
    jh_native_shutdown(token->descriptor);
  }
}

void tcp_socket_close(void *socket) {
  auto *token = static_cast<NativeSocket *>(socket);
  release_token(token, SocketKind::kTcp);
}

hal_status_t tcp_listener_open(void **out_listener) {
  return open_token(SocketKind::kTcpListener, out_listener);
}

hal_status_t tcp_listener_bind(void *listener,
                               const hal_net_endpoint_t *local) {
  return bind_token(static_cast<NativeSocket *>(listener),
                    SocketKind::kTcpListener, local, SOCK_STREAM, IPPROTO_TCP);
}

hal_status_t tcp_listener_listen(void *listener, uint8_t backlog) {
  auto *token = static_cast<NativeSocket *>(listener);
  if (!token_is_valid(token, SocketKind::kTcpListener) ||
      !has_descriptor(token) || backlog == 0u) {
    return HAL_EINVAL;
  }
  const int capped = backlog > HAL_TCP_LISTENER_BACKLOG_MAX
                         ? static_cast<int>(HAL_TCP_LISTENER_BACKLOG_MAX)
                         : static_cast<int>(backlog);
  return jh_native_listen(token->descriptor, capped) == 0
             ? HAL_OK
             : status_from_last_error(false, 0u);
}

hal_status_t tcp_listener_accept(void *listener, hal_net_endpoint_t *remote,
                                 uint32_t timeout_ms, void **out_socket) {
  auto *token = static_cast<NativeSocket *>(listener);
  if (out_socket != nullptr) {
    *out_socket = nullptr;
  }
  if (!token_is_valid(token, SocketKind::kTcpListener) ||
      !has_descriptor(token) || out_socket == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t ready =
      wait_for_token(token, SocketKind::kTcpListener, true, false, timeout_ms);
  if (ready != HAL_OK) {
    return ready;
  }
  sockaddr_storage address = {};
  jh_native_socklen_t address_length =
      static_cast<jh_native_socklen_t>(sizeof(address));
  const jh_native_fd_t descriptor =
      jh_native_accept(token->descriptor,
                       reinterpret_cast<sockaddr *>(&address), &address_length);
  if (descriptor == JH_NATIVE_FD_INVALID) {
    return status_from_last_error(true, timeout_ms);
  }
  if (jh_native_make_nonblocking(descriptor) != 0) {
    const hal_status_t status = status_from_last_error(false, 0u);
    (void)jh_native_close(descriptor);
    return status;
  }
  const hal_status_t network_ready = jh_native_sockets_require_ready();
  if (network_ready != HAL_OK) {
    (void)jh_native_close(descriptor);
    return network_ready;
  }
  NativeSocket *accepted = allocate_token(SocketKind::kTcp);
  if (accepted == nullptr) {
    (void)jh_native_close(descriptor);
    return HAL_ENOMEM;
  }
  const hal_status_t adopt_status = adopt_descriptor(
      accepted, SocketKind::kTcp, descriptor, address.ss_family);
  if (adopt_status != HAL_OK) {
    (void)jh_native_close(descriptor);
    release_token(accepted, SocketKind::kTcp);
    return adopt_status;
  }
  if (remote != nullptr) {
    const hal_status_t endpoint_status = endpoint_from_sockaddr(
        reinterpret_cast<sockaddr *>(&address), address_length, remote);
    if (endpoint_status != HAL_OK) {
      release_token(accepted, SocketKind::kTcp);
      return endpoint_status;
    }
  }
  *out_socket = accepted;
  return HAL_OK;
}

bool tcp_listener_can_accept(void *listener) {
  auto *token = static_cast<NativeSocket *>(listener);
  return token_is_valid(token, SocketKind::kTcpListener) &&
         descriptor_ready(token->descriptor, true, false);
}

void tcp_listener_close(void *listener) {
  auto *token = static_cast<NativeSocket *>(listener);
  release_token(token, SocketKind::kTcpListener);
}
#endif

#if defined(HAL_ENABLE_UDP)
hal_status_t udp_socket_open(void **out_socket) {
  return open_token(SocketKind::kUdp, out_socket);
}

hal_status_t udp_socket_bind(void *socket, const hal_net_endpoint_t *local) {
  return bind_token(static_cast<NativeSocket *>(socket), SocketKind::kUdp,
                    local, SOCK_DGRAM, IPPROTO_UDP);
}

hal_status_t udp_socket_sendto(void *socket, const void *data, size_t length,
                               const hal_net_endpoint_t *remote,
                               size_t *out_sent) {
  auto *token = static_cast<NativeSocket *>(socket);
  if (out_sent != nullptr) {
    *out_sent = 0u;
  }
  if (!token_is_valid(token, SocketKind::kUdp) || out_sent == nullptr ||
      (length > 0u && data == nullptr) || remote == nullptr ||
      !has_descriptor(token)) {
    return HAL_EINVAL;
  }
  sockaddr_storage address = {};
  jh_native_socklen_t address_length = 0;
  const hal_status_t status =
      sockaddr_from_endpoint(remote, &address, &address_length);
  if (status != HAL_OK) {
    return status;
  }
  if (native_family(remote->family) != token->family) {
    return HAL_EUNSUPPORTED;
  }
  const intptr_t sent =
      jh_native_sendto(token->descriptor, data, length,
                       reinterpret_cast<sockaddr *>(&address), address_length);
  if (sent < 0) {
    return status_from_last_error(false, 0u);
  }
  *out_sent = static_cast<size_t>(sent);
  return HAL_OK;
}

hal_status_t udp_socket_recvfrom(void *socket, void *buffer, size_t max_length,
                                 hal_net_endpoint_t *remote,
                                 uint32_t timeout_ms, size_t *out_received) {
  auto *token = static_cast<NativeSocket *>(socket);
  if (out_received != nullptr) {
    *out_received = 0u;
  }
  if (!token_is_valid(token, SocketKind::kUdp) || out_received == nullptr ||
      (max_length > 0u && buffer == nullptr) || !has_descriptor(token)) {
    return HAL_EINVAL;
  }
  if (max_length == 0u) {
    return HAL_OK;
  }
  const hal_status_t ready =
      wait_for_token(token, SocketKind::kUdp, true, false, timeout_ms);
  if (ready != HAL_OK) {
    return ready;
  }
  sockaddr_storage address = {};
  jh_native_socklen_t address_length =
      static_cast<jh_native_socklen_t>(sizeof(address));
  const intptr_t received = jh_native_recvfrom(
      token->descriptor, buffer, max_length,
      reinterpret_cast<sockaddr *>(&address), &address_length);
  if (received < 0) {
    return status_from_last_error(true, timeout_ms);
  }
  if (remote != nullptr) {
    const hal_status_t status = endpoint_from_sockaddr(
        reinterpret_cast<sockaddr *>(&address), address_length, remote);
    if (status != HAL_OK) {
      return status;
    }
  }
  *out_received = static_cast<size_t>(received);
  return HAL_OK;
}

bool udp_socket_can_recv(void *socket) {
  auto *token = static_cast<NativeSocket *>(socket);
  return token_is_valid(token, SocketKind::kUdp) &&
         descriptor_ready(token->descriptor, true, false);
}

bool udp_socket_can_send(void *socket) {
  auto *token = static_cast<NativeSocket *>(socket);
  return token_is_valid(token, SocketKind::kUdp) && has_descriptor(token) &&
         descriptor_ready(token->descriptor, false, true);
}

void udp_socket_close(void *socket) {
  auto *token = static_cast<NativeSocket *>(socket);
  release_token(token, SocketKind::kUdp);
}
#endif

#if defined(HAL_ENABLE_TCP)
const jh_network_tcp_ops_t s_tcp_ops = {
    tcp_socket_open,         tcp_socket_connect,      tcp_socket_send,
    tcp_socket_recv,         tcp_socket_can_recv,     tcp_socket_can_send,
    tcp_socket_is_connected, tcp_socket_shutdown,     tcp_socket_close,
    tcp_listener_open,       tcp_listener_bind,       tcp_listener_listen,
    tcp_listener_accept,     tcp_listener_can_accept, tcp_listener_close,
};
#endif

#if defined(HAL_ENABLE_UDP)
const jh_network_udp_ops_t s_udp_ops = {
    udp_socket_open,     udp_socket_bind,     udp_socket_sendto,
    udp_socket_recvfrom, udp_socket_can_recv, udp_socket_can_send,
    udp_socket_close,
};
#endif

bool resolvable_family(int family) {
  return family == AF_INET
#if JH_NATIVE_HAS_IPV6
         || family == AF_INET6
#endif
      ;
}

hal_status_t status_from_resolver(int resolve_status) {
  if (resolve_status == EAI_AGAIN) {
    return HAL_EAGAIN;
  }
  if (resolve_status == EAI_MEMORY) {
    return HAL_ENOMEM;
  }
  if (resolve_status == EAI_NONAME) {
    return HAL_ENOENT;
  }
#if defined(EAI_NODATA)
  if (resolve_status == EAI_NODATA) {
    return HAL_ENOENT;
  }
#endif
  if (resolve_status == EAI_FAMILY) {
    return HAL_EUNSUPPORTED;
  }
  return HAL_EIO;
}

} // namespace

extern "C" hal_status_t jh_native_sockets_initialize(void) {
  return ensure_pool();
}

extern "C" hal_status_t jh_native_sockets_shutdown_all(void) {
  const hal_status_t pool_status = ensure_pool();
  if (pool_status != HAL_OK) {
    return pool_status;
  }
  hal_mutex_lock(s_socket_pool_mutex);
  ++s_socket_generation;
  if (s_socket_generation == 0u) {
    s_socket_generation = 1u;
    for (size_t index = 0u; index < kNativeSocketCapacity; ++index) {
      if (s_sockets[index].in_use) {
        s_sockets[index].generation = 0u;
      }
    }
  }
  for (size_t index = 0u; index < kNativeSocketCapacity; ++index) {
    if (s_sockets[index].in_use && has_descriptor(&s_sockets[index])) {
      jh_native_shutdown(s_sockets[index].descriptor);
    }
  }
  hal_mutex_unlock(s_socket_pool_mutex);
  return HAL_OK;
}

#if defined(HAL_ENABLE_TCP)
extern "C" const jh_network_tcp_ops_t *jh_native_sockets_tcp_ops(void) {
  return &s_tcp_ops;
}
#endif

#if defined(HAL_ENABLE_UDP)
extern "C" const jh_network_udp_ops_t *jh_native_sockets_udp_ops(void) {
  return &s_udp_ops;
}
#endif

extern "C" hal_status_t jh_native_sockets_resolve_validate(
    const char *hostname, hal_net_family_t family_hint,
    const hal_net_endpoint_t *results, size_t capacity, size_t *out_count) {
  if (out_count != nullptr) {
    *out_count = 0u;
  }
  if (hostname == nullptr || hostname[0] == '\0' || out_count == nullptr ||
      (capacity > 0u && results == nullptr)) {
    return HAL_EINVAL;
  }
  if (family_hint != HAL_NET_AF_UNSPEC && family_hint != HAL_NET_AF_INET &&
      family_hint != HAL_NET_AF_INET6) {
    return HAL_EINVAL;
  }
#if !JH_NATIVE_HAS_IPV6
  if (family_hint == HAL_NET_AF_INET6) {
    return HAL_EUNSUPPORTED;
  }
#endif
  return HAL_OK;
}

extern "C" hal_status_t jh_native_sockets_resolve_lookup(
    const char *hostname, hal_net_family_t family_hint,
    hal_net_endpoint_t *results, size_t capacity, size_t *out_count) {
  *out_count = 0u;
  addrinfo hints = {};
  hints.ai_family = family_hint == HAL_NET_AF_INET    ? AF_INET
                    : family_hint == HAL_NET_AF_INET6 ? AF_INET6
                                                      : AF_UNSPEC;
  // One entry per address: without a socket type, getaddrinfo() of a host C
  // library repeats every address for each of its socket types.
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *addresses = nullptr;
  const int resolve_status =
      jh_native_getaddrinfo(hostname, &hints, &addresses);
  if (resolve_status != 0) {
    return status_from_resolver(resolve_status);
  }

  size_t count = 0u;
  for (const addrinfo *current = addresses; current != nullptr;
       current = current->ai_next) {
    if (resolvable_family(current->ai_family)) {
      ++count;
    }
  }
  *out_count = count;
  if (count > capacity) {
    jh_native_freeaddrinfo(addresses);
    return HAL_EOVERFLOW;
  }

  size_t index = 0u;
  hal_status_t status = HAL_OK;
  for (const addrinfo *current = addresses; current != nullptr && index < count;
       current = current->ai_next) {
    if (!resolvable_family(current->ai_family)) {
      continue;
    }
    status = endpoint_from_sockaddr(
        current->ai_addr, static_cast<jh_native_socklen_t>(current->ai_addrlen),
        &results[index]);
    if (status != HAL_OK) {
      break;
    }
    results[index].port = 0u;
    ++index;
  }
  jh_native_freeaddrinfo(addresses);
  return status;
}

#endif
