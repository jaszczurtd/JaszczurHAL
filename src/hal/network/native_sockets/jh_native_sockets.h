#pragma once

/**
 * @file jh_native_sockets.h
 * @brief Shared TCP/UDP backend operations over a native BSD socket API.
 *
 * The ESP-IDF backend (lwIP sockets) and the host backend (POSIX sockets or
 * Winsock) publish these operations in their network backend descriptor. The
 * module keeps one static pool of socket tokens and maps native errors to
 * hal_status_t; jh_native_socket_platform.h hides the platform differences.
 */

#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) &&                                        \
    (defined(HAL_NETWORK_BACKEND_ESP_IDF) ||                                   \
     defined(HAL_NETWORK_BACKEND_HOST))

#include "hal/network/jh_network_backend.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Readiness check called before a socket is created, bound or accepted.
 *
 * Defined by the backend that links the module. The ESP-IDF backend requires
 * a started station; the host backend starts Winsock on first use.
 * @return HAL_OK when sockets may be created, otherwise the reason.
 */
hal_status_t jh_native_sockets_require_ready(void);

/** @brief Create the token pool lock; HAL_ENOMEM when that fails. */
hal_status_t jh_native_sockets_initialize(void);

/**
 * @brief Invalidate every open token and shut its descriptor down.
 *
 * Handles held by callers stay allocated until closed, but every operation on
 * them fails; used when the network underlay stops.
 */
hal_status_t jh_native_sockets_shutdown_all(void);

#if defined(HAL_ENABLE_TCP)
/** @brief TCP client and listener operations of the shared socket pool. */
const jh_network_tcp_ops_t *jh_native_sockets_tcp_ops(void);
#endif

#if defined(HAL_ENABLE_UDP)
/** @brief UDP datagram operations of the shared socket pool. */
const jh_network_udp_ops_t *jh_native_sockets_udp_ops(void);
#endif

/**
 * @brief Check the arguments of a hostname lookup.
 *
 * Sets *out_count to zero when it is not NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL/empty hostname, NULL counter, NULL
 *         results with non-zero capacity or an unknown family, or
 *         HAL_EUNSUPPORTED for an IPv6 hint without IPv6 support.
 */
hal_status_t jh_native_sockets_resolve_validate(
    const char *hostname, hal_net_family_t family_hint,
    const hal_net_endpoint_t *results, size_t capacity, size_t *out_count);

/**
 * @brief Resolve @p hostname through the native getaddrinfo().
 *
 * Call after jh_native_sockets_resolve_validate() and the backend's own
 * readiness checks. Results carry port 0.
 * @param out_count Number of IPv4/IPv6 addresses found, also on HAL_EOVERFLOW,
 *        where nothing is written to @p results.
 * @return HAL_OK, HAL_EOVERFLOW, HAL_ENOENT for an unknown name, HAL_EAGAIN
 *         for a temporary resolver failure, HAL_ENOMEM, HAL_EUNSUPPORTED or
 *         HAL_EIO.
 */
hal_status_t jh_native_sockets_resolve_lookup(const char *hostname,
                                              hal_net_family_t family_hint,
                                              hal_net_endpoint_t *results,
                                              size_t capacity,
                                              size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif
