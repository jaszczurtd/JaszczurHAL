#pragma once

/**
 * @file jh_native_socket_platform.h
 * @brief Platform layer of the shared native BSD socket implementation.
 *
 * The socket core (jh_native_sockets.cpp) calls only the helpers below. Each
 * helper has the same signature on lwIP (ESP-IDF), POSIX hosts and Winsock
 * hosts, so platform differences stay in this header: function names, the
 * descriptor type, error codes, readiness waiting, non-blocking mode,
 * descriptor inheritance, listener address reuse and truncated datagrams.
 */

#include "hal/core/hal_config.h"
#include "hal/network/hal_net.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(HAL_NETWORK_BACKEND_ESP_IDF)
#include <lwip/errno.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <errno.h>
#include <fcntl.h>
#define JH_NATIVE_SOCKETS_LWIP 1
#elif defined(HAL_NETWORK_BACKEND_HOST) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define JH_NATIVE_SOCKETS_WINSOCK 1
#elif defined(HAL_NETWORK_BACKEND_HOST)
/* The JaszczurHAL include root continues these lookups to the system headers
 * for the host backend (see src/sys/socket.h). */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define JH_NATIVE_SOCKETS_POSIX 1
#else
#error                                                                         \
    "Native sockets require HAL_NETWORK_BACKEND_ESP_IDF or HAL_NETWORK_BACKEND_HOST"
#endif

#if defined(JH_NATIVE_SOCKETS_WINSOCK)
typedef SOCKET jh_native_fd_t;
typedef int jh_native_socklen_t;
#define JH_NATIVE_FD_INVALID INVALID_SOCKET
#define JH_NATIVE_SHUT_RDWR SD_BOTH
#else
typedef int jh_native_fd_t;
typedef socklen_t jh_native_socklen_t;
#define JH_NATIVE_FD_INVALID (-1)
#define JH_NATIVE_SHUT_RDWR SHUT_RDWR
#endif

#if defined(JH_NATIVE_SOCKETS_LWIP)
#define JH_NATIVE_HAS_IPV6 LWIP_IPV6
#else
#define JH_NATIVE_HAS_IPV6 1
#endif

/** Portable class of a native socket error code. */
typedef enum {
  JH_NATIVE_ERR_NONE = 0,
  JH_NATIVE_ERR_INVALID,     /**< Bad argument or descriptor. */
  JH_NATIVE_ERR_UNSUPPORTED, /**< Family, protocol or operation unsupported. */
  JH_NATIVE_ERR_NOMEM,       /**< No memory, buffers or descriptors. */
  JH_NATIVE_ERR_BUSY,        /**< Address already in use. */
  JH_NATIVE_ERR_IN_PROGRESS, /**< Connection attempt already running. */
  JH_NATIVE_ERR_WOULD_BLOCK, /**< Non-blocking operation would block. */
  JH_NATIVE_ERR_TIMEOUT,     /**< Operation timed out. */
  JH_NATIVE_ERR_INTERRUPTED, /**< A signal interrupted the call. */
  JH_NATIVE_ERR_IO           /**< Connection or network failure. */
} jh_native_error_t;

#if defined(JH_NATIVE_SOCKETS_WINSOCK)

static inline int jh_native_last_error(void) { return WSAGetLastError(); }

static inline jh_native_error_t jh_native_classify(int error) {
  switch (error) {
  case 0:
    return JH_NATIVE_ERR_NONE;
  case WSAEINVAL:
  case WSAENOTSOCK:
  case WSAEFAULT:
  case WSAEBADF:
    return JH_NATIVE_ERR_INVALID;
  case WSAEAFNOSUPPORT:
  case WSAEPROTONOSUPPORT:
  case WSAEOPNOTSUPP:
    return JH_NATIVE_ERR_UNSUPPORTED;
  case WSAENOBUFS:
  case WSAEMFILE:
  case WSA_NOT_ENOUGH_MEMORY:
    return JH_NATIVE_ERR_NOMEM;
  case WSAEADDRINUSE:
    return JH_NATIVE_ERR_BUSY;
  case WSAEALREADY:
  case WSAEINPROGRESS:
    return JH_NATIVE_ERR_IN_PROGRESS;
  case WSAEWOULDBLOCK:
    return JH_NATIVE_ERR_WOULD_BLOCK;
  case WSAETIMEDOUT:
    return JH_NATIVE_ERR_TIMEOUT;
  case WSAEINTR:
    return JH_NATIVE_ERR_INTERRUPTED;
  default:
    return JH_NATIVE_ERR_IO;
  }
}

#else

static inline int jh_native_last_error(void) { return errno; }

/* An if-chain, not a switch: several of these names share one value on some
 * C libraries (EAGAIN/EWOULDBLOCK, EOPNOTSUPP/ENOTSUP). */
static inline jh_native_error_t jh_native_classify(int error) {
  if (error == 0) {
    return JH_NATIVE_ERR_NONE;
  }
  if (error == EINVAL || error == EBADF || error == ENOTSOCK) {
    return JH_NATIVE_ERR_INVALID;
  }
  if (error == EAFNOSUPPORT || error == EPROTONOSUPPORT ||
      error == EOPNOTSUPP) {
    return JH_NATIVE_ERR_UNSUPPORTED;
  }
  if (error == ENOMEM || error == ENOBUFS || error == EMFILE ||
      error == ENFILE) {
    return JH_NATIVE_ERR_NOMEM;
  }
  if (error == EADDRINUSE) {
    return JH_NATIVE_ERR_BUSY;
  }
  if (error == EALREADY || error == EINPROGRESS) {
    return JH_NATIVE_ERR_IN_PROGRESS;
  }
  if (error == EAGAIN || error == EWOULDBLOCK) {
    return JH_NATIVE_ERR_WOULD_BLOCK;
  }
  if (error == ETIMEDOUT) {
    return JH_NATIVE_ERR_TIMEOUT;
  }
  if (error == EINTR) {
    return JH_NATIVE_ERR_INTERRUPTED;
  }
  return JH_NATIVE_ERR_IO;
}

#endif

#if defined(JH_NATIVE_SOCKETS_WINSOCK)
/** Winsock takes int lengths; larger requests transfer a first part. */
static inline int jh_native_io_length(size_t length) {
  return length > (size_t)INT_MAX ? INT_MAX : (int)length;
}

/** Child processes started by a host tool must not inherit sockets. */
static inline jh_native_fd_t jh_native_no_inherit(jh_native_fd_t fd) {
  if (fd != JH_NATIVE_FD_INVALID) {
    (void)SetHandleInformation((HANDLE)fd, HANDLE_FLAG_INHERIT, 0);
  }
  return fd;
}
#elif defined(JH_NATIVE_SOCKETS_POSIX)
/** Child processes started by a host tool must not inherit sockets. */
static inline jh_native_fd_t jh_native_no_inherit(jh_native_fd_t fd) {
  if (fd >= 0) {
    const int flags = fcntl(fd, F_GETFD, 0);
    if (flags >= 0) {
      (void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
  }
  return fd;
}
#endif

static inline jh_native_fd_t jh_native_socket(int family, int type,
                                              int protocol) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_socket(family, type, protocol);
#elif defined(JH_NATIVE_SOCKETS_WINSOCK)
  return jh_native_no_inherit(socket(family, type, protocol));
#elif defined(SOCK_CLOEXEC)
  return socket(family, type | SOCK_CLOEXEC, protocol);
#else
  return jh_native_no_inherit(socket(family, type, protocol));
#endif
}

static inline int jh_native_close(jh_native_fd_t fd) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_close(fd);
#elif defined(JH_NATIVE_SOCKETS_WINSOCK)
  return closesocket(fd);
#else
  return close(fd);
#endif
}

static inline int jh_native_bind(jh_native_fd_t fd,
                                 const struct sockaddr *address,
                                 jh_native_socklen_t length) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_bind(fd, address, length);
#else
  return bind(fd, address, length);
#endif
}

static inline int jh_native_listen(jh_native_fd_t fd, int backlog) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_listen(fd, backlog);
#else
  return listen(fd, backlog);
#endif
}

static inline jh_native_fd_t jh_native_accept(jh_native_fd_t fd,
                                              struct sockaddr *address,
                                              jh_native_socklen_t *length) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_accept(fd, address, length);
#elif defined(JH_NATIVE_SOCKETS_POSIX) && defined(SOCK_CLOEXEC)
  return accept4(fd, address, length, SOCK_CLOEXEC);
#else
  return jh_native_no_inherit(accept(fd, address, length));
#endif
}

static inline int jh_native_connect(jh_native_fd_t fd,
                                    const struct sockaddr *address,
                                    jh_native_socklen_t length) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_connect(fd, address, length);
#else
  return connect(fd, address, length);
#endif
}

/** Send on a connected socket; a closed peer reports an error, never SIGPIPE.
 */
static inline intptr_t jh_native_send(jh_native_fd_t fd, const void *data,
                                      size_t length) {
#if defined(MSG_NOSIGNAL)
  const int flags = MSG_NOSIGNAL;
#else
  const int flags = 0;
#endif
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_send(fd, data, length, flags);
#elif defined(JH_NATIVE_SOCKETS_WINSOCK)
  return send(fd, (const char *)data, jh_native_io_length(length), flags);
#else
  return send(fd, data, length, flags);
#endif
}

/**
 * Receive after a readiness wait. Winsock has no MSG_DONTWAIT; a socket that
 * reported readable data or end of stream does not block there.
 */
static inline intptr_t jh_native_recv(jh_native_fd_t fd, void *buffer,
                                      size_t length) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_recv(fd, buffer, length, MSG_DONTWAIT);
#elif defined(JH_NATIVE_SOCKETS_WINSOCK)
  return recv(fd, (char *)buffer, jh_native_io_length(length), 0);
#else
  return recv(fd, buffer, length, MSG_DONTWAIT);
#endif
}

static inline intptr_t jh_native_sendto(jh_native_fd_t fd, const void *data,
                                        size_t length,
                                        const struct sockaddr *address,
                                        jh_native_socklen_t address_length) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_sendto(fd, data, length, 0, address, address_length);
#elif defined(JH_NATIVE_SOCKETS_WINSOCK)
  return sendto(fd, (const char *)data, jh_native_io_length(length), 0, address,
                address_length);
#else
  return sendto(fd, data, length, 0, address, address_length);
#endif
}

static inline intptr_t jh_native_recvfrom(jh_native_fd_t fd, void *buffer,
                                          size_t length,
                                          struct sockaddr *address,
                                          jh_native_socklen_t *address_length) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_recvfrom(fd, buffer, length, MSG_DONTWAIT, address,
                       address_length);
#elif defined(JH_NATIVE_SOCKETS_WINSOCK)
  const int received = recvfrom(fd, (char *)buffer, jh_native_io_length(length),
                                0, address, address_length);
  // Winsock reports a datagram longer than the buffer as an error after
  // filling the buffer and consuming the datagram; the other platforms return
  // the truncated length.
  return received == SOCKET_ERROR && WSAGetLastError() == WSAEMSGSIZE
             ? jh_native_io_length(length)
             : received;
#else
  return recvfrom(fd, buffer, length, MSG_DONTWAIT, address, address_length);
#endif
}

/** Read and clear the pending socket error (SO_ERROR); 0 on success. */
static inline int jh_native_socket_error(jh_native_fd_t fd, int *out_error) {
  jh_native_socklen_t size = (jh_native_socklen_t)sizeof(*out_error);
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, out_error, &size);
#elif defined(JH_NATIVE_SOCKETS_WINSOCK)
  return getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)out_error, &size);
#else
  return getsockopt(fd, SOL_SOCKET, SO_ERROR, out_error, &size);
#endif
}

static inline int jh_native_getpeername(jh_native_fd_t fd,
                                        struct sockaddr *address,
                                        jh_native_socklen_t *length) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_getpeername(fd, address, length);
#else
  return getpeername(fd, address, length);
#endif
}

static inline void jh_native_shutdown(jh_native_fd_t fd) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  (void)lwip_shutdown(fd, JH_NATIVE_SHUT_RDWR);
#else
  (void)shutdown(fd, JH_NATIVE_SHUT_RDWR);
#endif
}

/**
 * Let a listener bind again right after a previous listener closed. Winsock's
 * SO_REUSEADDR would let another process take over a bound port, so Windows
 * claims the address exclusively instead.
 */
static inline void jh_native_prepare_listener(jh_native_fd_t fd) {
  const int enable = 1;
#if defined(JH_NATIVE_SOCKETS_LWIP)
  (void)lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#elif defined(JH_NATIVE_SOCKETS_WINSOCK)
  (void)setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&enable,
                   (int)sizeof(enable));
#else
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#endif
}

/**
 * Put the socket in non-blocking mode; 0 on success. HAL sockets stay in it
 * for their whole life: a send to a peer that stops reading returns a partial
 * count or would-block instead of stalling the caller's loop.
 */
static inline int jh_native_make_nonblocking(jh_native_fd_t fd) {
#if defined(JH_NATIVE_SOCKETS_WINSOCK)
  u_long mode = 1u;
  return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#elif defined(JH_NATIVE_SOCKETS_LWIP)
  const int flags = lwip_fcntl(fd, F_GETFL, 0);
  return flags < 0 || lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ? -1 : 0;
#else
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ? -1 : 0;
#endif
}

#if defined(JH_NATIVE_SOCKETS_POSIX)
static inline uint64_t jh_native_monotonic_ms(void) {
  struct timespec now = {};
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * UINT64_C(1000) +
         (uint64_t)now.tv_nsec / UINT64_C(1000000);
}
#endif

/**
 * Wait until @p fd is readable and/or writable or reports an error.
 * @return >0 when ready (@p out_failed tells whether the socket flagged an
 *         error), 0 on timeout, <0 on failure (see jh_native_last_error()).
 *         HAL_NET_TIMEOUT_FOREVER waits without a limit.
 */
static inline int jh_native_wait(jh_native_fd_t fd, bool read, bool write,
                                 uint32_t timeout_ms, bool *out_failed) {
  *out_failed = false;
#if defined(JH_NATIVE_SOCKETS_POSIX)
  struct pollfd entry = {};
  entry.fd = fd;
  entry.events = (short)((read ? POLLIN : 0) | (write ? POLLOUT : 0));
  const bool forever = timeout_ms == HAL_NET_TIMEOUT_FOREVER;
  const uint64_t deadline = jh_native_monotonic_ms() + timeout_ms;
  for (;;) {
    int wait_ms = -1;
    if (!forever) {
      const uint64_t now = jh_native_monotonic_ms();
      const uint64_t left = now < deadline ? deadline - now : 0u;
      wait_ms = left > (uint64_t)INT_MAX ? INT_MAX : (int)left;
    }
    const int status = poll(&entry, 1, wait_ms);
    if (status < 0 && errno == EINTR) {
      continue;
    }
    if (status > 0) {
      *out_failed = (entry.revents & (POLLERR | POLLNVAL)) != 0;
    }
    return status;
  }
#else
  fd_set read_set;
  fd_set write_set;
  fd_set error_set;
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  FD_ZERO(&error_set);
  if (read) {
    FD_SET(fd, &read_set);
  }
  if (write) {
    FD_SET(fd, &write_set);
  }
  FD_SET(fd, &error_set);
  struct timeval timeout = {};
  timeout.tv_sec = (long)(timeout_ms / 1000u);
  timeout.tv_usec = (long)((timeout_ms % 1000u) * 1000u);
  struct timeval *timeout_pointer =
      timeout_ms == HAL_NET_TIMEOUT_FOREVER ? NULL : &timeout;
#if defined(JH_NATIVE_SOCKETS_LWIP)
  const int status =
      lwip_select(fd + 1, read ? &read_set : NULL, write ? &write_set : NULL,
                  &error_set, timeout_pointer);
#else
  /* Winsock ignores the first argument; a failed connect sets error_set. */
  const int status =
      select(0, read ? &read_set : NULL, write ? &write_set : NULL, &error_set,
             timeout_pointer);
#endif
  if (status > 0) {
    *out_failed = FD_ISSET(fd, &error_set) != 0;
  }
  return status;
#endif
}

static inline int jh_native_getaddrinfo(const char *hostname,
                                        const struct addrinfo *hints,
                                        struct addrinfo **out_result) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  return lwip_getaddrinfo(hostname, NULL, hints, out_result);
#else
  return getaddrinfo(hostname, NULL, hints, out_result);
#endif
}

static inline void jh_native_freeaddrinfo(struct addrinfo *result) {
#if defined(JH_NATIVE_SOCKETS_LWIP)
  lwip_freeaddrinfo(result);
#else
  freeaddrinfo(result);
#endif
}
