#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_WIREGUARD

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_wireguard.h
 * @brief Thread-safe HAL wrapper for arduino-wireguard-pico-w.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAL_WIREGUARD_IPV4_OCTETS 4u
#define HAL_WIREGUARD_IP_STR_LEN 16u

/**
 * @brief Start WireGuard in backward-compatible mode (full tunnel).
 *
 * Equivalent to driver begin(): AllowedIPs = 0.0.0.0/0.
 */
bool hal_wireguard_begin(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                         const char *private_key,
                         const char *remote_peer_address,
                         const char *remote_peer_public_key,
                         uint16_t remote_peer_port);

/**
 * @brief Start WireGuard in advanced mode with explicit AllowedIPs mask.
 */
bool hal_wireguard_begin_advanced(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  const char *private_key,
                                  const char *remote_peer_address,
                                  const char *remote_peer_public_key,
                                  uint16_t remote_peer_port,
                                  const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]);

/** @brief Stop WireGuard and restore previous netif state. */
void hal_wireguard_end(void);

/** @brief Return true when WireGuard is initialized. */
bool hal_wireguard_is_initialized(void);

/**
 * @brief Check if WireGuard peer session is up.
 *
 * @param endpoint_ip_out Optional dotted IPv4 output buffer.
 * @param endpoint_ip_out_size Size of output buffer in bytes.
 * @param endpoint_port_out Optional endpoint port output.
 */
bool hal_wireguard_peer_up(char *endpoint_ip_out,
                           size_t endpoint_ip_out_size,
                           uint16_t *endpoint_port_out);

/**
 * @brief Trigger non-blocking handshake via tiny UDP probe.
 */
bool hal_wireguard_kick_handshake(const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  uint16_t probe_port,
                                  uint32_t min_interval_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_WIREGUARD */
