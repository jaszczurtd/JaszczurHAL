#pragma once

/**
 * @file arpa/inet.h
 * @brief IPv4/IPv6 text and binary conversion helpers for BSD sockets.
 */

#include "hal/core/hal_config.h"

#if defined(HAL_NETWORK_BACKEND_ESP_IDF) ||                                    \
    (defined(HAL_NETWORK_BACKEND_HOST) && !defined(_WIN32))

/* Keep the native address types and conversions of ESP-IDF (lwIP adapters)
 * or the host system. */
#include_next <arpa/inet.h>
#include <sys/socket.h>

#elif defined(HAL_ENABLE_BSD_SOCKETS)

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

in_addr_t inet_addr(const char *cp);
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

#ifdef __cplusplus
}
#endif

#endif /* native platform sockets / HAL_ENABLE_BSD_SOCKETS */
