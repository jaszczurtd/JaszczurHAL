#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_WIREGUARD

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_wireguard.h
 * @brief Thread-safe HAL wrapper for the shared WireGuard/lwIP engine.
 */

#include "hal_status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAL_WIREGUARD_IPV4_OCTETS 4u
#define HAL_WIREGUARD_IP_STR_LEN 16u

hal_status_t
hal_wireguard_begin_ex(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                       const char *private_key, const char *remote_peer_address,
                       const char *remote_peer_public_key,
                       uint16_t remote_peer_port);
hal_status_t hal_wireguard_begin_text_ex(const char *local_ip_text,
                                         const char *private_key,
                                         const char *remote_peer_address,
                                         const char *remote_peer_public_key,
                                         uint16_t remote_peer_port);
hal_status_t hal_wireguard_begin_advanced_ex(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS], const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port,
    const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
    const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]);
hal_status_t hal_wireguard_begin_advanced_text_ex(
    const char *local_ip_text, const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port, const char *allowed_ip_text,
    const char *allowed_mask_text);
hal_status_t
hal_wireguard_parse_ipv4_ex(const char *ip_text,
                            uint8_t out_ip[HAL_WIREGUARD_IPV4_OCTETS]);
hal_status_t hal_wireguard_peer_up_ex(char *endpoint_ip_out,
                                      size_t endpoint_ip_out_size,
                                      uint16_t *endpoint_port_out,
                                      bool *out_peer_up);
hal_status_t hal_wireguard_peer_up_quick_ex(bool *out_peer_up);
hal_status_t hal_wireguard_kick_handshake_ex(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms);
hal_status_t hal_wireguard_kick_handshake_text_ex(const char *probe_ip_text,
                                                  uint16_t probe_port,
                                                  uint32_t min_interval_ms);

/**
 * @brief Parse dotted IPv4 text into 4 octets.
 *
 * Strict format: `a.b.c.d`, exactly four decimal octets in range 0..255,
 * no suffix characters.
 *
 * @param ip_text Null-terminated IPv4 text.
 * @param out_ip Output buffer for 4 octets.
 * @return true on successful parse, false on validation error.
 */
bool hal_wireguard_parse_ipv4(const char *ip_text,
                              uint8_t out_ip[HAL_WIREGUARD_IPV4_OCTETS]);

/**
 * @brief Start WireGuard in backward-compatible mode (full tunnel).
 *
 * Equivalent to driver begin(): AllowedIPs = 0.0.0.0/0.
 *
 * @param local_ip Local tunnel IPv4 as octets.
 * @param private_key Base64 private key.
 * @param remote_peer_address Remote endpoint host/IP.
 * @param remote_peer_public_key Remote peer public key.
 * @param remote_peer_port Remote peer UDP port.
 * @return true when tunnel start succeeded.
 */
bool hal_wireguard_begin(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                         const char *private_key,
                         const char *remote_peer_address,
                         const char *remote_peer_public_key,
                         uint16_t remote_peer_port);

/**
 * @brief Start WireGuard in backward-compatible mode using dotted IPv4 text.
 *
 * local_ip_text must be in strict `a.b.c.d` format.
 *
 * @param local_ip_text Local tunnel IPv4 text.
 * @param private_key Base64 private key.
 * @param remote_peer_address Remote endpoint host/IP.
 * @param remote_peer_public_key Remote peer public key.
 * @param remote_peer_port Remote peer UDP port.
 * @return true when parse and tunnel start succeeded.
 */
bool hal_wireguard_begin_text(const char *local_ip_text,
                              const char *private_key,
                              const char *remote_peer_address,
                              const char *remote_peer_public_key,
                              uint16_t remote_peer_port);

/**
 * @brief Start WireGuard in advanced mode with explicit AllowedIPs mask.
 *
 * @param local_ip Local tunnel IPv4 as octets.
 * @param private_key Base64 private key.
 * @param remote_peer_address Remote endpoint host/IP.
 * @param remote_peer_public_key Remote peer public key.
 * @param remote_peer_port Remote peer UDP port.
 * @param allowed_ip Allowed network IPv4 as octets.
 * @param allowed_mask Allowed network mask as octets.
 * @return true when tunnel start succeeded.
 */
bool hal_wireguard_begin_advanced(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS], const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port,
    const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
    const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]);

/**
 * @brief Start WireGuard in advanced mode using dotted IPv4 text.
 *
 * local_ip_text, allowed_ip_text and allowed_mask_text must be in strict
 * `a.b.c.d` format.
 *
 * @param local_ip_text Local tunnel IPv4 text.
 * @param private_key Base64 private key.
 * @param remote_peer_address Remote endpoint host/IP.
 * @param remote_peer_public_key Remote peer public key.
 * @param remote_peer_port Remote peer UDP port.
 * @param allowed_ip_text Allowed network IPv4 text.
 * @param allowed_mask_text Allowed network mask text.
 * @return true when parse and tunnel start succeeded.
 */
bool hal_wireguard_begin_advanced_text(const char *local_ip_text,
                                       const char *private_key,
                                       const char *remote_peer_address,
                                       const char *remote_peer_public_key,
                                       uint16_t remote_peer_port,
                                       const char *allowed_ip_text,
                                       const char *allowed_mask_text);

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
 * @return true when peer is up (and optional outputs were written).
 */
bool hal_wireguard_peer_up(char *endpoint_ip_out, size_t endpoint_ip_out_size,
                           uint16_t *endpoint_port_out);

/**
 * @brief Check if WireGuard peer session is up (no endpoint outputs).
 *
 * Equivalent to `hal_wireguard_peer_up(NULL, 0u, NULL)`.
 */
bool hal_wireguard_peer_up_quick(void);

/**
 * @brief Trigger non-blocking handshake via tiny UDP probe.
 *
 * @param probe_ip Probe IPv4 as octets.
 * @param probe_port Probe UDP port.
 * @param min_interval_ms Minimum interval between probes.
 * @return true when probe request was accepted.
 */
bool hal_wireguard_kick_handshake(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms);

/**
 * @brief Trigger non-blocking handshake via tiny UDP probe (IPv4 text helper).
 *
 * probe_ip_text must be in strict `a.b.c.d` format.
 *
 * @param probe_ip_text Probe IPv4 text.
 * @param probe_port Probe UDP port.
 * @param min_interval_ms Minimum interval between probes.
 * @return true when parse and probe request succeeded.
 */
bool hal_wireguard_kick_handshake_text(const char *probe_ip_text,
                                       uint16_t probe_port,
                                       uint32_t min_interval_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_WIREGUARD */
