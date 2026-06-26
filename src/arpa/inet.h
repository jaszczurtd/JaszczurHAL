#pragma once

/**
 * @file arpa/inet.h
 * @brief Minimal IPv4 text/binary conversion helpers for BSD sockets.
 */

#include "../hal/hal_config.h"

#ifdef HAL_ENABLE_BSD_SOCKETS

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

in_addr_t inet_addr(const char *cp);
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BSD_SOCKETS */
