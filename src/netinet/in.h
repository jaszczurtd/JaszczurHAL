#pragma once

/**
 * @file netinet/in.h
 * @brief Minimal IPv4 socket address definitions for JaszczurHAL BSD sockets.
 */

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

static inline uint16_t htons(uint16_t hostshort) {
#if HAL_BSD_SOCKET_NEEDS_SWAP
  return hal_bsd_bswap16(hostshort);
#else
  return hostshort;
#endif
}

static inline uint16_t ntohs(uint16_t netshort) { return htons(netshort); }

static inline uint32_t htonl(uint32_t hostlong) {
#if HAL_BSD_SOCKET_NEEDS_SWAP
  return hal_bsd_bswap32(hostlong);
#else
  return hostlong;
#endif
}

static inline uint32_t ntohl(uint32_t netlong) { return htonl(netlong); }

#ifdef __cplusplus
}
#endif
