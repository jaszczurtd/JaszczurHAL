#pragma once

#include "hal/core/hal_layout_check.h"

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_WIREGUARD

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_wireguard.h
 * @brief Thread-safe HAL wrapper for the shared WireGuard/lwIP engine.
 *
 * Operations that access the transport report HAL_EUNSUPPORTED, HAL_EUNINIT
 * or HAL_EHW when the RP CYW43 board hardware is respectively absent,
 * inactive or known to have failed. Pure IPv4 parsing remains available
 * without initialized network hardware.
 *
 * Starting the tunnel needs a synchronized wall clock. The tunnel accepts a
 * handshake initiation from the peer only when its timestamp is newer than
 * every initiation accepted before from the same peer key, also before
 * hal_wireguard_end(), and newer than the clock reading at
 * hal_wireguard_begin() minus 120 s. That limit does not follow the clock
 * later on, and a device reset clears the remembered initiations. An accepted
 * initiation gets its response at its source address. It neither moves the
 * peer endpoint, which follows authenticated data packets and the response to
 * the device's own handshake initiation, nor cancels a handshake the device
 * has already started. The device starts its own handshakes, so the tunnel
 * comes up even when the peer's clock lags behind. Only IPv4 packets travel
 * through the tunnel.
 *
 * hal_wireguard_config_t adds the optional preshared key of the peer and the
 * persistent keepalive interval. The begin() helpers use no preshared key and
 * a keepalive of HAL_WIREGUARD_KEEPALIVE_DEFAULT_S: the tunnel then stays up
 * behind NAT and renews its keys on its own. Every target feeds the engine's
 * key generation from jh_secure_random_bytes().
 */

#include "hal/core/hal_status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAL_WIREGUARD_IPV4_OCTETS 4u
#define HAL_WIREGUARD_IP_STR_LEN 16u
/** Persistent keepalive the begin() helpers configure, in seconds: the
 *  WireGuard recommendation for a peer behind NAT. */
#define HAL_WIREGUARD_KEEPALIVE_DEFAULT_S 25u
/** Longest persistent keepalive interval in seconds. A longer interval cannot
 *  keep a session alive: a session ends 180 s after its last traffic. */
#define HAL_WIREGUARD_KEEPALIVE_MAX_S 180u

/**
 * @brief Tunnel configuration for hal_wireguard_begin_config_ex().
 *
 * Fill it with hal_wireguard_config_init() first, then set the keys and
 * addresses. The text fields are read during the call only; the engine keeps
 * the decoded keys.
 */
typedef struct {
  /** Local tunnel IPv4 as octets. */
  uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS];
  /** Base64 private key of this device. */
  const char *private_key;
  /** Remote endpoint host name or dotted IPv4. */
  const char *remote_peer_address;
  /** Base64 public key of the peer. */
  const char *remote_peer_public_key;
  /** Remote endpoint UDP port, 1..65535. */
  uint16_t remote_peer_port;
  /** Optional base64 text of the 32-byte preshared key; NULL or empty means
   *  none. Text that does not decode to 32 bytes makes the start fail before
   *  the stack is touched. */
  const char *preshared_key;
  /** AllowedIPs network as octets. An all-zero address and mask route
   *  everything through the tunnel, which then becomes the default route. */
  uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS];
  /** Persistent keepalive interval in seconds, 0 disables it, at most
   *  HAL_WIREGUARD_KEEPALIVE_MAX_S. With a keepalive the tunnel stays up and
   *  renews its keys on its own; without one a session ends 180 s after its
   *  last traffic and the next packet starts a new handshake. */
  uint16_t keepalive_s;
} hal_wireguard_config_t;

hal_status_t
hal_wireguard_begin_config_ex(const hal_wireguard_config_t *config);
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
 * @brief Reset a configuration to its defaults.
 *
 * No keys or addresses, full tunnel, no preshared key and a keepalive of
 * HAL_WIREGUARD_KEEPALIVE_DEFAULT_S.
 *
 * @param config Configuration to reset.
 * @return HAL_OK, or HAL_EINVAL when config is NULL.
 */
hal_status_t hal_wireguard_config_init(hal_wireguard_config_t *config);

/**
 * @brief Start WireGuard from a configuration.
 *
 * The only start that sets a preshared key or the keepalive interval.
 *
 * @param config Configuration from hal_wireguard_config_init().
 * @return HAL_OK when the tunnel started. HAL_EINVAL for a NULL configuration,
 * an empty key or endpoint, port 0 or a keepalive above
 * HAL_WIREGUARD_KEEPALIVE_MAX_S; the network readiness status while the stack
 * is not ready; HAL_EIO when the engine rejects a key, also a preshared key
 * that is not 32 base64-encoded bytes, or cannot bring the interface up. The
 * stack is then left as it was.
 */
hal_status_t
hal_wireguard_begin_config_ex(const hal_wireguard_config_t *config);

/** @brief Boolean form of hal_wireguard_begin_config_ex(). */
bool hal_wireguard_begin_config(const hal_wireguard_config_t *config);

/**
 * @brief Start WireGuard in backward-compatible mode (full tunnel).
 *
 * Equivalent to driver begin(): AllowedIPs = 0.0.0.0/0, no preshared key and
 * a keepalive of HAL_WIREGUARD_KEEPALIVE_DEFAULT_S, like every begin()
 * helper below.
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
