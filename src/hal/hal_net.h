#pragma once

/**
 * @file hal_net.h
 * @brief Shared network value types for HAL transports and compatibility
 *        adapters.
 *
 * This header intentionally contains only plain C data types and constants.
 * Backend-specific TCP/IP stack objects must stay in implementation files.
 */

#include <stdbool.h>
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
  HAL_NET_AF_INET = 2    /**< IPv4 endpoint. */
} hal_net_family_t;

/**
 * @brief IPv4 transport endpoint.
 *
 * The address is stored in network byte order as four octets. The port is
 * stored in host byte order; compatibility layers such as BSD sockets perform
 * their own htons()/ntohs() translation at their boundary.
 */
typedef struct {
  hal_net_family_t family;             /**< Address family. */
  uint8_t addr[HAL_NET_IPV4_ADDR_LEN]; /**< IPv4 address octets. */
  uint16_t port;                       /**< Transport port in host order. */
} hal_net_endpoint_t;

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
