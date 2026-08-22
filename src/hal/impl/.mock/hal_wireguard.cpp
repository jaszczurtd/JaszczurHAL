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
  uint16_t last_remote_peer_port;
  uint8_t peer_endpoint_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint16_t peer_endpoint_port;
  uint32_t peer_up_quick_call_count;
  uint8_t last_probe_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint16_t last_probe_port;
  uint32_t last_probe_min_interval_ms;
};

MockWireGuardState s_wireguard;

} // namespace

hal_status_t jh_hal_wireguard_begin_provider(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS], const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port, const uint8_t *allowed_ip,
    const uint8_t *allowed_mask) {
  memcpy(s_wireguard.last_local_ip, local_ip,
         sizeof(s_wireguard.last_local_ip));
  snprintf(s_wireguard.last_private_key, sizeof(s_wireguard.last_private_key),
           "%s", private_key);
  snprintf(s_wireguard.last_remote_peer_address,
           sizeof(s_wireguard.last_remote_peer_address), "%s",
           remote_peer_address);
  snprintf(s_wireguard.last_remote_peer_public_key,
           sizeof(s_wireguard.last_remote_peer_public_key), "%s",
           remote_peer_public_key);
  s_wireguard.last_remote_peer_port = remote_peer_port;
  s_wireguard.begin_advanced_called = false;
  if (allowed_ip != nullptr) {
    memcpy(s_wireguard.last_allowed_ip, allowed_ip,
           sizeof(s_wireguard.last_allowed_ip));
    memcpy(s_wireguard.last_allowed_mask, allowed_mask,
           sizeof(s_wireguard.last_allowed_mask));
    s_wireguard.begin_advanced_called = s_wireguard.begin_result;
  }
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
