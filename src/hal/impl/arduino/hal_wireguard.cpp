#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#ifdef HAL_ENABLE_WIREGUARD

#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_wireguard.h"
#include "../shared/hal_mutex_once.h"
#include "frameworks/arduino-wireguard-pico-w/src/arduino-wireguard-pico-w.h"

#include <IPAddress.h>
#include <stdio.h>

static WireGuard s_wireguard;
static hal_mutex_t s_wireguard_mutex = NULL;

static inline void wireguard_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_wireguard_mutex);
}

static bool validate_non_empty(const char *value, const char *fn,
                               const char *name) {
  if (!value || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", fn, name);
    return false;
  }
  return true;
}

static bool validate_ip_ptr(const uint8_t *ip, const char *fn,
                            const char *name) {
  if (!ip) {
    hal_derr("%s: %s is NULL", fn, name);
    return false;
  }
  return true;
}

static IPAddress ip_from_bytes(const uint8_t ip[HAL_WIREGUARD_IPV4_OCTETS]) {
  return IPAddress(ip[0], ip[1], ip[2], ip[3]);
}

bool hal_wireguard_parse_ipv4(const char *ip_text,
                              uint8_t out_ip[HAL_WIREGUARD_IPV4_OCTETS]) {
  if (!validate_non_empty(ip_text, "hal_wireguard_parse_ipv4", "ip_text")) {
    return false;
  }
  if (!validate_ip_ptr(out_ip, "hal_wireguard_parse_ipv4", "out_ip")) {
    return false;
  }

  const char *p = ip_text;
  for (size_t idx = 0u; idx < HAL_WIREGUARD_IPV4_OCTETS; ++idx) {
    if (*p < '0' || *p > '9') {
      hal_derr("hal_wireguard_parse_ipv4: invalid IPv4 format '%s'", ip_text);
      return false;
    }

    uint16_t octet = 0u;
    while (*p >= '0' && *p <= '9') {
      octet = (uint16_t)(octet * 10u + (uint16_t)(*p - '0'));
      if (octet > 255u) {
        hal_derr("hal_wireguard_parse_ipv4: octet out of range in '%s'",
                 ip_text);
        return false;
      }
      ++p;
    }

    out_ip[idx] = (uint8_t)octet;
    if (idx + 1u < HAL_WIREGUARD_IPV4_OCTETS) {
      if (*p != '.') {
        hal_derr("hal_wireguard_parse_ipv4: invalid IPv4 format '%s'", ip_text);
        return false;
      }
      ++p;
    }
  }

  if (*p != '\0') {
    hal_derr("hal_wireguard_parse_ipv4: invalid IPv4 suffix in '%s'", ip_text);
    return false;
  }

  return true;
}

bool hal_wireguard_begin(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                         const char *private_key,
                         const char *remote_peer_address,
                         const char *remote_peer_public_key,
                         uint16_t remote_peer_port) {
  if (!validate_ip_ptr(local_ip, "hal_wireguard_begin", "local_ip")) {
    return false;
  }
  if (!validate_non_empty(private_key, "hal_wireguard_begin", "private_key")) {
    return false;
  }
  if (!validate_non_empty(remote_peer_address, "hal_wireguard_begin",
                          "remote_peer_address")) {
    return false;
  }
  if (!validate_non_empty(remote_peer_public_key, "hal_wireguard_begin",
                          "remote_peer_public_key")) {
    return false;
  }
  if (remote_peer_port == 0u) {
    hal_derr("hal_wireguard_begin: remote_peer_port must be > 0");
    return false;
  }

  wireguard_ensure_mutex();
  hal_mutex_lock(s_wireguard_mutex);

  const bool ok = s_wireguard.begin(ip_from_bytes(local_ip), private_key,
                                    remote_peer_address, remote_peer_public_key,
                                    remote_peer_port);

  hal_mutex_unlock(s_wireguard_mutex);
  return ok;
}

bool hal_wireguard_begin_text(const char *local_ip_text,
                              const char *private_key,
                              const char *remote_peer_address,
                              const char *remote_peer_public_key,
                              uint16_t remote_peer_port) {
  uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS] = {0u};
  if (!hal_wireguard_parse_ipv4(local_ip_text, local_ip)) {
    return false;
  }

  return hal_wireguard_begin(local_ip, private_key, remote_peer_address,
                             remote_peer_public_key, remote_peer_port);
}

bool hal_wireguard_begin_advanced(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS], const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port,
    const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
    const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]) {
  if (!validate_ip_ptr(local_ip, "hal_wireguard_begin_advanced", "local_ip")) {
    return false;
  }
  if (!validate_non_empty(private_key, "hal_wireguard_begin_advanced",
                          "private_key")) {
    return false;
  }
  if (!validate_non_empty(remote_peer_address, "hal_wireguard_begin_advanced",
                          "remote_peer_address")) {
    return false;
  }
  if (!validate_non_empty(remote_peer_public_key,
                          "hal_wireguard_begin_advanced",
                          "remote_peer_public_key")) {
    return false;
  }
  if (remote_peer_port == 0u) {
    hal_derr("hal_wireguard_begin_advanced: remote_peer_port must be > 0");
    return false;
  }
  if (!validate_ip_ptr(allowed_ip, "hal_wireguard_begin_advanced",
                       "allowed_ip")) {
    return false;
  }
  if (!validate_ip_ptr(allowed_mask, "hal_wireguard_begin_advanced",
                       "allowed_mask")) {
    return false;
  }

  wireguard_ensure_mutex();
  hal_mutex_lock(s_wireguard_mutex);

  const bool ok = s_wireguard.beginAdvanced(
      ip_from_bytes(local_ip), private_key, remote_peer_address,
      remote_peer_public_key, remote_peer_port, ip_from_bytes(allowed_ip),
      ip_from_bytes(allowed_mask));

  hal_mutex_unlock(s_wireguard_mutex);
  return ok;
}

bool hal_wireguard_begin_advanced_text(const char *local_ip_text,
                                       const char *private_key,
                                       const char *remote_peer_address,
                                       const char *remote_peer_public_key,
                                       uint16_t remote_peer_port,
                                       const char *allowed_ip_text,
                                       const char *allowed_mask_text) {
  uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS] = {0u};
  uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS] = {0u};
  uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS] = {0u};

  if (!hal_wireguard_parse_ipv4(local_ip_text, local_ip)) {
    return false;
  }
  if (!hal_wireguard_parse_ipv4(allowed_ip_text, allowed_ip)) {
    return false;
  }
  if (!hal_wireguard_parse_ipv4(allowed_mask_text, allowed_mask)) {
    return false;
  }

  return hal_wireguard_begin_advanced(
      local_ip, private_key, remote_peer_address, remote_peer_public_key,
      remote_peer_port, allowed_ip, allowed_mask);
}

void hal_wireguard_end(void) {
  wireguard_ensure_mutex();
  hal_mutex_lock(s_wireguard_mutex);

  s_wireguard.end();

  hal_mutex_unlock(s_wireguard_mutex);
}

bool hal_wireguard_is_initialized(void) {
  wireguard_ensure_mutex();
  hal_mutex_lock(s_wireguard_mutex);

  const bool initialized = s_wireguard.is_initialized();

  hal_mutex_unlock(s_wireguard_mutex);
  return initialized;
}

bool hal_wireguard_peer_up(char *endpoint_ip_out, size_t endpoint_ip_out_size,
                           uint16_t *endpoint_port_out) {
  if (endpoint_ip_out != NULL && endpoint_ip_out_size == 0u) {
    hal_derr("hal_wireguard_peer_up: endpoint_ip_out_size is 0");
    return false;
  }

  IPAddress endpoint_ip(0, 0, 0, 0);
  uint16_t endpoint_port = 0u;

  wireguard_ensure_mutex();
  hal_mutex_lock(s_wireguard_mutex);

  const bool up = s_wireguard.peerUp(endpoint_ip_out ? &endpoint_ip : NULL,
                                     endpoint_port_out ? &endpoint_port : NULL);

  hal_mutex_unlock(s_wireguard_mutex);

  if (!up) {
    return false;
  }

  if (endpoint_ip_out) {
    if (snprintf(endpoint_ip_out, endpoint_ip_out_size, "%u.%u.%u.%u",
                 (unsigned)endpoint_ip[0], (unsigned)endpoint_ip[1],
                 (unsigned)endpoint_ip[2], (unsigned)endpoint_ip[3]) < 0) {
      hal_derr("hal_wireguard_peer_up: snprintf failed");
      return false;
    }
  }

  if (endpoint_port_out) {
    *endpoint_port_out = endpoint_port;
  }

  return true;
}

bool hal_wireguard_peer_up_quick(void) {
  return hal_wireguard_peer_up(NULL, 0u, NULL);
}

bool hal_wireguard_kick_handshake(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms) {
  if (!validate_ip_ptr(probe_ip, "hal_wireguard_kick_handshake", "probe_ip")) {
    return false;
  }
  if (probe_port == 0u) {
    hal_derr("hal_wireguard_kick_handshake: probe_port must be > 0");
    return false;
  }

  wireguard_ensure_mutex();
  hal_mutex_lock(s_wireguard_mutex);

  const bool ok = s_wireguard.kickHandshake(ip_from_bytes(probe_ip), probe_port,
                                            min_interval_ms);

  hal_mutex_unlock(s_wireguard_mutex);
  return ok;
}

bool hal_wireguard_kick_handshake_text(const char *probe_ip_text,
                                       uint16_t probe_port,
                                       uint32_t min_interval_ms) {
  uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS] = {0u};
  if (!hal_wireguard_parse_ipv4(probe_ip_text, probe_ip)) {
    return false;
  }

  return hal_wireguard_kick_handshake(probe_ip, probe_port, min_interval_ms);
}

#endif /* HAL_ENABLE_WIREGUARD */
#endif // HAL_TARGET_IS_RP2040
