#pragma once

/**
 * @file hal_net.h
 * @brief Shared network value types for HAL transports and compatibility
 *        adapters.
 *
 * This header intentionally contains only plain C data types and constants.
 * Backend-specific TCP/IP stack objects must stay in implementation files.
 */

#include "hal_status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def HAL_NET_IPV4_ADDR_LEN
 * @brief Number of octets in an IPv4 address stored in HAL network structs.
 */
#ifndef HAL_NET_IPV4_ADDR_LEN
#define HAL_NET_IPV4_ADDR_LEN 4u
#endif

/** @brief Number of octets in an IPv6 address. */
#ifndef HAL_NET_IPV6_ADDR_LEN
#define HAL_NET_IPV6_ADDR_LEN 16u
#endif

/** @brief Storage capacity of the address field in a HAL endpoint. */
#ifndef HAL_NET_MAX_ADDR_LEN
#define HAL_NET_MAX_ADDR_LEN HAL_NET_IPV6_ADDR_LEN
#endif

#if HAL_NET_MAX_ADDR_LEN < HAL_NET_IPV6_ADDR_LEN
#error "HAL_NET_MAX_ADDR_LEN must hold a complete IPv6 address"
#endif

/**
 * @def HAL_NET_TIMEOUT_FOREVER
 * @brief Timeout value used by transport APIs to request blocking without a
 *        fixed deadline.
 */
#ifndef HAL_NET_TIMEOUT_FOREVER
#define HAL_NET_TIMEOUT_FOREVER UINT32_MAX
#endif

/**
 * @brief Address family used by HAL transport endpoints.
 */
typedef enum {
  HAL_NET_AF_UNSPEC = 0, /**< Unspecified or not-yet-bound endpoint. */
  HAL_NET_AF_INET = 2,   /**< IPv4 endpoint. */
  HAL_NET_AF_INET6 = 10  /**< IPv6 endpoint. */
} hal_net_family_t;

/**
 * @brief Family-tagged transport endpoint.
 *
 * IPv4 addresses use the first four bytes and set @ref addr_len to
 * HAL_NET_IPV4_ADDR_LEN. IPv6 addresses use all sixteen bytes and may carry an
 * interface scope identifier. Address bytes are always in network byte order;
 * the port is stored in host byte order.
 */
typedef struct {
  hal_net_family_t family;            /**< Address family. */
  uint8_t addr[HAL_NET_MAX_ADDR_LEN]; /**< Address bytes in network order. */
  uint8_t addr_len;                   /**< Meaningful bytes in @ref addr. */
  uint16_t port;                      /**< Transport port in host order. */
  uint32_t scope_id;                  /**< IPv6 interface scope, else zero. */
} hal_net_endpoint_t;

/** @brief Portable network address-family capability bits. */
typedef uint32_t hal_net_capabilities_t;

#define HAL_NET_CAP_IPV4 (1u << 0u)
#define HAL_NET_CAP_IPV6 (1u << 1u)
#define HAL_NET_CAP_DUAL_STACK (1u << 2u)

/**
 * @brief Transport status values that can be mapped to errno by adapters.
 */
typedef enum {
  HAL_NET_OK = 0,            /**< Operation completed successfully. */
  HAL_NET_ERR_INVALID,       /**< Invalid argument or endpoint. */
  HAL_NET_ERR_UNSUPPORTED,   /**< Operation is unsupported by this backend. */
  HAL_NET_ERR_NO_MEMORY,     /**< Static pool or backend memory exhausted. */
  HAL_NET_ERR_NOT_CONNECTED, /**< Socket is not connected or not bound. */
  HAL_NET_ERR_TIMEOUT,       /**< Operation timed out. */
  HAL_NET_ERR_WOULD_BLOCK,   /**< Non-blocking operation would block. */
  HAL_NET_ERR_BACKEND        /**< Backend-specific failure. */
} hal_net_status_t;

#ifdef HAL_ENABLE_WIFI
/**
 * @brief Return address-family capabilities of the selected runtime backend.
 *
 * On RP CYW43 backends this reports HAL_EUNSUPPORTED when the selected board
 * profile has no required radio hardware and HAL_EHW after a failed probe or
 * initialization. Declared but inactive hardware may still report its
 * backend's address-family capabilities.
 */
hal_status_t
hal_net_get_capabilities_ex(hal_net_capabilities_t *out_capabilities);

/** @brief Compatibility value-returning capability query. */
hal_net_capabilities_t hal_net_get_capabilities(void);

/**
 * @brief Allow the selected backend to make bounded forward progress.
 *
 * Poll-driven backends perform one service pass. Worker/platform-owned
 * backends return HAL_OK after draining any facade work that is safe in the
 * caller context. RP CYW43 backends report HAL_EUNSUPPORTED, HAL_EUNINIT or
 * HAL_EHW for absent, inactive or failed required hardware.
 */
hal_status_t hal_net_service(void);

/**
 * @brief Resolve a literal or hostname into bounded family-neutral results.
 *
 * @param host_or_ip Hostname or numeric address.
 * @param family_hint HAL_NET_AF_UNSPEC, HAL_NET_AF_INET or HAL_NET_AF_INET6.
 * @param results Caller-owned result array, or NULL when @p capacity is zero.
 * @param capacity Number of entries available in @p results.
 * @param out_count Actual required result count. On HAL_EOVERFLOW no result is
 *        written and this value reports the necessary capacity.
 * Numeric literals are parsed without requiring initialized radio hardware.
 * Hostname lookup requires the backend to be ready and may additionally
 * return HAL_EUNINIT or HAL_EHW.
 * @return HAL_OK, HAL_EOVERFLOW, HAL_ENOENT, HAL_EUNSUPPORTED, HAL_EUNINIT,
 *         HAL_EHW or HAL_EINVAL.
 */
hal_status_t hal_net_resolve_ex(const char *host_or_ip,
                                hal_net_family_t family_hint,
                                hal_net_endpoint_t *results, size_t capacity,
                                size_t *out_count);

hal_status_t hal_net_resolve_ipv4_ex(const char *host_or_ip,
                                     uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]);
/**
 * @brief Resolve a hostname or dotted IPv4 literal to an IPv4 address.
 *
 * The resolver intentionally returns only the address bytes. Callers that need
 * a transport endpoint keep the port in their own protocol-specific value.
 * @param host_or_ip Null-terminated hostname or dotted IPv4 literal.
 * @param out_addr Destination for four IPv4 octets in network order.
 * @return true when the address was resolved.
 */
bool hal_net_resolve_ipv4(const char *host_or_ip,
                          uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]);
#endif

#ifdef __cplusplus
}
#endif
