#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_WIREGUARD

#include "hal/network/wireguard/hal_wireguard_internal.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr size_t kTextBufferSize = 160u;

struct MockWireGuardState {
  bool initialized;
  bool begin_result;
  bool peer_up_result;
  bool kick_result;
  bool begin_advanced_called;
  uint8_t last_local_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint8_t last_allowed_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint8_t last_allowed_mask[HAL_WIREGUARD_IPV4_OCTETS];
  char last_private_key[kTextBufferSize];
  char last_remote_peer_address[kTextBufferSize];
  char last_remote_peer_public_key[kTextBufferSize];
  char last_preshared_key[kTextBufferSize];
  uint16_t last_remote_peer_port;
  uint16_t last_keepalive_s;
  uint8_t peer_endpoint_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint16_t peer_endpoint_port;
  uint32_t peer_up_quick_call_count;
  uint8_t last_probe_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint16_t last_probe_port;
  uint32_t last_probe_min_interval_ms;
};

MockWireGuardState s_wireguard;

bool all_zero(const uint8_t octets[HAL_WIREGUARD_IPV4_OCTETS]) {
  for (size_t index = 0u; index < HAL_WIREGUARD_IPV4_OCTETS; ++index) {
    if (octets[index] != 0u) {
      return false;
    }
  }
  return true;
}

} // namespace

hal_status_t
jh_hal_wireguard_begin_provider(const hal_wireguard_config_t *config) {
  if (config == nullptr) {
    return HAL_EINVAL;
  }
  memcpy(s_wireguard.last_local_ip, config->local_ip,
         sizeof(s_wireguard.last_local_ip));
  snprintf(s_wireguard.last_private_key, sizeof(s_wireguard.last_private_key),
           "%s", config->private_key);
  snprintf(s_wireguard.last_remote_peer_address,
           sizeof(s_wireguard.last_remote_peer_address), "%s",
           config->remote_peer_address);
  snprintf(s_wireguard.last_remote_peer_public_key,
           sizeof(s_wireguard.last_remote_peer_public_key), "%s",
           config->remote_peer_public_key);
  snprintf(s_wireguard.last_preshared_key,
           sizeof(s_wireguard.last_preshared_key), "%s",
           config->preshared_key != nullptr ? config->preshared_key : "");
  s_wireguard.last_remote_peer_port = config->remote_peer_port;
  s_wireguard.last_keepalive_s = config->keepalive_s;
  memcpy(s_wireguard.last_allowed_ip, config->allowed_ip,
         sizeof(s_wireguard.last_allowed_ip));
  memcpy(s_wireguard.last_allowed_mask, config->allowed_mask,
         sizeof(s_wireguard.last_allowed_mask));
  // A split tunnel is any AllowedIPs network other than the all-zero one.
  s_wireguard.begin_advanced_called =
      s_wireguard.begin_result &&
      !(all_zero(config->allowed_ip) && all_zero(config->allowed_mask));
  s_wireguard.initialized = s_wireguard.begin_result;
  return s_wireguard.begin_result ? HAL_OK : HAL_EIO;
}

hal_status_t jh_hal_wireguard_end_provider(void) {
  s_wireguard.initialized = false;
  return HAL_OK;
}

hal_status_t jh_hal_wireguard_is_initialized_provider(bool *out_initialized) {
  if (out_initialized == nullptr) {
    return HAL_EINVAL;
  }
  *out_initialized = s_wireguard.initialized;
  return HAL_OK;
}

hal_status_t jh_hal_wireguard_peer_up_provider(
    uint8_t endpoint_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t *endpoint_port,
    bool *out_peer_up) {
  if (out_peer_up == nullptr) {
    return HAL_EINVAL;
  }
  *out_peer_up = false;
  if (!s_wireguard.peer_up_result) {
    return HAL_OK;
  }
  if (endpoint_ip != nullptr) {
    memcpy(endpoint_ip, s_wireguard.peer_endpoint_ip,
           sizeof(s_wireguard.peer_endpoint_ip));
  }
  if (endpoint_port != nullptr) {
    *endpoint_port = s_wireguard.peer_endpoint_port;
  }
  *out_peer_up = true;
  return HAL_OK;
}

void jh_hal_wireguard_note_quick_check(void) {
  ++s_wireguard.peer_up_quick_call_count;
}

hal_status_t jh_hal_wireguard_kick_provider(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms) {
  memcpy(s_wireguard.last_probe_ip, probe_ip,
         sizeof(s_wireguard.last_probe_ip));
  s_wireguard.last_probe_port = probe_port;
  s_wireguard.last_probe_min_interval_ms = min_interval_ms;
  return s_wireguard.kick_result ? HAL_OK : HAL_EIO;
}

void hal_mock_wireguard_reset(void) {
  memset(&s_wireguard, 0, sizeof(s_wireguard));
  s_wireguard.begin_result = true;
  s_wireguard.kick_result = true;
}

void hal_mock_wireguard_set_begin_result(bool result) {
  s_wireguard.begin_result = result;
}

void hal_mock_wireguard_set_peer_up_result(bool result) {
  s_wireguard.peer_up_result = result;
}

void hal_mock_wireguard_set_kick_result(bool result) {
  s_wireguard.kick_result = result;
}

void hal_mock_wireguard_set_initialized(bool initialized) {
  s_wireguard.initialized = initialized;
}

void hal_mock_wireguard_set_peer_endpoint(
    const uint8_t ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t port) {
  if (ip != nullptr) {
    memcpy(s_wireguard.peer_endpoint_ip, ip,
           sizeof(s_wireguard.peer_endpoint_ip));
    s_wireguard.peer_endpoint_port = port;
  }
}

uint32_t hal_mock_wireguard_get_peer_up_quick_call_count(void) {
  return s_wireguard.peer_up_quick_call_count;
}

const uint8_t *hal_mock_wireguard_get_last_local_ip(void) {
  return s_wireguard.last_local_ip;
}

const uint8_t *hal_mock_wireguard_get_last_allowed_ip(void) {
  return s_wireguard.last_allowed_ip;
}

const uint8_t *hal_mock_wireguard_get_last_allowed_mask(void) {
  return s_wireguard.last_allowed_mask;
}

const char *hal_mock_wireguard_get_last_remote_peer_address(void) {
  return s_wireguard.last_remote_peer_address;
}

uint16_t hal_mock_wireguard_get_last_remote_peer_port(void) {
  return s_wireguard.last_remote_peer_port;
}

const char *hal_mock_wireguard_get_last_preshared_key(void) {
  return s_wireguard.last_preshared_key;
}

uint16_t hal_mock_wireguard_get_last_keepalive_s(void) {
  return s_wireguard.last_keepalive_s;
}

bool hal_mock_wireguard_was_begin_advanced(void) {
  return s_wireguard.begin_advanced_called;
}

const uint8_t *hal_mock_wireguard_get_last_probe_ip(void) {
  return s_wireguard.last_probe_ip;
}

uint16_t hal_mock_wireguard_get_last_probe_port(void) {
  return s_wireguard.last_probe_port;
}

uint32_t hal_mock_wireguard_get_last_probe_min_interval_ms(void) {
  return s_wireguard.last_probe_min_interval_ms;
}

#endif
#endif
