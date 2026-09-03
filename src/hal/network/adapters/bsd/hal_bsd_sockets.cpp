/** @file Public BSD/POSIX socket adapter over the shared HAL network API. */
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_BSD_SOCKETS) && defined(HAL_NETWORK_BACKEND_ESP_IDF)

#include "hal/core/hal_target.h"

#if !HAL_TARGET_IS_ESP32_FAMILY
#error "HAL_NETWORK_BACKEND_ESP_IDF requires an ESP32-family target"
#endif

/* ESP-IDF exports the native lwIP BSD API. Defining the HAL compatibility
 * symbols here would be macro-expanded to lwip_* and collide with ESP-IDF. */

#elif defined(HAL_ENABLE_BSD_SOCKETS)

#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_target.h"
#include "hal/core/jh_endian.h"
#include "hal/network/hal_net.h"
#include "hal/network/hal_tcp.h"
#include "hal/network/hal_udp.h"
#include "hal/network/jh_net_address_utils.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT EINVAL
#endif
#ifndef EMFILE
#define EMFILE ENOMEM
#endif
#ifndef ENOTSOCK
#define ENOTSOCK EBADF
#endif
#ifndef EOPNOTSUPP
#ifdef ENOTSUP
#define EOPNOTSUPP ENOTSUP
#else
#define EOPNOTSUPP EINVAL
#endif
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT EINVAL
#endif
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT EPROTONOSUPPORT
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED EIO
#endif
#ifndef EADDRINUSE
#define EADDRINUSE EIO
#endif
#ifndef ENOTCONN
#define ENOTCONN EIO
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT EIO
#endif
#ifndef EAGAIN
#define EAGAIN EIO
#endif
#ifndef EINVAL
#define EINVAL EIO
#endif
#ifndef EINPROGRESS
#define EINPROGRESS EAGAIN
#endif
#ifndef ENOPROTOOPT
#define ENOPROTOOPT EINVAL
#endif

#define HAL_BSD_ADDRINFO_CANONNAME_MAX 256u
#define HAL_BSD_ADDRINFO_MAX_RESULTS 8u
#define HAL_BSD_UDP_EPHEMERAL_PORT_FIRST 49152u
#define HAL_BSD_UDP_EPHEMERAL_PORT_COUNT 16384u

typedef enum {
  HAL_BSD_FD_UNUSED = 0,
  HAL_BSD_FD_UDP,
  HAL_BSD_FD_TCP_SOCKET,
  HAL_BSD_FD_TCP_LISTENER
} hal_bsd_fd_kind_t;

typedef struct {
  hal_bsd_fd_kind_t kind;
  hal_udp_socket_t udp;
  hal_tcp_socket_t tcp_socket;
  hal_tcp_listener_t tcp_listener;
  hal_net_endpoint_t local_endpoint;
  hal_net_endpoint_t remote_endpoint;
  uint32_t recv_timeout_ms;
  uint32_t send_timeout_ms;
  int status_flags;
  int last_error;
  hal_net_family_t family;
  bool has_local_endpoint;
  bool has_remote_endpoint;
  bool udp_bound;
  bool reuse_addr;
  bool tcp_was_connected;
} hal_bsd_fd_entry_t;

typedef struct {
  struct addrinfo info;
  union {
    struct sockaddr generic;
    struct sockaddr_in ipv4;
    struct sockaddr_in6 ipv6;
  } addr;
  char canonname[HAL_BSD_ADDRINFO_CANONNAME_MAX];
} hal_bsd_addrinfo_node_t;

static hal_bsd_fd_entry_t s_fd_table[HAL_BSD_SOCKET_MAX_FDS];
static hal_mutex_t s_fd_table_mutex = NULL;
static uint16_t s_next_ephemeral_udp_port = 49152u;

static bool fd_table_lock(void) {
  if (jh_hal_mutex_create_once(&s_fd_table_mutex) == NULL) {
    errno = ENOMEM;
    return false;
  }
  hal_mutex_lock(s_fd_table_mutex);
  return true;
}

static void fd_table_unlock(void) { hal_mutex_unlock(s_fd_table_mutex); }

static void fd_table_relock(void) {
  // Only used after this operation has already acquired and released the
  // table, so the once-created mutex cannot be absent here.
  hal_mutex_lock(s_fd_table_mutex);
}

static void clear_entry(hal_bsd_fd_entry_t *entry) {
  if (!entry) {
    return;
  }
  memset(entry, 0, sizeof(*entry));
  entry->kind = HAL_BSD_FD_UNUSED;
  entry->family = HAL_NET_AF_UNSPEC;
  entry->local_endpoint.family = HAL_NET_AF_UNSPEC;
  entry->remote_endpoint.family = HAL_NET_AF_UNSPEC;
  entry->recv_timeout_ms = HAL_NET_TIMEOUT_FOREVER;
  entry->send_timeout_ms = HAL_NET_TIMEOUT_FOREVER;
}

static bool is_fd_in_range(int fd) {
  return fd >= HAL_BSD_SOCKET_FD_BASE &&
         fd < (HAL_BSD_SOCKET_FD_BASE + (int)HAL_BSD_SOCKET_MAX_FDS);
}

static int fd_to_index(int fd) { return fd - HAL_BSD_SOCKET_FD_BASE; }

static hal_bsd_fd_entry_t *entry_for_fd(int fd) {
  if (!is_fd_in_range(fd)) {
    errno = EBADF;
    return NULL;
  }

  hal_bsd_fd_entry_t *entry = &s_fd_table[fd_to_index(fd)];
  if (entry->kind == HAL_BSD_FD_UNUSED) {
    errno = EBADF;
    return NULL;
  }
  return entry;
}

static int allocate_fd(void) {
  for (size_t i = 0u; i < HAL_BSD_SOCKET_MAX_FDS; ++i) {
    if (s_fd_table[i].kind == HAL_BSD_FD_UNUSED) {
      clear_entry(&s_fd_table[i]);
      return HAL_BSD_SOCKET_FD_BASE + (int)i;
    }
  }

  errno = EMFILE;
  return -1;
}

static bool file_status_flags_supported(int flags) {
  return (flags & ~O_NONBLOCK) == 0;
}

static uint32_t endpoint_addr_to_host_u32(const hal_net_endpoint_t *endpoint) {
  return jh_load_be32(endpoint->addr);
}

static void endpoint_addr_from_host_u32(hal_net_endpoint_t *endpoint,
                                        uint32_t host_addr) {
  jh_store_be32(endpoint->addr, host_addr);
}

static uint32_t
ipv4_addr_to_host_u32(const uint8_t addr[HAL_NET_IPV4_ADDR_LEN]) {
  return jh_load_be32(addr);
}

static bool sockaddr_to_endpoint(const struct sockaddr *addr, socklen_t addrlen,
                                 hal_net_endpoint_t *endpoint) {
  if (!addr || !endpoint) {
    errno = EINVAL;
    return false;
  }
  memset(endpoint, 0, sizeof(*endpoint));
  if (addr->sa_family == AF_INET) {
    if (addrlen < (socklen_t)sizeof(struct sockaddr_in)) {
      errno = EINVAL;
      return false;
    }
    struct sockaddr_in sin = {};
    memcpy(&sin, addr, sizeof(sin));
    endpoint->family = HAL_NET_AF_INET;
    endpoint->addr_len = HAL_NET_IPV4_ADDR_LEN;
    endpoint->port = ntohs(sin.sin_port);
    endpoint_addr_from_host_u32(endpoint, ntohl(sin.sin_addr.s_addr));
    return true;
  }
  if (addr->sa_family == AF_INET6) {
    if (addrlen < (socklen_t)sizeof(struct sockaddr_in6)) {
      errno = EINVAL;
      return false;
    }
    struct sockaddr_in6 sin6 = {};
    memcpy(&sin6, addr, sizeof(sin6));
    if (sin6.sin6_flowinfo != 0u) {
      errno = EINVAL;
      return false;
    }
    endpoint->family = HAL_NET_AF_INET6;
    endpoint->addr_len = HAL_NET_IPV6_ADDR_LEN;
    endpoint->port = ntohs(sin6.sin6_port);
    endpoint->scope_id = sin6.sin6_scope_id;
    memcpy(endpoint->addr, sin6.sin6_addr.s6_addr, HAL_NET_IPV6_ADDR_LEN);
    return true;
  }
  errno = EAFNOSUPPORT;
  return false;
}

static bool write_sockaddr_from_endpoint(const hal_net_endpoint_t *endpoint,
                                         struct sockaddr *addr,
                                         socklen_t *addrlen) {
  if (!addr) {
    return true;
  }
  if (!addrlen) {
    errno = EINVAL;
    return false;
  }
  if (jh_net_validate_endpoint_shape(endpoint, false, true) != HAL_OK) {
    errno = EAFNOSUPPORT;
    return false;
  }
  const socklen_t requested = *addrlen;
  socklen_t actual = 0u;
  uint8_t storage[sizeof(struct sockaddr_in6)] = {};
  if (endpoint->family == HAL_NET_AF_INET) {
    struct sockaddr_in sin = {};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(endpoint->port);
    sin.sin_addr.s_addr = htonl(endpoint_addr_to_host_u32(endpoint));
    actual = (socklen_t)sizeof(sin);
    memcpy(storage, &sin, sizeof(sin));
  } else {
    struct sockaddr_in6 sin6 = {};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(endpoint->port);
    memcpy(sin6.sin6_addr.s6_addr, endpoint->addr, HAL_NET_IPV6_ADDR_LEN);
    sin6.sin6_scope_id = endpoint->scope_id;
    actual = (socklen_t)sizeof(sin6);
    memcpy(storage, &sin6, sizeof(sin6));
  }
  const socklen_t to_copy = requested < actual ? requested : actual;
  if (to_copy > 0u) {
    memcpy(addr, storage, (size_t)to_copy);
  }
  *addrlen = actual;
  return true;
}

static bool check_message_flags(int flags) {
  if ((flags & ~MSG_DONTWAIT) == 0) {
    return true;
  }
  errno = EOPNOTSUPP;
  return false;
}

static bool message_flags_request_nonblocking(int flags) {
  return (flags & MSG_DONTWAIT) != 0;
}

static bool entry_is_nonblocking(const hal_bsd_fd_entry_t *entry) {
  return entry && ((entry->status_flags & O_NONBLOCK) != 0);
}

static void entry_set_error(hal_bsd_fd_entry_t *entry, int error) {
  if (entry) {
    entry->last_error = error;
  }
}

/* Compute an I/O timeout from non-blocking and configured timeout state
 * captured under the fd-table lock. Used by blocking I/O paths so the lock can
 * be released before the underlying transport call instead of being held across
 * it. */
static uint32_t io_timeout_value(bool nonblocking, int message_flags,
                                 uint32_t configured_timeout_ms) {
  if (nonblocking || message_flags_request_nonblocking(message_flags)) {
    return 0u;
  }
  return configured_timeout_ms;
}

/* Compute an operation timeout from state captured under the fd-table lock. */
static uint32_t operation_timeout_value(bool nonblocking,
                                        uint32_t configured_timeout_ms) {
  return nonblocking ? 0u : configured_timeout_ms;
}

static bool timeval_to_timeout_ms(const struct timeval *timeout,
                                  uint32_t *timeout_ms, bool *forever);

static bool timeout_ms_to_timeval(uint32_t timeout_ms, void *optval,
                                  socklen_t *optlen) {
  if (!optval || !optlen || *optlen < (socklen_t)sizeof(struct timeval)) {
    errno = EINVAL;
    return false;
  }

  struct timeval timeout = {};
  if (timeout_ms != HAL_NET_TIMEOUT_FOREVER) {
    timeout.tv_sec = (long)(timeout_ms / 1000u);
    timeout.tv_usec = (long)((timeout_ms % 1000u) * 1000u);
  }

  memcpy(optval, &timeout, sizeof(timeout));
  *optlen = (socklen_t)sizeof(timeout);
  return true;
}

static bool sockopt_timeval_to_timeout_ms(const void *optval, socklen_t optlen,
                                          uint32_t *timeout_ms) {
  if (!optval || !timeout_ms || optlen < (socklen_t)sizeof(struct timeval)) {
    errno = EINVAL;
    return false;
  }

  struct timeval timeout = {};
  memcpy(&timeout, optval, sizeof(timeout));

  bool forever = false;
  return timeval_to_timeout_ms(&timeout, timeout_ms, &forever);
}

static uint16_t next_ephemeral_udp_port(void) {
  const uint16_t port = s_next_ephemeral_udp_port;
  s_next_ephemeral_udp_port++;
  if (s_next_ephemeral_udp_port < HAL_BSD_UDP_EPHEMERAL_PORT_FIRST) {
    s_next_ephemeral_udp_port = HAL_BSD_UDP_EPHEMERAL_PORT_FIRST;
  }
  return port;
}

static bool udp_local_port_in_use(uint16_t port) {
  for (size_t i = 0u; i < HAL_BSD_SOCKET_MAX_FDS; ++i) {
    const hal_bsd_fd_entry_t *entry = &s_fd_table[i];
    if (entry->kind == HAL_BSD_FD_UDP && entry->udp_bound &&
        entry->local_endpoint.port == port) {
      return true;
    }
  }
  return false;
}

static bool ensure_udp_bound(hal_bsd_fd_entry_t *entry) {
  if (!entry || entry->kind != HAL_BSD_FD_UDP || !entry->udp) {
    errno = ENOTSOCK;
    return false;
  }
  if (entry->udp_bound) {
    return true;
  }

  hal_net_endpoint_t local = {};
  local.family = entry->family;
  local.addr_len = entry->family == HAL_NET_AF_INET ? HAL_NET_IPV4_ADDR_LEN
                                                    : HAL_NET_IPV6_ADDR_LEN;
  bool bound = false;
  for (uint32_t attempts = 0u; attempts < HAL_BSD_UDP_EPHEMERAL_PORT_COUNT;
       ++attempts) {
    local.port = next_ephemeral_udp_port();
    if (udp_local_port_in_use(local.port)) {
      continue;
    }
    if (!hal_udp_socket_bind(entry->udp, &local)) {
      errno = EIO;
      return false;
    }
    bound = true;
    break;
  }

  if (!bound) {
    errno = EADDRINUSE;
    return false;
  }

  entry->local_endpoint = local;
  entry->has_local_endpoint = true;
  entry->udp_bound = true;
  return true;
}

static bool parse_ipv4_string(const char *src, uint8_t out[4]) {
  if (!src || !out) {
    return false;
  }

  const char *cursor = src;
  for (size_t octet = 0u; octet < 4u; ++octet) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    unsigned value = 0u;
    while (*cursor >= '0' && *cursor <= '9') {
      value = (value * 10u) + (unsigned)(*cursor - '0');
      if (value > 255u) {
        return false;
      }
      cursor++;
    }

    out[octet] = (uint8_t)value;
    if (octet < 3u) {
      if (*cursor != '.') {
        return false;
      }
      cursor++;
    } else if (*cursor != '\0') {
      return false;
    }
  }

  return true;
}

static bool parse_service_port(const char *service, uint16_t *port) {
  if (!port) {
    return false;
  }
  if (!service) {
    *port = 0u;
    return true;
  }
  if (service[0] == '\0') {
    return false;
  }

  unsigned value = 0u;
  const char *cursor = service;
  while (*cursor != '\0') {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    value = (value * 10u) + (unsigned)(*cursor - '0');
    if (value > 65535u) {
      return false;
    }
    cursor++;
  }

  *port = (uint16_t)value;
  return true;
}

static int addrinfo_protocol_for(int socktype, int protocol) {
  if (protocol != 0) {
    return protocol;
  }
  if (socktype == SOCK_STREAM) {
    return IPPROTO_TCP;
  }
  if (socktype == SOCK_DGRAM) {
    return IPPROTO_UDP;
  }
  return 0;
}

static bool addrinfo_socktype_protocol_supported(int socktype, int protocol) {
  if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM) {
    return false;
  }
  if (protocol != 0 && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) {
    return false;
  }
  if (socktype == SOCK_STREAM && protocol == IPPROTO_UDP) {
    return false;
  }
  if (socktype == SOCK_DGRAM && protocol == IPPROTO_TCP) {
    return false;
  }
  return true;
}

static bool copy_canonname(hal_bsd_addrinfo_node_t *node,
                           const char *canonname) {
  if (!node || !canonname) {
    return false;
  }

  const size_t len = strlen(canonname);
  if (len >= sizeof(node->canonname)) {
    return false;
  }

  memcpy(node->canonname, canonname, len + 1u);
  node->info.ai_canonname = node->canonname;
  return true;
}

static bool format_endpoint_canonname(hal_bsd_addrinfo_node_t *node,
                                      const hal_net_endpoint_t *endpoint) {
  if (!node || !endpoint) {
    return false;
  }
  if (endpoint->family == HAL_NET_AF_INET) {
    const int written =
        snprintf(node->canonname, sizeof(node->canonname), "%u.%u.%u.%u",
                 (unsigned)endpoint->addr[0], (unsigned)endpoint->addr[1],
                 (unsigned)endpoint->addr[2], (unsigned)endpoint->addr[3]);
    if (written < 0 || (size_t)written >= sizeof(node->canonname)) {
      return false;
    }
  } else if (endpoint->family == HAL_NET_AF_INET6) {
    if (!jh_net_format_ipv6(endpoint->addr, node->canonname,
                            sizeof(node->canonname))) {
      return false;
    }
  } else {
    return false;
  }
  node->info.ai_canonname = node->canonname;
  return true;
}

static bool bsd_family_supported(int family) {
  const hal_net_capabilities_t capabilities = hal_net_get_capabilities();
  if (family == AF_INET) {
    return (capabilities & HAL_NET_CAP_IPV4) != 0u;
  }
  if (family == AF_INET6) {
    return (capabilities & HAL_NET_CAP_IPV6) != 0u;
  }
  return false;
}

static int resolver_status_to_eai(hal_status_t status) {
  switch (status) {
  case HAL_OK:
    return 0;
  case HAL_EUNSUPPORTED:
    return EAI_FAMILY;
  case HAL_EBUSY:
  case HAL_ETIMEOUT:
    return EAI_AGAIN;
  case HAL_ENOMEM:
  case HAL_EOVERFLOW:
    return EAI_MEMORY;
  case HAL_ENOENT:
    return EAI_NONAME;
  default:
    return EAI_FAIL;
  }
}

static bool append_null_node_result(hal_net_family_t family, int flags,
                                    hal_net_endpoint_t *results,
                                    size_t capacity, size_t *count) {
  if (*count >= capacity) {
    return false;
  }
  hal_net_endpoint_t *result = &results[(*count)++];
  memset(result, 0, sizeof(*result));
  result->family = family;
  result->addr_len =
      family == HAL_NET_AF_INET ? HAL_NET_IPV4_ADDR_LEN : HAL_NET_IPV6_ADDR_LEN;
  if ((flags & AI_PASSIVE) == 0) {
    result->addr[result->addr_len - 1u] = 1u;
    if (family == HAL_NET_AF_INET) {
      result->addr[0] = 127u;
    }
  }
  return true;
}

static int resolve_addrinfo_results(
    const char *node, int flags, int family,
    hal_net_endpoint_t results[HAL_BSD_ADDRINFO_MAX_RESULTS],
    size_t *out_count) {
  *out_count = 0u;
  if (family != AF_UNSPEC && !bsd_family_supported(family)) {
    return EAI_FAMILY;
  }

  if (!node) {
    if ((family == AF_UNSPEC || family == AF_INET) &&
        bsd_family_supported(AF_INET) &&
        !append_null_node_result(HAL_NET_AF_INET, flags, results,
                                 HAL_BSD_ADDRINFO_MAX_RESULTS, out_count)) {
      return EAI_MEMORY;
    }
    if ((family == AF_UNSPEC || family == AF_INET6) &&
        bsd_family_supported(AF_INET6) &&
        !append_null_node_result(HAL_NET_AF_INET6, flags, results,
                                 HAL_BSD_ADDRINFO_MAX_RESULTS, out_count)) {
      return EAI_MEMORY;
    }
    return *out_count > 0u ? 0 : EAI_FAMILY;
  }
  if (node[0] == '\0') {
    return EAI_NONAME;
  }

  if ((flags & AI_NUMERICHOST) != 0) {
    hal_net_endpoint_t endpoint = {};
    if ((family == AF_UNSPEC || family == AF_INET) &&
        jh_net_parse_ipv4_literal(node, endpoint.addr)) {
      if (!bsd_family_supported(AF_INET)) {
        return EAI_FAMILY;
      }
      endpoint.family = HAL_NET_AF_INET;
      endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
      results[0] = endpoint;
      *out_count = 1u;
      return 0;
    }
    memset(&endpoint, 0, sizeof(endpoint));
    if ((family == AF_UNSPEC || family == AF_INET6) &&
        jh_net_parse_ipv6_literal(node, endpoint.addr, &endpoint.scope_id,
                                  true)) {
      if (!bsd_family_supported(AF_INET6)) {
        return EAI_FAMILY;
      }
      endpoint.family = HAL_NET_AF_INET6;
      endpoint.addr_len = HAL_NET_IPV6_ADDR_LEN;
      results[0] = endpoint;
      *out_count = 1u;
      return 0;
    }
    return EAI_NONAME;
  }

  const hal_net_family_t family_hint = family == AF_INET    ? HAL_NET_AF_INET
                                       : family == AF_INET6 ? HAL_NET_AF_INET6
                                                            : HAL_NET_AF_UNSPEC;
  return resolver_status_to_eai(hal_net_resolve_ex(
      node, family_hint, results, HAL_BSD_ADDRINFO_MAX_RESULTS, out_count));
}

in_addr_t inet_addr(const char *cp) {
  uint8_t octets[4] = {};
  if (!parse_ipv4_string(cp, octets)) {
    return INADDR_NONE;
  }

  const uint32_t host_addr = jh_load_be32(octets);
  return htonl(host_addr);
}

int inet_pton(int af, const char *src, void *dst) {
  if (af != AF_INET && af != AF_INET6) {
    errno = EAFNOSUPPORT;
    return -1;
  }
  if (!src || !dst) {
    errno = EINVAL;
    return -1;
  }

  if (af == AF_INET6) {
    uint8_t address[HAL_NET_IPV6_ADDR_LEN] = {};
    if (!jh_net_parse_ipv6_literal(src, address, NULL, false)) {
      return 0;
    }
    memcpy(dst, address, sizeof(address));
    return 1;
  }

  uint8_t octets[4] = {};
  if (!parse_ipv4_string(src, octets)) {
    return 0;
  }

  const uint32_t host_addr = jh_load_be32(octets);
  const in_addr_t net_addr = htonl(host_addr);
  memcpy(dst, &net_addr, sizeof(net_addr));
  return 1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
  if (af != AF_INET && af != AF_INET6) {
    errno = EAFNOSUPPORT;
    return NULL;
  }
  if (!src || !dst || size == 0u) {
    errno = EINVAL;
    return NULL;
  }

  if (af == AF_INET6) {
    if (!jh_net_format_ipv6((const uint8_t *)src, dst, (size_t)size)) {
      errno = ENOSPC;
      return NULL;
    }
    return dst;
  }

  in_addr_t net_addr = 0u;
  memcpy(&net_addr, src, sizeof(net_addr));
  const uint32_t host_addr = ntohl(net_addr);
  uint8_t octets[HAL_NET_IPV4_ADDR_LEN];
  jh_store_be32(octets, host_addr);
  const int written =
      snprintf(dst, (size_t)size, "%u.%u.%u.%u", (unsigned)octets[0],
               (unsigned)octets[1], (unsigned)octets[2], (unsigned)octets[3]);
  if (written < 0 || (socklen_t)written >= size) {
    errno = ENOSPC;
    return NULL;
  }

  return dst;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res) {
  if (!res) {
    return EAI_FAIL;
  }
  *res = NULL;

  if (!node && !service) {
    return EAI_NONAME;
  }

  int flags = 0;
  int family = AF_UNSPEC;
  int socktype = 0;
  int protocol = 0;
  if (hints) {
    flags = hints->ai_flags;
    family = hints->ai_family;
    socktype = hints->ai_socktype;
    protocol = hints->ai_protocol;
  }

  const int supported_flags = AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
                              AI_ADDRCONFIG | AI_NUMERICSERV;
  if ((flags & ~supported_flags) != 0) {
    return EAI_BADFLAGS;
  }
  if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) {
    return EAI_FAMILY;
  }
  if (!addrinfo_socktype_protocol_supported(socktype, protocol)) {
    return EAI_SOCKTYPE;
  }

  uint16_t port = 0u;
  if (!parse_service_port(service, &port)) {
    return EAI_SERVICE;
  }

  hal_net_endpoint_t endpoints[HAL_BSD_ADDRINFO_MAX_RESULTS] = {};
  size_t endpoint_count = 0u;
  const int resolve_result =
      resolve_addrinfo_results(node, flags, family, endpoints, &endpoint_count);
  if (resolve_result != 0) {
    return resolve_result;
  }

  struct addrinfo *head = NULL;
  struct addrinfo *tail = NULL;
  for (size_t index = 0u; index < endpoint_count; ++index) {
    hal_bsd_addrinfo_node_t *out =
        (hal_bsd_addrinfo_node_t *)calloc(1u, sizeof(*out));
    if (!out) {
      freeaddrinfo(head);
      return EAI_MEMORY;
    }
    endpoints[index].port = port;
    if (endpoints[index].family == HAL_NET_AF_INET) {
      out->addr.ipv4.sin_family = AF_INET;
      out->addr.ipv4.sin_port = htons(port);
      out->addr.ipv4.sin_addr.s_addr =
          htonl(ipv4_addr_to_host_u32(endpoints[index].addr));
      out->info.ai_family = AF_INET;
      out->info.ai_addrlen = (socklen_t)sizeof(out->addr.ipv4);
      out->info.ai_addr = (struct sockaddr *)&out->addr.ipv4;
    } else {
      out->addr.ipv6.sin6_family = AF_INET6;
      out->addr.ipv6.sin6_port = htons(port);
      memcpy(out->addr.ipv6.sin6_addr.s6_addr, endpoints[index].addr,
             HAL_NET_IPV6_ADDR_LEN);
      out->addr.ipv6.sin6_scope_id = endpoints[index].scope_id;
      out->info.ai_family = AF_INET6;
      out->info.ai_addrlen = (socklen_t)sizeof(out->addr.ipv6);
      out->info.ai_addr = (struct sockaddr *)&out->addr.ipv6;
    }
    out->info.ai_socktype = socktype;
    out->info.ai_protocol = addrinfo_protocol_for(socktype, protocol);
    out->info.ai_next = NULL;

    if (index == 0u && (flags & AI_CANONNAME) != 0) {
      const bool ok = node ? copy_canonname(out, node)
                           : format_endpoint_canonname(out, &endpoints[index]);
      if (!ok) {
        free(out);
        freeaddrinfo(head);
        return EAI_MEMORY;
      }
    }

    if (!head) {
      head = &out->info;
    } else {
      tail->ai_next = &out->info;
    }
    tail = &out->info;
  }
  *res = head;
  return 0;
}

void freeaddrinfo(struct addrinfo *res) {
  while (res) {
    struct addrinfo *next = res->ai_next;
    free((hal_bsd_addrinfo_node_t *)res);
    res = next;
  }
}

const char *gai_strerror(int errcode) {
  switch (errcode) {
  case 0:
    return "success";
  case EAI_BADFLAGS:
    return "bad flags";
  case EAI_NONAME:
    return "name or service not known";
  case EAI_AGAIN:
    return "temporary failure";
  case EAI_FAIL:
    return "non-recoverable failure";
  case EAI_FAMILY:
    return "address family not supported";
  case EAI_MEMORY:
    return "memory allocation failure";
  case EAI_SERVICE:
    return "service not supported";
  case EAI_SOCKTYPE:
    return "socket type not supported";
  case EAI_SYSTEM:
    return "system error";
  default:
    return "unknown getaddrinfo error";
  }
}

int fcntl(int fd, int cmd, ...) {
  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }

  if (cmd == F_GETFL) {
    const int flags = entry->status_flags;
    fd_table_unlock();
    return flags;
  }

  if (cmd == F_SETFL) {
    va_list args;
    va_start(args, cmd);
    const int flags = va_arg(args, int);
    va_end(args);

    if (!file_status_flags_supported(flags)) {
      errno = EINVAL;
      fd_table_unlock();
      return -1;
    }

    entry->status_flags = flags & O_NONBLOCK;
    fd_table_unlock();
    return 0;
  }

  errno = EINVAL;
  fd_table_unlock();
  return -1;
}

int socket(int domain, int type, int protocol) {
  if (domain != AF_INET && domain != PF_INET && domain != AF_INET6 &&
      domain != PF_INET6) {
    errno = EAFNOSUPPORT;
    return -1;
  }
  const hal_net_family_t family = domain == AF_INET6 || domain == PF_INET6
                                      ? HAL_NET_AF_INET6
                                      : HAL_NET_AF_INET;
  if (!bsd_family_supported(domain)) {
    errno = EAFNOSUPPORT;
    return -1;
  }

  if (type == SOCK_DGRAM && (protocol == 0 || protocol == IPPROTO_UDP)) {
    if (!fd_table_lock()) {
      return -1;
    }
    const int fd = allocate_fd();
    if (fd < 0) {
      fd_table_unlock();
      return -1;
    }

    hal_udp_socket_t udp = hal_udp_socket_open();
    if (!udp) {
      clear_entry(&s_fd_table[fd_to_index(fd)]);
      errno = ENOMEM;
      fd_table_unlock();
      return -1;
    }

    hal_bsd_fd_entry_t *entry = &s_fd_table[fd_to_index(fd)];
    entry->kind = HAL_BSD_FD_UDP;
    entry->family = family;
    entry->udp = udp;
    fd_table_unlock();
    return fd;
  }

  if (type == SOCK_STREAM && (protocol == 0 || protocol == IPPROTO_TCP)) {
    if (!fd_table_lock()) {
      return -1;
    }
    const int fd = allocate_fd();
    if (fd < 0) {
      fd_table_unlock();
      return -1;
    }

    hal_tcp_socket_t tcp = hal_tcp_socket_open();
    if (!tcp) {
      clear_entry(&s_fd_table[fd_to_index(fd)]);
      errno = ENOMEM;
      fd_table_unlock();
      return -1;
    }

    hal_bsd_fd_entry_t *entry = &s_fd_table[fd_to_index(fd)];
    entry->kind = HAL_BSD_FD_TCP_SOCKET;
    entry->family = family;
    entry->tcp_socket = tcp;
    fd_table_unlock();
    return fd;
  }

  errno = (type == SOCK_DGRAM || type == SOCK_STREAM) ? EPROTONOSUPPORT
                                                      : ESOCKTNOSUPPORT;
  return -1;
}

static hal_bsd_fd_entry_t *
prepare_endpoint_operation(int sockfd, const struct sockaddr *addr,
                           socklen_t addrlen, hal_net_endpoint_t *endpoint) {
  if (!sockaddr_to_endpoint(addr, addrlen, endpoint)) {
    return NULL;
  }
  if (endpoint->port == 0u) {
    errno = EINVAL;
    return NULL;
  }
  if (!fd_table_lock()) {
    return NULL;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return NULL;
  }
  if (endpoint->family != entry->family) {
    errno = EAFNOSUPPORT;
    fd_table_unlock();
    return NULL;
  }
  return entry;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  hal_net_endpoint_t endpoint = {};
  hal_bsd_fd_entry_t *entry =
      prepare_endpoint_operation(sockfd, addr, addrlen, &endpoint);
  if (entry == NULL) {
    return -1;
  }

  if (entry->kind == HAL_BSD_FD_UDP) {
    if (!hal_udp_socket_bind(entry->udp, &endpoint)) {
      errno = EIO;
      entry_set_error(entry, errno);
      fd_table_unlock();
      return -1;
    }
    entry->local_endpoint = endpoint;
    entry->has_local_endpoint = true;
    entry->udp_bound = true;
    fd_table_unlock();
    return 0;
  }

  if (entry->kind == HAL_BSD_FD_TCP_SOCKET) {
    entry->local_endpoint = endpoint;
    entry->has_local_endpoint = true;
    fd_table_unlock();
    return 0;
  }

  errno = EINVAL;
  fd_table_unlock();
  return -1;
}

int listen(int sockfd, int backlog) {
  if (backlog <= 0) {
    errno = EINVAL;
    return -1;
  }

  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_TCP_SOCKET || !entry->has_local_endpoint) {
    errno = EINVAL;
    fd_table_unlock();
    return -1;
  }

  hal_tcp_listener_t listener = hal_tcp_listener_open();
  if (!listener) {
    errno = ENOMEM;
    fd_table_unlock();
    return -1;
  }

  if (!hal_tcp_listener_bind(listener, &entry->local_endpoint) ||
      !hal_tcp_listener_listen(listener, (uint8_t)backlog)) {
    hal_tcp_listener_close(listener);
    errno = EIO;
    entry_set_error(entry, errno);
    fd_table_unlock();
    return -1;
  }

  hal_tcp_socket_close(entry->tcp_socket);
  entry->tcp_socket = NULL;
  entry->tcp_listener = listener;
  entry->kind = HAL_BSD_FD_TCP_LISTENER;
  fd_table_unlock();
  return 0;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_TCP_LISTENER) {
    errno = EOPNOTSUPP;
    fd_table_unlock();
    return -1;
  }

  hal_tcp_listener_t listener = entry->tcp_listener;
  const bool nonblocking = entry_is_nonblocking(entry);
  fd_table_unlock();

  hal_net_endpoint_t remote = {};
  hal_tcp_socket_t accepted = hal_tcp_listener_accept(
      listener, &remote, nonblocking ? 0u : HAL_NET_TIMEOUT_FOREVER);
  if (!accepted) {
    errno = nonblocking ? EAGAIN : EINVAL;
    return -1;
  }

  fd_table_relock();
  const int accepted_fd = allocate_fd();
  if (accepted_fd < 0) {
    fd_table_unlock();
    hal_tcp_socket_close(accepted);
    return -1;
  }

  /* The accepted descriptor is not visible to any other caller yet, so it is
   * safe to keep operating on its entry until it is returned. */
  hal_bsd_fd_entry_t *accepted_entry = &s_fd_table[fd_to_index(accepted_fd)];
  accepted_entry->kind = HAL_BSD_FD_TCP_SOCKET;
  accepted_entry->family = entry->family;
  accepted_entry->tcp_socket = accepted;
  accepted_entry->local_endpoint = entry->local_endpoint;
  accepted_entry->remote_endpoint = remote;
  accepted_entry->has_local_endpoint = entry->has_local_endpoint;
  accepted_entry->has_remote_endpoint = true;
  accepted_entry->tcp_was_connected = true;

  if (!write_sockaddr_from_endpoint(&remote, addr, addrlen)) {
    hal_tcp_socket_close(accepted_entry->tcp_socket);
    clear_entry(accepted_entry);
    fd_table_unlock();
    return -1;
  }

  fd_table_unlock();
  return accepted_fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  hal_net_endpoint_t endpoint = {};
  hal_bsd_fd_entry_t *entry =
      prepare_endpoint_operation(sockfd, addr, addrlen, &endpoint);
  if (entry == NULL) {
    return -1;
  }
  if (entry->kind == HAL_BSD_FD_UDP) {
    if (!ensure_udp_bound(entry)) {
      entry_set_error(entry, errno);
      fd_table_unlock();
      return -1;
    }

    entry->remote_endpoint = endpoint;
    entry->has_remote_endpoint = true;
    entry_set_error(entry, 0);
    fd_table_unlock();
    return 0;
  }

  if (entry->kind != HAL_BSD_FD_TCP_SOCKET) {
    errno = EOPNOTSUPP;
    fd_table_unlock();
    return -1;
  }

  hal_tcp_socket_t tcp_socket = entry->tcp_socket;
  const bool nonblocking = entry_is_nonblocking(entry);
  const uint32_t connect_timeout_ms =
      operation_timeout_value(nonblocking, entry->send_timeout_ms);
  fd_table_unlock();

  if (!hal_tcp_socket_connect(tcp_socket, &endpoint, connect_timeout_ms)) {
    errno = nonblocking ? EINPROGRESS : ECONNREFUSED;
    fd_table_relock();
    hal_bsd_fd_entry_t *failed = entry_for_fd(sockfd);
    if (failed && failed->kind == HAL_BSD_FD_TCP_SOCKET &&
        failed->tcp_socket == tcp_socket) {
      entry_set_error(failed, errno);
    }
    fd_table_unlock();
    return -1;
  }

  /* Re-validate: the descriptor may have been closed while the lock was
   * released during the blocking connect. */
  fd_table_relock();
  hal_bsd_fd_entry_t *current = entry_for_fd(sockfd);
  if (current && current->kind == HAL_BSD_FD_TCP_SOCKET &&
      current->tcp_socket == tcp_socket) {
    current->remote_endpoint = endpoint;
    current->has_remote_endpoint = true;
    current->tcp_was_connected = true;
    entry_set_error(current, 0);
  }
  fd_table_unlock();
  return 0;
}

static hal_bsd_fd_entry_t *prepare_io_operation(int sockfd, const void *buffer,
                                                size_t len, int flags) {
  if (!check_message_flags(flags)) {
    return NULL;
  }
  if (len > 0u && buffer == NULL) {
    errno = EINVAL;
    return NULL;
  }
  if (!fd_table_lock()) {
    return NULL;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return NULL;
  }
  if (entry->kind == HAL_BSD_FD_UDP &&
      (!entry->udp_bound || !entry->has_remote_endpoint)) {
    errno = ENOTCONN;
    entry_set_error(entry, errno);
    fd_table_unlock();
    return NULL;
  }
  return entry;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
  hal_bsd_fd_entry_t *entry = prepare_io_operation(sockfd, buf, len, flags);
  if (entry == NULL) {
    return -1;
  }
  if (entry->kind == HAL_BSD_FD_UDP) {

    hal_udp_socket_t udp = entry->udp;
    const hal_net_endpoint_t remote = entry->remote_endpoint;
    fd_table_unlock();

    const int sent = hal_udp_socket_sendto(udp, buf, len, &remote);
    if (sent < 0) {
      errno = EIO;
      fd_table_relock();
      hal_bsd_fd_entry_t *failed = entry_for_fd(sockfd);
      if (failed && failed->kind == HAL_BSD_FD_UDP && failed->udp == udp) {
        entry_set_error(failed, errno);
      }
      fd_table_unlock();
      return -1;
    }
    return (ssize_t)sent;
  }

  if (entry->kind != HAL_BSD_FD_TCP_SOCKET) {
    errno = EOPNOTSUPP;
    fd_table_unlock();
    return -1;
  }

  hal_tcp_socket_t tcp_socket = entry->tcp_socket;
  fd_table_unlock();

  const int sent = hal_tcp_socket_send(tcp_socket, buf, len);
  if (sent < 0) {
    errno = ENOTCONN;
    fd_table_relock();
    hal_bsd_fd_entry_t *failed = entry_for_fd(sockfd);
    if (failed && failed->kind == HAL_BSD_FD_TCP_SOCKET &&
        failed->tcp_socket == tcp_socket) {
      entry_set_error(failed, errno);
    }
    fd_table_unlock();
    return -1;
  }
  return (ssize_t)sent;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
  hal_bsd_fd_entry_t *entry = prepare_io_operation(sockfd, buf, len, flags);
  if (entry == NULL) {
    return -1;
  }
  if (entry->kind == HAL_BSD_FD_UDP) {
    if (len == 0u) {
      fd_table_unlock();
      return 0;
    }

    hal_udp_socket_t udp = entry->udp;
    const uint32_t timeout_ms = io_timeout_value(entry_is_nonblocking(entry),
                                                 flags, entry->recv_timeout_ms);
    fd_table_unlock();

    const int received =
        hal_udp_socket_recvfrom(udp, buf, len, NULL, timeout_ms);
    if (received < 0) {
      errno = EIO;
      fd_table_relock();
      hal_bsd_fd_entry_t *failed = entry_for_fd(sockfd);
      if (failed && failed->kind == HAL_BSD_FD_UDP && failed->udp == udp) {
        entry_set_error(failed, errno);
      }
      fd_table_unlock();
      return -1;
    }
    if (received == 0 && timeout_ms == 0u) {
      errno = EAGAIN;
      return -1;
    }
    return (ssize_t)received;
  }

  if (entry->kind != HAL_BSD_FD_TCP_SOCKET) {
    errno = EOPNOTSUPP;
    fd_table_unlock();
    return -1;
  }

  if (len == 0u) {
    fd_table_unlock();
    return 0;
  }

  hal_tcp_socket_t tcp_socket = entry->tcp_socket;
  const uint32_t timeout_ms = io_timeout_value(entry_is_nonblocking(entry),
                                               flags, entry->recv_timeout_ms);
  fd_table_unlock();

  const int received = hal_tcp_socket_recv(tcp_socket, buf, len, timeout_ms);
  if (received < 0) {
    errno = ENOTCONN;
    fd_table_relock();
    hal_bsd_fd_entry_t *failed = entry_for_fd(sockfd);
    if (failed && failed->kind == HAL_BSD_FD_TCP_SOCKET &&
        failed->tcp_socket == tcp_socket) {
      entry_set_error(failed, errno);
    }
    fd_table_unlock();
    return -1;
  }
  if (received == 0 && timeout_ms == 0u &&
      hal_tcp_socket_is_connected(tcp_socket)) {
    errno = EAGAIN;
    return -1;
  }
  return (ssize_t)received;
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
  if (!check_message_flags(flags)) {
    return -1;
  }
  if (len > 0u && !buf) {
    errno = EINVAL;
    return -1;
  }

  hal_net_endpoint_t remote = {};
  if (!sockaddr_to_endpoint(dest_addr, addrlen, &remote)) {
    return -1;
  }
  if (remote.port == 0u) {
    errno = EINVAL;
    return -1;
  }

  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_UDP) {
    errno = EOPNOTSUPP;
    fd_table_unlock();
    return -1;
  }
  if (remote.family != entry->family) {
    errno = EAFNOSUPPORT;
    fd_table_unlock();
    return -1;
  }
  if (!ensure_udp_bound(entry)) {
    fd_table_unlock();
    return -1;
  }

  hal_udp_socket_t udp = entry->udp;
  fd_table_unlock();

  const int sent = hal_udp_socket_sendto(udp, buf, len, &remote);
  if (sent < 0) {
    errno = EIO;
    fd_table_relock();
    hal_bsd_fd_entry_t *failed = entry_for_fd(sockfd);
    if (failed && failed->kind == HAL_BSD_FD_UDP && failed->udp == udp) {
      entry_set_error(failed, errno);
    }
    fd_table_unlock();
    return -1;
  }
  return (ssize_t)sent;
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
  if (!check_message_flags(flags)) {
    return -1;
  }
  if (len > 0u && !buf) {
    errno = EINVAL;
    return -1;
  }
  if (src_addr && !addrlen) {
    errno = EINVAL;
    return -1;
  }

  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_UDP) {
    errno = EOPNOTSUPP;
    fd_table_unlock();
    return -1;
  }
  if (!entry->udp_bound) {
    errno = EINVAL;
    fd_table_unlock();
    return -1;
  }

  if (len == 0u) {
    fd_table_unlock();
    return 0;
  }

  hal_udp_socket_t udp = entry->udp;
  const uint32_t timeout_ms = io_timeout_value(entry_is_nonblocking(entry),
                                               flags, entry->recv_timeout_ms);
  fd_table_unlock();

  hal_net_endpoint_t remote = {};
  const int received =
      hal_udp_socket_recvfrom(udp, buf, len, &remote, timeout_ms);
  if (received < 0) {
    errno = EIO;
    return -1;
  }
  if (received == 0 && timeout_ms == 0u) {
    errno = EAGAIN;
    return -1;
  }
  if (received > 0 &&
      !write_sockaddr_from_endpoint(&remote, src_addr, addrlen)) {
    return -1;
  }
  return (ssize_t)received;
}

int setsockopt(int sockfd, int level, int optname, const void *optval,
               socklen_t optlen) {
  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }
  if (level != SOL_SOCKET) {
    errno = ENOPROTOOPT;
    fd_table_unlock();
    return -1;
  }
  if (!optval || (optname != SO_RCVTIMEO && optname != SO_SNDTIMEO &&
                  optlen < (socklen_t)sizeof(int))) {
    errno = EINVAL;
    fd_table_unlock();
    return -1;
  }

  if (optname == SO_REUSEADDR || optname == SO_REUSEPORT) {
    int value = 0;
    memcpy(&value, optval, sizeof(value));
    entry->reuse_addr = value != 0;
    fd_table_unlock();
    return 0;
  }
  if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
    uint32_t timeout_ms = HAL_NET_TIMEOUT_FOREVER;
    if (!sockopt_timeval_to_timeout_ms(optval, optlen, &timeout_ms)) {
      fd_table_unlock();
      return -1;
    }
    if (optname == SO_RCVTIMEO) {
      entry->recv_timeout_ms = timeout_ms;
    } else {
      entry->send_timeout_ms = timeout_ms;
    }
    fd_table_unlock();
    return 0;
  }

  errno = ENOPROTOOPT;
  fd_table_unlock();
  return -1;
}

int getsockopt(int sockfd, int level, int optname, void *optval,
               socklen_t *optlen) {
  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }
  if (level != SOL_SOCKET) {
    errno = ENOPROTOOPT;
    fd_table_unlock();
    return -1;
  }

  if (optname == SO_ERROR || optname == SO_REUSEADDR ||
      optname == SO_REUSEPORT) {
    if (!optval || !optlen || *optlen < (socklen_t)sizeof(int)) {
      errno = EINVAL;
      fd_table_unlock();
      return -1;
    }

    int value = 0;
    if (optname == SO_ERROR) {
      value = entry->last_error;
      entry->last_error = 0;
    } else {
      value = entry->reuse_addr ? 1 : 0;
    }

    memcpy(optval, &value, sizeof(value));
    *optlen = (socklen_t)sizeof(value);
    fd_table_unlock();
    return 0;
  }

  if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
    const uint32_t timeout_ms = optname == SO_RCVTIMEO ? entry->recv_timeout_ms
                                                       : entry->send_timeout_ms;
    const bool ok = timeout_ms_to_timeval(timeout_ms, optval, optlen);
    fd_table_unlock();
    return ok ? 0 : -1;
  }

  errno = ENOPROTOOPT;
  fd_table_unlock();
  return -1;
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }

  hal_net_endpoint_t local = entry->local_endpoint;
  if (!entry->has_local_endpoint) {
    memset(&local, 0, sizeof(local));
    local.family = entry->family;
    local.addr_len = entry->family == HAL_NET_AF_INET ? HAL_NET_IPV4_ADDR_LEN
                                                      : HAL_NET_IPV6_ADDR_LEN;
  }

  const bool ok = write_sockaddr_from_endpoint(&local, addr, addrlen);
  fd_table_unlock();
  return ok ? 0 : -1;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }
  if (!entry->has_remote_endpoint) {
    errno = ENOTCONN;
    fd_table_unlock();
    return -1;
  }

  const hal_net_endpoint_t remote = entry->remote_endpoint;
  const bool ok = write_sockaddr_from_endpoint(&remote, addr, addrlen);
  fd_table_unlock();
  return ok ? 0 : -1;
}

static bool timeval_to_timeout_ms(const struct timeval *timeout,
                                  uint32_t *timeout_ms, bool *forever) {
  if (!timeout_ms || !forever) {
    errno = EINVAL;
    return false;
  }

  if (!timeout) {
    *timeout_ms = HAL_NET_TIMEOUT_FOREVER;
    *forever = true;
    return true;
  }

  if (timeout->tv_sec < 0 || timeout->tv_usec < 0 ||
      timeout->tv_usec >= 1000000) {
    errno = EINVAL;
    return false;
  }

  uint64_t total_ms = ((uint64_t)timeout->tv_sec * 1000u) +
                      (((uint64_t)timeout->tv_usec + 999u) / 1000u);
  if (total_ms >= (uint64_t)HAL_NET_TIMEOUT_FOREVER) {
    total_ms = (uint64_t)HAL_NET_TIMEOUT_FOREVER - 1u;
  }

  *timeout_ms = (uint32_t)total_ms;
  *forever = false;
  return true;
}

static bool fd_is_requested(const fd_set *set, int fd) {
  return set && FD_ISSET(fd, set);
}

static bool entry_read_ready(hal_bsd_fd_entry_t *entry) {
  if (!entry) {
    return false;
  }

  if (entry->kind == HAL_BSD_FD_UDP) {
    return entry->udp_bound && hal_udp_socket_can_recv(entry->udp);
  }
  if (entry->kind == HAL_BSD_FD_TCP_SOCKET) {
    return hal_tcp_socket_can_recv(entry->tcp_socket);
  }
  if (entry->kind == HAL_BSD_FD_TCP_LISTENER) {
    return hal_tcp_listener_can_accept(entry->tcp_listener);
  }
  return false;
}

static bool entry_write_ready(hal_bsd_fd_entry_t *entry) {
  if (!entry) {
    return false;
  }

  if (entry->kind == HAL_BSD_FD_UDP) {
    return entry->udp != NULL;
  }
  if (entry->kind == HAL_BSD_FD_TCP_SOCKET) {
    return hal_tcp_socket_can_send(entry->tcp_socket);
  }
  return false;
}

static bool entry_except_ready(hal_bsd_fd_entry_t *entry) {
  if (!entry || entry->kind != HAL_BSD_FD_TCP_SOCKET ||
      !entry->tcp_was_connected) {
    return false;
  }

  return !hal_tcp_socket_is_connected(entry->tcp_socket);
}

static int select_poll_once(int nfds, const fd_set *requested_read,
                            const fd_set *requested_write,
                            const fd_set *requested_except, fd_set *ready_read,
                            fd_set *ready_write, fd_set *ready_except) {
  if (!fd_table_lock()) {
    return -1;
  }
  if (ready_read) {
    FD_ZERO(ready_read);
  }
  if (ready_write) {
    FD_ZERO(ready_write);
  }
  if (ready_except) {
    FD_ZERO(ready_except);
  }

  int ready_count = 0;
  for (int fd = 0; fd < nfds; ++fd) {
    const bool read_requested = fd_is_requested(requested_read, fd);
    const bool write_requested = fd_is_requested(requested_write, fd);
    const bool except_requested = fd_is_requested(requested_except, fd);
    if (!read_requested && !write_requested && !except_requested) {
      continue;
    }

    hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
    if (!entry) {
      errno = EBADF;
      fd_table_unlock();
      return -1;
    }

    if (read_requested && entry_read_ready(entry)) {
      FD_SET(fd, ready_read);
      ready_count++;
    }
    if (write_requested && entry_write_ready(entry)) {
      FD_SET(fd, ready_write);
      ready_count++;
    }
    if (except_requested && entry_except_ready(entry)) {
      FD_SET(fd, ready_except);
      ready_count++;
    }
  }

  fd_table_unlock();
  return ready_count;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout) {
  if (nfds < 0) {
    errno = EINVAL;
    return -1;
  }
#ifdef FD_SETSIZE
  if (nfds > FD_SETSIZE) {
    errno = EINVAL;
    return -1;
  }
#endif

  fd_set requested_read = {};
  fd_set requested_write = {};
  fd_set requested_except = {};
  const fd_set *requested_read_ptr = NULL;
  const fd_set *requested_write_ptr = NULL;
  const fd_set *requested_except_ptr = NULL;
  if (readfds) {
    requested_read = *readfds;
    requested_read_ptr = &requested_read;
  }
  if (writefds) {
    requested_write = *writefds;
    requested_write_ptr = &requested_write;
  }
  if (exceptfds) {
    requested_except = *exceptfds;
    requested_except_ptr = &requested_except;
  }

  uint32_t timeout_ms = 0u;
  bool wait_forever = false;
  if (!timeval_to_timeout_ms(timeout, &timeout_ms, &wait_forever)) {
    return -1;
  }

  const uint32_t start_ms = hal_millis();
  for (;;) {
    const int ready =
        select_poll_once(nfds, requested_read_ptr, requested_write_ptr,
                         requested_except_ptr, readfds, writefds, exceptfds);
    if (ready != 0) {
      return ready;
    }
    if (timeout_ms == 0u && !wait_forever) {
      return 0;
    }
    if (!wait_forever && hal_millis_deadline_expired(start_ms, timeout_ms)) {
      return 0;
    }

    hal_idle();
    hal_delay_ms(1u);
  }
}

int shutdown(int sockfd, int how) {
  if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR) {
    errno = EINVAL;
    return -1;
  }

  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_TCP_SOCKET) {
    errno = EOPNOTSUPP;
    fd_table_unlock();
    return -1;
  }

  hal_tcp_socket_shutdown(entry->tcp_socket);
  fd_table_unlock();
  return 0;
}

int close(int fd) {
  if (!fd_table_lock()) {
    return -1;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
  if (!entry) {
    fd_table_unlock();
    return -1;
  }

  if (entry->kind == HAL_BSD_FD_UDP) {
    hal_udp_socket_close(entry->udp);
  } else if (entry->kind == HAL_BSD_FD_TCP_SOCKET) {
    hal_tcp_socket_close(entry->tcp_socket);
  } else if (entry->kind == HAL_BSD_FD_TCP_LISTENER) {
    hal_tcp_listener_close(entry->tcp_listener);
  }

  clear_entry(entry);
  fd_table_unlock();
  return 0;
}

ssize_t read(int fd, void *buf, size_t count) {
  return recv(fd, buf, count, 0);
}

ssize_t write(int fd, const void *buf, size_t count) {
  return send(fd, buf, count, 0);
}

#if HAL_TARGET_IS_MOCK
void hal_mock_bsd_sockets_reset(void) {
  if (!fd_table_lock()) {
    return;
  }
  for (size_t i = 0u; i < HAL_BSD_SOCKET_MAX_FDS; ++i) {
    if (s_fd_table[i].kind == HAL_BSD_FD_UDP) {
      hal_udp_socket_close(s_fd_table[i].udp);
    } else if (s_fd_table[i].kind == HAL_BSD_FD_TCP_SOCKET) {
      hal_tcp_socket_close(s_fd_table[i].tcp_socket);
    } else if (s_fd_table[i].kind == HAL_BSD_FD_TCP_LISTENER) {
      hal_tcp_listener_close(s_fd_table[i].tcp_listener);
    }
    clear_entry(&s_fd_table[i]);
  }
  s_next_ephemeral_udp_port = 49152u;
  fd_table_unlock();
  /* Test-only: force the singleton mutex through a real destroy so
   * Helgrind/DRD can observe the teardown path. Firmware never resets this
   * table - the mutex is a process-lifetime singleton by design. */
  hal_mutex_destroy(s_fd_table_mutex);
  s_fd_table_mutex = NULL;
}

hal_udp_socket_t hal_mock_bsd_socket_get_udp_handle(int fd) {
  if (!fd_table_lock()) {
    return NULL;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
  if (!entry || entry->kind != HAL_BSD_FD_UDP) {
    fd_table_unlock();
    return NULL;
  }
  hal_udp_socket_t udp = entry->udp;
  fd_table_unlock();
  return udp;
}

hal_tcp_socket_t hal_mock_bsd_socket_get_tcp_handle(int fd) {
  if (!fd_table_lock()) {
    return NULL;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
  if (!entry || entry->kind != HAL_BSD_FD_TCP_SOCKET) {
    fd_table_unlock();
    return NULL;
  }
  hal_tcp_socket_t tcp_socket = entry->tcp_socket;
  fd_table_unlock();
  return tcp_socket;
}

hal_tcp_listener_t hal_mock_bsd_socket_get_tcp_listener(int fd) {
  if (!fd_table_lock()) {
    return NULL;
  }
  hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
  if (!entry || entry->kind != HAL_BSD_FD_TCP_LISTENER) {
    fd_table_unlock();
    return NULL;
  }
  hal_tcp_listener_t tcp_listener = entry->tcp_listener;
  fd_table_unlock();
  return tcp_listener;
}
#endif

#endif /* HAL_ENABLE_BSD_SOCKETS */
