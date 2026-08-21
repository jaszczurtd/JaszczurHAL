#pragma once

/**
 * @file netinet/in.h
 * @brief IPv4/IPv6 socket address definitions for JaszczurHAL BSD sockets.
 */

#include "hal/core/hal_config.h"

#if defined(HAL_NETWORK_BACKEND_ESP_IDF)

/* Keep ESP-IDF's native sockaddr/in_addr layout; notably, lwIP's ESP32 ABI
 * includes the sa_len/sin_len fields that the portable shim omits. */
#include_next <netinet/in.h>
#include <sys/socket.h>

#elif defined(HAL_ENABLE_BSD_SOCKETS)

#include <stdint.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

#ifndef IPPROTO_IP
#define IPPROTO_IP 0
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif
#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif

#ifndef INADDR_ANY
#define INADDR_ANY ((in_addr_t)0x00000000UL)
#endif
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK ((in_addr_t)0x7F000001UL)
#endif
#ifndef INADDR_NONE
#define INADDR_NONE ((in_addr_t)0xFFFFFFFFUL)
#endif

struct in_addr {
  in_addr_t s_addr;
};

struct sockaddr_in {
  sa_family_t sin_family;
  in_port_t sin_port;
  struct in_addr sin_addr;
  unsigned char sin_zero[8];
};

struct in6_addr {
  uint8_t s6_addr[16];
};

struct sockaddr_in6 {
  sa_family_t sin6_family;
  in_port_t sin6_port;
  uint32_t sin6_flowinfo;
  struct in6_addr sin6_addr;
  uint32_t sin6_scope_id;
};

#ifndef IN6ADDR_ANY_INIT
#define IN6ADDR_ANY_INIT                                                       \
  {                                                                            \
    { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u }         \
  }
#endif
#ifndef IN6ADDR_LOOPBACK_INIT
#define IN6ADDR_LOOPBACK_INIT                                                  \
  {                                                                            \
    { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u }         \
  }
#endif

static inline uint16_t hal_bsd_bswap16(uint16_t value) {
  return (uint16_t)((uint16_t)(value << 8u) | (uint16_t)(value >> 8u));
}

static inline uint32_t hal_bsd_bswap32(uint32_t value) {
  return ((value & 0x000000FFUL) << 24u) | ((value & 0x0000FF00UL) << 8u) |
         ((value & 0x00FF0000UL) >> 8u) | ((value & 0xFF000000UL) >> 24u);
}

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) &&                \
    (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define HAL_BSD_SOCKET_NEEDS_SWAP 0
#else
#define HAL_BSD_SOCKET_NEEDS_SWAP 1
#endif

#ifndef htons
static inline uint16_t htons(uint16_t hostshort) {
#if HAL_BSD_SOCKET_NEEDS_SWAP
  return hal_bsd_bswap16(hostshort);
#else
  return hostshort;
#endif
}
#endif

#ifndef ntohs
static inline uint16_t ntohs(uint16_t netshort) { return htons(netshort); }
#endif

#ifndef htonl
static inline uint32_t htonl(uint32_t hostlong) {
#if HAL_BSD_SOCKET_NEEDS_SWAP
  return hal_bsd_bswap32(hostlong);
#else
  return hostlong;
#endif
}
#endif

#ifndef ntohl
static inline uint32_t ntohl(uint32_t netlong) { return htonl(netlong); }
#endif

#ifdef __cplusplus
}
#endif

#endif /* HAL_NETWORK_BACKEND_ESP_IDF / HAL_ENABLE_BSD_SOCKETS */
