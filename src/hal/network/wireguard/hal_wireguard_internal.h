#ifndef JH_HAL_WIREGUARD_INTERNAL_H
#define JH_HAL_WIREGUARD_INTERNAL_H

#include "hal/core/hal_status.h"
#include "hal/network/wireguard/hal_wireguard.h"

bool jh_hal_wireguard_begin_provider(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS], const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port, const uint8_t *allowed_ip,
    const uint8_t *allowed_mask);
void jh_hal_wireguard_end_provider(void);
bool jh_hal_wireguard_is_initialized_provider(void);
bool jh_hal_wireguard_peer_up_provider(
    uint8_t endpoint_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t *endpoint_port);
void jh_hal_wireguard_note_quick_check(void);
bool jh_hal_wireguard_kick_provider(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms);

#endif
