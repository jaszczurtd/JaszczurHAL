#ifndef JH_HAL_WIREGUARD_INTERNAL_H
#define JH_HAL_WIREGUARD_INTERNAL_H

#include "hal/core/hal_status.h"
#include "hal/network/wireguard/hal_wireguard.h"

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t
jh_hal_wireguard_begin_provider(const hal_wireguard_config_t *config);
hal_status_t jh_hal_wireguard_end_provider(void);
hal_status_t jh_hal_wireguard_is_initialized_provider(bool *out_initialized);
hal_status_t jh_hal_wireguard_peer_up_provider(
    uint8_t endpoint_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t *endpoint_port,
    bool *out_peer_up);
void jh_hal_wireguard_note_quick_check(void);
hal_status_t jh_hal_wireguard_kick_provider(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms);

#ifdef __cplusplus
}
#endif

#endif
