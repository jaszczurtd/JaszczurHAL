#pragma once

/**
 * @file sys/socket.h
 * @brief Minimal BSD/POSIX socket compatibility declarations for JaszczurHAL.
 *
 * Enable it with HAL_ENABLE_BSD_SOCKETS. IPv6 types are always declared; an
 * IPv4-only runtime rejects AF_INET6 deterministically.
 */

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BSD_SOCKETS

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#if defined(_WIN32) && !defined(HAL_BSD_SSIZE_T_DEFINED)
#include <BaseTsd.h>
#if !defined(_SSIZE_T_DEFINED)
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED 1
#endif
#define HAL_BSD_SSIZE_T_DEFINED 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef sa_family_t
typedef uint16_t sa_family_t;
#endif

#ifndef socklen_t
typedef uint32_t socklen_t;
#endif

#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif
#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 10
#endif
#ifndef PF_INET
#define PF_INET AF_INET
#endif
#ifndef PF_INET6
#define PF_INET6 AF_INET6
#endif

#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif

#ifndef SHUT_RD
#define SHUT_RD 0
#endif
#ifndef SHUT_WR
#define SHUT_WR 1
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif

#ifndef SOL_SOCKET
#define SOL_SOCKET 1
#endif
#ifndef SO_REUSEADDR
#define SO_REUSEADDR 2
#endif
#ifndef SO_ERROR
#define SO_ERROR 4
#endif
#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE 9
#endif
#ifndef SO_SNDTIMEO
#define SO_SNDTIMEO 21
#endif
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 20
#endif
#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif

struct sockaddr {
  sa_family_t sa_family;
  char sa_data[14];
};

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
int setsockopt(int sockfd, int level, int optname, const void *optval,
               socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void *optval,
               socklen_t *optlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int shutdown(int sockfd, int how);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BSD_SOCKETS */
