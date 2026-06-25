#include "../../hal_config.h"

#ifdef HAL_ENABLE_BSD_SOCKETS

#include "../../hal_net.h"
#include "../../hal_system.h"
#include "../../hal_target.h"
#include "../../hal_tcp.h"
#include "../../hal_udp.h"

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
  int status_flags;
  bool has_local_endpoint;
  bool udp_bound;
  bool reuse_addr;
} hal_bsd_fd_entry_t;

typedef struct {
  struct addrinfo info;
  struct sockaddr_in addr;
  char canonname[HAL_BSD_ADDRINFO_CANONNAME_MAX];
} hal_bsd_addrinfo_node_t;

static hal_bsd_fd_entry_t s_fd_table[HAL_BSD_SOCKET_MAX_FDS];
static uint16_t s_next_ephemeral_udp_port = 49152u;

static void clear_entry(hal_bsd_fd_entry_t *entry) {
  if (!entry) {
    return;
  }
  memset(entry, 0, sizeof(*entry));
  entry->kind = HAL_BSD_FD_UNUSED;
  entry->local_endpoint.family = HAL_NET_AF_UNSPEC;
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
  return ((uint32_t)endpoint->addr[0] << 24u) |
         ((uint32_t)endpoint->addr[1] << 16u) |
         ((uint32_t)endpoint->addr[2] << 8u) | (uint32_t)endpoint->addr[3];
}

static void endpoint_addr_from_host_u32(hal_net_endpoint_t *endpoint,
                                        uint32_t host_addr) {
  endpoint->addr[0] = (uint8_t)((host_addr >> 24u) & 0xFFu);
  endpoint->addr[1] = (uint8_t)((host_addr >> 16u) & 0xFFu);
  endpoint->addr[2] = (uint8_t)((host_addr >> 8u) & 0xFFu);
  endpoint->addr[3] = (uint8_t)(host_addr & 0xFFu);
}

static uint32_t
ipv4_addr_to_host_u32(const uint8_t addr[HAL_NET_IPV4_ADDR_LEN]) {
  return ((uint32_t)addr[0] << 24u) | ((uint32_t)addr[1] << 16u) |
         ((uint32_t)addr[2] << 8u) | (uint32_t)addr[3];
}

static bool sockaddr_to_endpoint(const struct sockaddr *addr, socklen_t addrlen,
                                 hal_net_endpoint_t *endpoint) {
  if (!addr || !endpoint) {
    errno = EINVAL;
    return false;
  }
  if (addrlen < (socklen_t)sizeof(struct sockaddr_in)) {
    errno = EINVAL;
    return false;
  }
  if (addr->sa_family != AF_INET) {
    errno = EAFNOSUPPORT;
    return false;
  }

  struct sockaddr_in sin = {};
  memcpy(&sin, addr, sizeof(sin));

  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->family = HAL_NET_AF_INET;
  endpoint->port = ntohs(sin.sin_port);
  endpoint_addr_from_host_u32(endpoint, ntohl(sin.sin_addr.s_addr));
  return true;
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
  if (!endpoint || endpoint->family != HAL_NET_AF_INET) {
    errno = EAFNOSUPPORT;
    return false;
  }

  struct sockaddr_in sin = {};
  sin.sin_family = AF_INET;
  sin.sin_port = htons(endpoint->port);
  sin.sin_addr.s_addr = htonl(endpoint_addr_to_host_u32(endpoint));

  const socklen_t requested = *addrlen;
  const socklen_t actual = (socklen_t)sizeof(sin);
  const socklen_t to_copy = requested < actual ? requested : actual;
  if (to_copy > 0u) {
    memcpy(addr, &sin, (size_t)to_copy);
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

static uint32_t io_timeout_for_entry(const hal_bsd_fd_entry_t *entry,
                                     int message_flags) {
  if (entry_is_nonblocking(entry) ||
      message_flags_request_nonblocking(message_flags)) {
    return 0u;
  }
  return HAL_NET_TIMEOUT_FOREVER;
}

static uint16_t next_ephemeral_udp_port(void) {
  const uint16_t port = s_next_ephemeral_udp_port;
  s_next_ephemeral_udp_port++;
  if (s_next_ephemeral_udp_port < 49152u) {
    s_next_ephemeral_udp_port = 49152u;
  }
  return port;
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
  local.family = HAL_NET_AF_INET;
  local.port = next_ephemeral_udp_port();
  if (!hal_udp_socket_bind(entry->udp, &local)) {
    errno = EIO;
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

static bool format_ipv4_canonname(hal_bsd_addrinfo_node_t *node,
                                  const uint8_t addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (!node || !addr) {
    return false;
  }

  const int written =
      snprintf(node->canonname, sizeof(node->canonname), "%u.%u.%u.%u",
               (unsigned)addr[0], (unsigned)addr[1], (unsigned)addr[2],
               (unsigned)addr[3]);
  if (written < 0 || (size_t)written >= sizeof(node->canonname)) {
    return false;
  }

  node->info.ai_canonname = node->canonname;
  return true;
}

static int resolve_addrinfo_ipv4(const char *node, int flags,
                                 uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (!out_addr) {
    return EAI_FAIL;
  }

  if (!node) {
    const uint32_t host_addr =
        (flags & AI_PASSIVE) != 0 ? INADDR_ANY : INADDR_LOOPBACK;
    out_addr[0] = (uint8_t)((host_addr >> 24u) & 0xFFu);
    out_addr[1] = (uint8_t)((host_addr >> 16u) & 0xFFu);
    out_addr[2] = (uint8_t)((host_addr >> 8u) & 0xFFu);
    out_addr[3] = (uint8_t)(host_addr & 0xFFu);
    return 0;
  }

  if (node[0] == '\0') {
    return EAI_NONAME;
  }

  if ((flags & AI_NUMERICHOST) != 0) {
    return parse_ipv4_string(node, out_addr) ? 0 : EAI_NONAME;
  }

  return hal_net_resolve_ipv4(node, out_addr) ? 0 : EAI_NONAME;
}

in_addr_t inet_addr(const char *cp) {
  uint8_t octets[4] = {};
  if (!parse_ipv4_string(cp, octets)) {
    return INADDR_NONE;
  }

  const uint32_t host_addr = ((uint32_t)octets[0] << 24u) |
                             ((uint32_t)octets[1] << 16u) |
                             ((uint32_t)octets[2] << 8u) | (uint32_t)octets[3];
  return htonl(host_addr);
}

int inet_pton(int af, const char *src, void *dst) {
  if (af != AF_INET) {
    errno = EAFNOSUPPORT;
    return -1;
  }
  if (!dst) {
    errno = EINVAL;
    return -1;
  }

  uint8_t octets[4] = {};
  if (!parse_ipv4_string(src, octets)) {
    return 0;
  }

  const uint32_t host_addr = ((uint32_t)octets[0] << 24u) |
                             ((uint32_t)octets[1] << 16u) |
                             ((uint32_t)octets[2] << 8u) | (uint32_t)octets[3];
  const in_addr_t net_addr = htonl(host_addr);
  memcpy(dst, &net_addr, sizeof(net_addr));
  return 1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
  if (af != AF_INET) {
    errno = EAFNOSUPPORT;
    return NULL;
  }
  if (!src || !dst || size == 0u) {
    errno = EINVAL;
    return NULL;
  }

  in_addr_t net_addr = 0u;
  memcpy(&net_addr, src, sizeof(net_addr));
  const uint32_t host_addr = ntohl(net_addr);
  const int written = snprintf(
      dst, (size_t)size, "%u.%u.%u.%u", (unsigned)((host_addr >> 24u) & 0xFFu),
      (unsigned)((host_addr >> 16u) & 0xFFu),
      (unsigned)((host_addr >> 8u) & 0xFFu), (unsigned)(host_addr & 0xFFu));
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
  if (family != AF_UNSPEC && family != AF_INET) {
    return EAI_FAMILY;
  }
  if (!addrinfo_socktype_protocol_supported(socktype, protocol)) {
    return EAI_SOCKTYPE;
  }

  uint16_t port = 0u;
  if (!parse_service_port(service, &port)) {
    return EAI_SERVICE;
  }

  uint8_t addr[HAL_NET_IPV4_ADDR_LEN] = {};
  const int resolve_result = resolve_addrinfo_ipv4(node, flags, addr);
  if (resolve_result != 0) {
    return resolve_result;
  }

  hal_bsd_addrinfo_node_t *out =
      (hal_bsd_addrinfo_node_t *)calloc(1u, sizeof(*out));
  if (!out) {
    return EAI_MEMORY;
  }

  out->addr.sin_family = AF_INET;
  out->addr.sin_port = htons(port);
  out->addr.sin_addr.s_addr = htonl(ipv4_addr_to_host_u32(addr));

  out->info.ai_family = AF_INET;
  out->info.ai_socktype = socktype;
  out->info.ai_protocol = addrinfo_protocol_for(socktype, protocol);
  out->info.ai_addrlen = (socklen_t)sizeof(out->addr);
  out->info.ai_addr = (struct sockaddr *)&out->addr;
  out->info.ai_next = NULL;

  if ((flags & AI_CANONNAME) != 0) {
    const bool ok =
        node ? copy_canonname(out, node) : format_ipv4_canonname(out, addr);
    if (!ok) {
      free(out);
      return EAI_MEMORY;
    }
  }

  *res = &out->info;
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
  hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
  if (!entry) {
    return -1;
  }

  if (cmd == F_GETFL) {
    return entry->status_flags;
  }

  if (cmd == F_SETFL) {
    va_list args;
    va_start(args, cmd);
    const int flags = va_arg(args, int);
    va_end(args);

    if (!file_status_flags_supported(flags)) {
      errno = EINVAL;
      return -1;
    }

    entry->status_flags = flags & O_NONBLOCK;
    return 0;
  }

  errno = EINVAL;
  return -1;
}

int socket(int domain, int type, int protocol) {
  if (domain != AF_INET && domain != PF_INET) {
    errno = EAFNOSUPPORT;
    return -1;
  }

  if (type == SOCK_DGRAM && (protocol == 0 || protocol == IPPROTO_UDP)) {
    const int fd = allocate_fd();
    if (fd < 0) {
      return -1;
    }

    hal_udp_socket_t udp = hal_udp_socket_open();
    if (!udp) {
      clear_entry(&s_fd_table[fd_to_index(fd)]);
      errno = ENOMEM;
      return -1;
    }

    hal_bsd_fd_entry_t *entry = &s_fd_table[fd_to_index(fd)];
    entry->kind = HAL_BSD_FD_UDP;
    entry->udp = udp;
    return fd;
  }

  if (type == SOCK_STREAM && (protocol == 0 || protocol == IPPROTO_TCP)) {
    const int fd = allocate_fd();
    if (fd < 0) {
      return -1;
    }

    hal_tcp_socket_t tcp = hal_tcp_socket_open();
    if (!tcp) {
      clear_entry(&s_fd_table[fd_to_index(fd)]);
      errno = ENOMEM;
      return -1;
    }

    hal_bsd_fd_entry_t *entry = &s_fd_table[fd_to_index(fd)];
    entry->kind = HAL_BSD_FD_TCP_SOCKET;
    entry->tcp_socket = tcp;
    return fd;
  }

  errno = (type == SOCK_DGRAM || type == SOCK_STREAM) ? EPROTONOSUPPORT
                                                      : ESOCKTNOSUPPORT;
  return -1;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  hal_net_endpoint_t endpoint = {};
  if (!sockaddr_to_endpoint(addr, addrlen, &endpoint)) {
    return -1;
  }
  if (endpoint.port == 0u) {
    errno = EINVAL;
    return -1;
  }

  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    return -1;
  }

  if (entry->kind == HAL_BSD_FD_UDP) {
    if (!hal_udp_socket_bind(entry->udp, &endpoint)) {
      errno = EIO;
      return -1;
    }
    entry->local_endpoint = endpoint;
    entry->has_local_endpoint = true;
    entry->udp_bound = true;
    return 0;
  }

  if (entry->kind == HAL_BSD_FD_TCP_SOCKET) {
    entry->local_endpoint = endpoint;
    entry->has_local_endpoint = true;
    return 0;
  }

  errno = EINVAL;
  return -1;
}

int listen(int sockfd, int backlog) {
  if (backlog <= 0) {
    errno = EINVAL;
    return -1;
  }

  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_TCP_SOCKET || !entry->has_local_endpoint) {
    errno = EINVAL;
    return -1;
  }

  hal_tcp_listener_t listener = hal_tcp_listener_open();
  if (!listener) {
    errno = ENOMEM;
    return -1;
  }

  if (!hal_tcp_listener_bind(listener, &entry->local_endpoint) ||
      !hal_tcp_listener_listen(listener, (uint8_t)backlog)) {
    hal_tcp_listener_close(listener);
    errno = EIO;
    return -1;
  }

  hal_tcp_socket_close(entry->tcp_socket);
  entry->tcp_socket = NULL;
  entry->tcp_listener = listener;
  entry->kind = HAL_BSD_FD_TCP_LISTENER;
  return 0;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_TCP_LISTENER) {
    errno = EOPNOTSUPP;
    return -1;
  }

  hal_net_endpoint_t remote = {};
  hal_tcp_socket_t accepted = hal_tcp_listener_accept(
      entry->tcp_listener, &remote,
      entry_is_nonblocking(entry) ? 0u : HAL_NET_TIMEOUT_FOREVER);
  if (!accepted) {
    errno = EAGAIN;
    return -1;
  }

  const int accepted_fd = allocate_fd();
  if (accepted_fd < 0) {
    hal_tcp_socket_close(accepted);
    return -1;
  }

  hal_bsd_fd_entry_t *accepted_entry = &s_fd_table[fd_to_index(accepted_fd)];
  accepted_entry->kind = HAL_BSD_FD_TCP_SOCKET;
  accepted_entry->tcp_socket = accepted;

  if (!write_sockaddr_from_endpoint(&remote, addr, addrlen)) {
    close(accepted_fd);
    return -1;
  }

  return accepted_fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  hal_net_endpoint_t endpoint = {};
  if (!sockaddr_to_endpoint(addr, addrlen, &endpoint)) {
    return -1;
  }
  if (endpoint.port == 0u) {
    errno = EINVAL;
    return -1;
  }

  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_TCP_SOCKET) {
    errno = EOPNOTSUPP;
    return -1;
  }

  const uint32_t timeout_ms =
      entry_is_nonblocking(entry) ? 0u : HAL_NET_TIMEOUT_FOREVER;
  if (!hal_tcp_socket_connect(entry->tcp_socket, &endpoint, timeout_ms)) {
    errno = entry_is_nonblocking(entry) ? EINPROGRESS : ECONNREFUSED;
    return -1;
  }

  return 0;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
  if (!check_message_flags(flags)) {
    return -1;
  }
  if (len > 0u && !buf) {
    errno = EINVAL;
    return -1;
  }

  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_TCP_SOCKET) {
    errno = EOPNOTSUPP;
    return -1;
  }

  const int sent = hal_tcp_socket_send(entry->tcp_socket, buf, len);
  if (sent < 0) {
    errno = ENOTCONN;
    return -1;
  }
  return (ssize_t)sent;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
  if (!check_message_flags(flags)) {
    return -1;
  }
  if (len > 0u && !buf) {
    errno = EINVAL;
    return -1;
  }

  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_TCP_SOCKET) {
    errno = EOPNOTSUPP;
    return -1;
  }

  if (len == 0u) {
    return 0;
  }

  const uint32_t timeout_ms = io_timeout_for_entry(entry, flags);
  const int received =
      hal_tcp_socket_recv(entry->tcp_socket, buf, len, timeout_ms);
  if (received < 0) {
    errno = ENOTCONN;
    return -1;
  }
  if (received == 0 && timeout_ms == 0u &&
      hal_tcp_socket_is_connected(entry->tcp_socket)) {
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

  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_UDP) {
    errno = EOPNOTSUPP;
    return -1;
  }
  if (!ensure_udp_bound(entry)) {
    return -1;
  }

  const int sent = hal_udp_socket_sendto(entry->udp, buf, len, &remote);
  if (sent < 0) {
    errno = EIO;
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

  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_UDP) {
    errno = EOPNOTSUPP;
    return -1;
  }
  if (!entry->udp_bound) {
    errno = EINVAL;
    return -1;
  }

  if (len == 0u) {
    return 0;
  }

  hal_net_endpoint_t remote = {};
  const uint32_t timeout_ms = io_timeout_for_entry(entry, flags);
  const int received =
      hal_udp_socket_recvfrom(entry->udp, buf, len, &remote, timeout_ms);
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
  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    return -1;
  }
  if (level != SOL_SOCKET) {
    errno = ENOPROTOOPT;
    return -1;
  }
  if (!optval || optlen < (socklen_t)sizeof(int)) {
    errno = EINVAL;
    return -1;
  }

  int value = 0;
  memcpy(&value, optval, sizeof(value));
  if (optname == SO_REUSEADDR || optname == SO_REUSEPORT) {
    entry->reuse_addr = value != 0;
    return 0;
  }

  errno = ENOPROTOOPT;
  return -1;
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

static int select_poll_once(int nfds, const fd_set *requested_read,
                            const fd_set *requested_write,
                            const fd_set *requested_except, fd_set *ready_read,
                            fd_set *ready_write, fd_set *ready_except) {
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
  }

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
    if (!wait_forever && (uint32_t)(hal_millis() - start_ms) >= timeout_ms) {
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

  hal_bsd_fd_entry_t *entry = entry_for_fd(sockfd);
  if (!entry) {
    return -1;
  }
  if (entry->kind != HAL_BSD_FD_TCP_SOCKET) {
    errno = EOPNOTSUPP;
    return -1;
  }

  hal_tcp_socket_shutdown(entry->tcp_socket);
  return 0;
}

int close(int fd) {
  hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
  if (!entry) {
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
}

hal_udp_socket_t hal_mock_bsd_socket_get_udp_handle(int fd) {
  hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
  if (!entry || entry->kind != HAL_BSD_FD_UDP) {
    return NULL;
  }
  return entry->udp;
}

hal_tcp_socket_t hal_mock_bsd_socket_get_tcp_handle(int fd) {
  hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
  if (!entry || entry->kind != HAL_BSD_FD_TCP_SOCKET) {
    return NULL;
  }
  return entry->tcp_socket;
}

hal_tcp_listener_t hal_mock_bsd_socket_get_tcp_listener(int fd) {
  hal_bsd_fd_entry_t *entry = entry_for_fd(fd);
  if (!entry || entry->kind != HAL_BSD_FD_TCP_LISTENER) {
    return NULL;
  }
  return entry->tcp_listener;
}
#endif

#endif /* HAL_ENABLE_BSD_SOCKETS */
