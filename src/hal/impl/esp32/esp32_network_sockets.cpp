#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_ESP32_FAMILY
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_ESP_IDF)

#include "hal/core/hal_mutex_once.h"
#include "hal/network/jh_network_backend.h"
#include "hal/system/hal_sync.h"
#include "jh_esp32_network.h"

#include <lwip/errno.h>
#include <lwip/sockets.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

namespace {

enum class SocketKind : uint8_t { kUnused, kTcp, kTcpListener, kUdp };

struct NativeSocket {
  int descriptor;
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

hal_status_t status_from_errno(int error, bool timed_operation,
                               uint32_t timeout_ms) {
  switch (error) {
  case 0:
    return HAL_OK;
  case EINVAL:
  case EBADF:
  case ENOTSOCK:
    return HAL_EINVAL;
  case EAFNOSUPPORT:
  case EPROTONOSUPPORT:
  case EOPNOTSUPP:
    return HAL_EUNSUPPORTED;
  case ENOMEM:
  case ENOBUFS:
  case EMFILE:
  case ENFILE:
    return HAL_ENOMEM;
  case EADDRINUSE:
  case EALREADY:
  case EINPROGRESS:
    return HAL_EBUSY;
  case EAGAIN:
#if EWOULDBLOCK != EAGAIN
  case EWOULDBLOCK:
#endif
    return timed_operation && timeout_ms != 0u ? HAL_ETIMEOUT : HAL_EAGAIN;
  case ETIMEDOUT:
    return HAL_ETIMEOUT;
  case ENOTCONN:
  case EPIPE:
  case ECONNABORTED:
  case ECONNRESET:
  case ECONNREFUSED:
  case ENETDOWN:
  case ENETUNREACH:
  case EHOSTUNREACH:
    return HAL_EIO;
  default:
    return HAL_EIO;
  }
}

hal_status_t status_from_last_errno(bool timed_operation, uint32_t timeout_ms) {
  return status_from_errno(errno, timed_operation, timeout_ms);
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
      token->descriptor = -1;
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

void release_token(NativeSocket *token, SocketKind kind) {
  if (token == nullptr) {
    return;
  }
  if (ensure_pool() != HAL_OK) {
    return;
  }
  hal_mutex_lock(s_socket_pool_mutex);
  if (token_is_member(token) && token->in_use && token->kind == kind) {
    if (token->descriptor >= 0) {
      (void)lwip_close(token->descriptor);
    }
    token->descriptor = -1;
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
                                    socklen_t *out_length) {
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
    *out_length = sizeof(*ipv4);
    return HAL_OK;
  }
#if LWIP_IPV6
  if (endpoint->family == HAL_NET_AF_INET6 &&
      endpoint->addr_len == HAL_NET_IPV6_ADDR_LEN) {
    auto *ipv6 = reinterpret_cast<sockaddr_in6 *>(storage);
    ipv6->sin6_family = AF_INET6;
    ipv6->sin6_port = htons(endpoint->port);
    memcpy(ipv6->sin6_addr.s6_addr, endpoint->addr, HAL_NET_IPV6_ADDR_LEN);
    ipv6->sin6_scope_id = endpoint->scope_id;
    *out_length = sizeof(*ipv6);
    return HAL_OK;
  }
#endif
  return HAL_EUNSUPPORTED;
}

hal_status_t endpoint_from_sockaddr(const sockaddr *address,
                                    socklen_t address_length,
                                    hal_net_endpoint_t *endpoint) {
  if (address == nullptr || endpoint == nullptr) {
    return HAL_EINVAL;
  }
  memset(endpoint, 0, sizeof(*endpoint));
  if (address->sa_family == AF_INET &&
      address_length >= static_cast<socklen_t>(sizeof(sockaddr_in))) {
    const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
    endpoint->family = HAL_NET_AF_INET;
    endpoint->addr_len = HAL_NET_IPV4_ADDR_LEN;
    endpoint->port = ntohs(ipv4->sin_port);
    memcpy(endpoint->addr, &ipv4->sin_addr.s_addr, HAL_NET_IPV4_ADDR_LEN);
    return HAL_OK;
  }
#if LWIP_IPV6
  if (address->sa_family == AF_INET6 &&
      address_length >= static_cast<socklen_t>(sizeof(sockaddr_in6))) {
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
#if LWIP_IPV6
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
  if (token->descriptor >= 0) {
    const hal_status_t status = token->family == family ? HAL_OK : HAL_ESTATE;
    hal_mutex_unlock(s_socket_pool_mutex);
    return status;
  }
  const int descriptor = lwip_socket(family, type, protocol);
  if (descriptor < 0) {
    const hal_status_t status = status_from_last_errno(false, 0u);
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
                              int descriptor, int family) {
  if (token == nullptr || descriptor < 0) {
    return HAL_EINVAL;
  }
  const hal_status_t pool_status = ensure_pool();
  if (pool_status != HAL_OK) {
    return pool_status;
  }
  hal_mutex_lock(s_socket_pool_mutex);
  if (!token_is_valid_locked(token, kind) || token->descriptor >= 0) {
    hal_mutex_unlock(s_socket_pool_mutex);
    return HAL_ESTATE;
  }
  token->descriptor = descriptor;
  token->family = family;
  hal_mutex_unlock(s_socket_pool_mutex);
  return HAL_OK;
}
#endif

timeval timeout_to_timeval(uint32_t timeout_ms) {
  timeval timeout = {};
  timeout.tv_sec = static_cast<long>(timeout_ms / 1000u);
  timeout.tv_usec = static_cast<long>((timeout_ms % 1000u) * 1000u);
  return timeout;
}

hal_status_t wait_for_descriptor(int descriptor, bool read, bool write,
                                 uint32_t timeout_ms) {
  fd_set read_set;
  fd_set write_set;
  fd_set error_set;
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  FD_ZERO(&error_set);
  if (read) {
    FD_SET(descriptor, &read_set);
  }
  if (write) {
    FD_SET(descriptor, &write_set);
  }
  FD_SET(descriptor, &error_set);
  timeval timeout = timeout_to_timeval(timeout_ms);
  timeval *timeout_pointer =
      timeout_ms == HAL_NET_TIMEOUT_FOREVER ? nullptr : &timeout;
  const int status =
      lwip_select(descriptor + 1, read ? &read_set : nullptr,
                  write ? &write_set : nullptr, &error_set, timeout_pointer);
  if (status < 0) {
    return status_from_last_errno(true, timeout_ms);
  }
  if (status == 0) {
    return timeout_ms == 0u ? HAL_EAGAIN : HAL_ETIMEOUT;
  }
  if (FD_ISSET(descriptor, &error_set)) {
    int error = 0;
    socklen_t size = sizeof(error);
    if (lwip_getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &error, &size) < 0) {
      return status_from_last_errno(false, 0u);
    }
    return status_from_errno(error == 0 ? EIO : error, false, 0u);
  }
  return HAL_OK;
}

bool descriptor_ready(int descriptor, bool read, bool write) {
  return descriptor >= 0 &&
         wait_for_descriptor(descriptor, read, write, 0u) == HAL_OK;
}

#if defined(HAL_ENABLE_TCP)
hal_status_t connect_with_timeout(int descriptor, const sockaddr *address,
                                  socklen_t address_length,
                                  uint32_t timeout_ms) {
  const int original_flags = lwip_fcntl(descriptor, F_GETFL, 0);
  if (original_flags < 0 ||
      lwip_fcntl(descriptor, F_SETFL, original_flags | O_NONBLOCK) < 0) {
    return status_from_last_errno(false, 0u);
  }

  int status = lwip_connect(descriptor, address, address_length);
  int connect_error = status == 0 ? 0 : errno;
  hal_status_t result = HAL_OK;
  if (status != 0 && connect_error != EINPROGRESS &&
      connect_error != EALREADY && connect_error != EAGAIN
#if EWOULDBLOCK != EAGAIN
      && connect_error != EWOULDBLOCK
#endif
  ) {
    result = status_from_errno(connect_error, false, timeout_ms);
  } else if (status != 0) {
    result = wait_for_descriptor(descriptor, false, true, timeout_ms);
    if (result == HAL_OK) {
      int socket_error = 0;
      socklen_t size = sizeof(socket_error);
      if (lwip_getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &socket_error,
                          &size) < 0) {
        result = status_from_last_errno(false, 0u);
      } else if (socket_error != 0) {
        result = status_from_errno(socket_error, false, timeout_ms);
      }
    }
  }
  const int restore_status = lwip_fcntl(descriptor, F_SETFL, original_flags);
  return result == HAL_OK && restore_status < 0
             ? status_from_last_errno(false, 0u)
             : result;
}

hal_status_t tcp_socket_open(void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  const hal_status_t ready = jh_esp32_network_require_ready();
  if (ready != HAL_OK) {
    return ready;
  }
  NativeSocket *token = allocate_token(SocketKind::kTcp);
  if (token == nullptr) {
    return HAL_ENOMEM;
  }
  const hal_status_t still_ready = jh_esp32_network_require_ready();
  if (still_ready != HAL_OK) {
    release_token(token, SocketKind::kTcp);
    return still_ready;
  }
  *out_socket = token;
  return HAL_OK;
}

hal_status_t tcp_socket_connect(void *socket, const hal_net_endpoint_t *remote,
                                uint32_t timeout_ms) {
  auto *token = static_cast<NativeSocket *>(socket);
  if (!token_is_valid(token, SocketKind::kTcp) || remote == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t ready = jh_esp32_network_require_ready();
  if (ready != HAL_OK) {
    return ready;
  }
  sockaddr_storage address = {};
  socklen_t address_length = 0u;
  hal_status_t status =
      sockaddr_from_endpoint(remote, &address, &address_length);
  if (status != HAL_OK) {
    return status;
  }
  const int family = native_family(remote->family);
  status = create_descriptor(token, SocketKind::kTcp, family, SOCK_STREAM,
                             IPPROTO_TCP);
  return status == HAL_OK
             ? connect_with_timeout(token->descriptor,
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
      (length > 0u && data == nullptr) || token->descriptor < 0) {
    return HAL_EINVAL;
  }
  if (length == 0u) {
    return HAL_OK;
  }
#ifdef MSG_NOSIGNAL
  constexpr int kSendFlags = MSG_NOSIGNAL;
#else
  constexpr int kSendFlags = 0;
#endif
  const ssize_t sent = lwip_send(token->descriptor, data, length, kSendFlags);
  if (sent < 0) {
    return status_from_last_errno(false, 0u);
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
      (max_length > 0u && buffer == nullptr) || token->descriptor < 0) {
    return HAL_EINVAL;
  }
  if (max_length == 0u) {
    return HAL_OK;
  }
  const hal_status_t ready =
      wait_for_descriptor(token->descriptor, true, false, timeout_ms);
  if (ready != HAL_OK) {
    return ready;
  }
  const ssize_t received =
      lwip_recv(token->descriptor, buffer, max_length, MSG_DONTWAIT);
  if (received < 0) {
    return status_from_last_errno(true, timeout_ms);
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
  if (!token_is_valid(token, SocketKind::kTcp) || token->descriptor < 0) {
    return false;
  }
  sockaddr_storage peer = {};
  socklen_t size = sizeof(peer);
  return lwip_getpeername(token->descriptor,
                          reinterpret_cast<sockaddr *>(&peer), &size) == 0;
}

void tcp_socket_shutdown(void *socket) {
  auto *token = static_cast<NativeSocket *>(socket);
  if (token_is_valid(token, SocketKind::kTcp) && token->descriptor >= 0) {
    (void)lwip_shutdown(token->descriptor, SHUT_RDWR);
  }
}

void tcp_socket_close(void *socket) {
  auto *token = static_cast<NativeSocket *>(socket);
  release_token(token, SocketKind::kTcp);
}

hal_status_t tcp_listener_open(void **out_listener) {
  if (out_listener == nullptr) {
    return HAL_EINVAL;
  }
  *out_listener = nullptr;
  const hal_status_t ready = jh_esp32_network_require_ready();
  if (ready != HAL_OK) {
    return ready;
  }
  NativeSocket *token = allocate_token(SocketKind::kTcpListener);
  if (token == nullptr) {
    return HAL_ENOMEM;
  }
  const hal_status_t still_ready = jh_esp32_network_require_ready();
  if (still_ready != HAL_OK) {
    release_token(token, SocketKind::kTcpListener);
    return still_ready;
  }
  *out_listener = token;
  return HAL_OK;
}

hal_status_t tcp_listener_bind(void *listener,
                               const hal_net_endpoint_t *local) {
  auto *token = static_cast<NativeSocket *>(listener);
  if (!token_is_valid(token, SocketKind::kTcpListener) || local == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t ready = jh_esp32_network_require_ready();
  if (ready != HAL_OK) {
    return ready;
  }
  sockaddr_storage address = {};
  socklen_t address_length = 0u;
  hal_status_t status =
      sockaddr_from_endpoint(local, &address, &address_length);
  if (status != HAL_OK) {
    return status;
  }
  status =
      create_descriptor(token, SocketKind::kTcpListener,
                        native_family(local->family), SOCK_STREAM, IPPROTO_TCP);
  if (status != HAL_OK) {
    return status;
  }
  const int reuse = 1;
  (void)lwip_setsockopt(token->descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse,
                        sizeof(reuse));
  return lwip_bind(token->descriptor, reinterpret_cast<sockaddr *>(&address),
                   address_length) == 0
             ? HAL_OK
             : status_from_last_errno(false, 0u);
}

hal_status_t tcp_listener_listen(void *listener, uint8_t backlog) {
  auto *token = static_cast<NativeSocket *>(listener);
  if (!token_is_valid(token, SocketKind::kTcpListener) ||
      token->descriptor < 0 || backlog == 0u) {
    return HAL_EINVAL;
  }
  const int capped = backlog > HAL_TCP_LISTENER_BACKLOG_MAX
                         ? static_cast<int>(HAL_TCP_LISTENER_BACKLOG_MAX)
                         : static_cast<int>(backlog);
  return lwip_listen(token->descriptor, capped) == 0
             ? HAL_OK
             : status_from_last_errno(false, 0u);
}

hal_status_t tcp_listener_accept(void *listener, hal_net_endpoint_t *remote,
                                 uint32_t timeout_ms, void **out_socket) {
  auto *token = static_cast<NativeSocket *>(listener);
  if (out_socket != nullptr) {
    *out_socket = nullptr;
  }
  if (!token_is_valid(token, SocketKind::kTcpListener) ||
      token->descriptor < 0 || out_socket == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t ready =
      wait_for_descriptor(token->descriptor, true, false, timeout_ms);
  if (ready != HAL_OK) {
    return ready;
  }
  sockaddr_storage address = {};
  socklen_t address_length = sizeof(address);
  const int descriptor =
      lwip_accept(token->descriptor, reinterpret_cast<sockaddr *>(&address),
                  &address_length);
  if (descriptor < 0) {
    return status_from_last_errno(true, timeout_ms);
  }
  const hal_status_t network_ready = jh_esp32_network_require_ready();
  if (network_ready != HAL_OK) {
    (void)lwip_close(descriptor);
    return network_ready;
  }
  NativeSocket *accepted = allocate_token(SocketKind::kTcp);
  if (accepted == nullptr) {
    (void)lwip_close(descriptor);
    return HAL_ENOMEM;
  }
  const hal_status_t adopt_status = adopt_descriptor(
      accepted, SocketKind::kTcp, descriptor, address.ss_family);
  if (adopt_status != HAL_OK) {
    (void)lwip_close(descriptor);
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
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  const hal_status_t ready = jh_esp32_network_require_ready();
  if (ready != HAL_OK) {
    return ready;
  }
  NativeSocket *token = allocate_token(SocketKind::kUdp);
  if (token == nullptr) {
    return HAL_ENOMEM;
  }
  const hal_status_t still_ready = jh_esp32_network_require_ready();
  if (still_ready != HAL_OK) {
    release_token(token, SocketKind::kUdp);
    return still_ready;
  }
  *out_socket = token;
  return HAL_OK;
}

hal_status_t udp_socket_bind(void *socket, const hal_net_endpoint_t *local) {
  auto *token = static_cast<NativeSocket *>(socket);
  if (!token_is_valid(token, SocketKind::kUdp) || local == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t ready = jh_esp32_network_require_ready();
  if (ready != HAL_OK) {
    return ready;
  }
  sockaddr_storage address = {};
  socklen_t address_length = 0u;
  hal_status_t status =
      sockaddr_from_endpoint(local, &address, &address_length);
  if (status != HAL_OK) {
    return status;
  }
  status =
      create_descriptor(token, SocketKind::kUdp, native_family(local->family),
                        SOCK_DGRAM, IPPROTO_UDP);
  if (status != HAL_OK) {
    return status;
  }
  return lwip_bind(token->descriptor, reinterpret_cast<sockaddr *>(&address),
                   address_length) == 0
             ? HAL_OK
             : status_from_last_errno(false, 0u);
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
      token->descriptor < 0) {
    return HAL_EINVAL;
  }
  sockaddr_storage address = {};
  socklen_t address_length = 0u;
  const hal_status_t status =
      sockaddr_from_endpoint(remote, &address, &address_length);
  if (status != HAL_OK) {
    return status;
  }
  if (native_family(remote->family) != token->family) {
    return HAL_EUNSUPPORTED;
  }
  const ssize_t sent =
      lwip_sendto(token->descriptor, data, length, 0,
                  reinterpret_cast<sockaddr *>(&address), address_length);
  if (sent < 0) {
    return status_from_last_errno(false, 0u);
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
      (max_length > 0u && buffer == nullptr) || token->descriptor < 0) {
    return HAL_EINVAL;
  }
  if (max_length == 0u) {
    return HAL_OK;
  }
  const hal_status_t ready =
      wait_for_descriptor(token->descriptor, true, false, timeout_ms);
  if (ready != HAL_OK) {
    return ready;
  }
  sockaddr_storage address = {};
  socklen_t address_length = sizeof(address);
  const ssize_t received =
      lwip_recvfrom(token->descriptor, buffer, max_length, MSG_DONTWAIT,
                    reinterpret_cast<sockaddr *>(&address), &address_length);
  if (received < 0) {
    return status_from_last_errno(true, timeout_ms);
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
  return token_is_valid(token, SocketKind::kUdp) && token->descriptor >= 0 &&
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

} // namespace

extern "C" hal_status_t jh_esp32_network_sockets_initialize(void) {
  return ensure_pool();
}

extern "C" hal_status_t jh_esp32_network_sockets_shutdown_all(void) {
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
    if (s_sockets[index].in_use && s_sockets[index].descriptor >= 0) {
      (void)lwip_shutdown(s_sockets[index].descriptor, SHUT_RDWR);
    }
  }
  hal_mutex_unlock(s_socket_pool_mutex);
  return HAL_OK;
}

#if defined(HAL_ENABLE_TCP)
extern "C" const jh_network_tcp_ops_t *jh_esp32_network_tcp_ops(void) {
  return &s_tcp_ops;
}
#endif

#if defined(HAL_ENABLE_UDP)
extern "C" const jh_network_udp_ops_t *jh_esp32_network_udp_ops(void) {
  return &s_udp_ops;
}
#endif

#endif // HAL_ENABLE_NETWORK_CORE && HAL_NETWORK_BACKEND_ESP_IDF
#endif // HAL_TARGET_IS_ESP32_FAMILY
