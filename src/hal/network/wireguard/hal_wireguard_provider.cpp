#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_WIREGUARD

#include "hal_wireguard_internal.h"

#include "hal/core/hal_mutex_once.h"
#include "hal/network/jh_network_runtime.h"
#include "hal/network/wireguard/core/jh_wireguard_client.h"
#include "hal/system/hal_sync.h"

namespace {

JHWireGuardClient s_wireguard;
hal_mutex_t s_wireguard_mutex = nullptr;

hal_status_t ensure_mutex() {
  return jh_hal_mutex_create_once(&s_wireguard_mutex) != nullptr ? HAL_OK
                                                                 : HAL_ENOMEM;
}

} // namespace

hal_status_t jh_hal_wireguard_begin_provider(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS], const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port, const uint8_t *allowed_ip,
    const uint8_t *allowed_mask) {
  const hal_status_t mutex_status = ensure_mutex();
  if (mutex_status != HAL_OK) {
    return mutex_status;
  }
  hal_mutex_lock(s_wireguard_mutex);
  const bool ok =
      allowed_ip == nullptr
          ? s_wireguard.begin(local_ip, private_key, remote_peer_address,
                              remote_peer_public_key, remote_peer_port)
          : s_wireguard.begin_advanced(local_ip, private_key,
                                       remote_peer_address,
                                       remote_peer_public_key, remote_peer_port,
                                       allowed_ip, allowed_mask);
  hal_mutex_unlock(s_wireguard_mutex);
  return ok ? HAL_OK : HAL_EIO;
}

hal_status_t jh_hal_wireguard_end_provider(void) {
  const hal_status_t mutex_status = ensure_mutex();
  if (mutex_status != HAL_OK) {
    return mutex_status;
  }
  hal_mutex_lock(s_wireguard_mutex);
  const hal_status_t status = s_wireguard.end();
  hal_mutex_unlock(s_wireguard_mutex);
  return status;
}

hal_status_t jh_hal_wireguard_is_initialized_provider(bool *out_initialized) {
  if (out_initialized == nullptr) {
    return HAL_EINVAL;
  }
  *out_initialized = false;
  const hal_status_t mutex_status = ensure_mutex();
  if (mutex_status != HAL_OK) {
    return mutex_status;
  }
  hal_mutex_lock(s_wireguard_mutex);
  *out_initialized = s_wireguard.is_initialized();
  hal_mutex_unlock(s_wireguard_mutex);
  return HAL_OK;
}

hal_status_t jh_hal_wireguard_peer_up_provider(
    uint8_t endpoint_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t *endpoint_port,
    bool *out_peer_up) {
  if (out_peer_up == nullptr) {
    return HAL_EINVAL;
  }
  *out_peer_up = false;
  const hal_status_t mutex_status = ensure_mutex();
  if (mutex_status != HAL_OK) {
    return mutex_status;
  }
  hal_mutex_lock(s_wireguard_mutex);
  *out_peer_up = s_wireguard.peer_up(endpoint_ip, endpoint_port);
  hal_mutex_unlock(s_wireguard_mutex);
  return HAL_OK;
}

void jh_hal_wireguard_note_quick_check(void) {}

hal_status_t jh_hal_wireguard_kick_provider(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms) {
  const hal_status_t mutex_status = ensure_mutex();
  if (mutex_status != HAL_OK) {
    return mutex_status;
  }
  hal_mutex_lock(s_wireguard_mutex);
  const bool ok =
      s_wireguard.kick_handshake(probe_ip, probe_port, min_interval_ms);
  hal_mutex_unlock(s_wireguard_mutex);
  return ok ? HAL_OK : HAL_EIO;
}

#endif /* HAL_ENABLE_WIREGUARD */
#endif /* supported host-lwIP target */
