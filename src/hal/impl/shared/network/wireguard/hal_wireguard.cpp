#include "hal/hal_target.h"
#if HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474
#include "hal/hal_config.h"

#ifdef HAL_ENABLE_WIREGUARD

#include "hal/hal_serial.h"
#include "hal/hal_sync.h"
#include "hal/hal_wireguard.h"
#include "hal/impl/shared/frameworks/wireguard/jh_wireguard_client.h"
#include "hal/impl/shared/hal_mutex_once.h"
#include "hal/impl/shared/network/jh_network_runtime.h"

#include <stdio.h>

static JHWireGuardClient s_wireguard;
static hal_mutex_t s_wireguard_mutex = NULL;

static inline void wireguard_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_wireguard_mutex);
}

static hal_status_t validate_non_empty(const char *value, const char *fn,
                                       const char *name) {
  if (!value || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", fn, name);
    return HAL_EINVAL;
  }
  return HAL_OK;
}

static hal_status_t validate_ip_ptr(const uint8_t *ip, const char *fn,
                                    const char *name) {
  if (!ip) {
    hal_derr("%s: %s is NULL", fn, name);
    return HAL_EINVAL;
  }
  return HAL_OK;
}

hal_status_t
hal_wireguard_parse_ipv4_ex(const char *ip_text,
                            uint8_t out_ip[HAL_WIREGUARD_IPV4_OCTETS]) {
  hal_status_t status =
      validate_non_empty(ip_text, "hal_wireguard_parse_ipv4_ex", "ip_text");
  if (status != HAL_OK) {
    return status;
  }
  status = validate_ip_ptr(out_ip, "hal_wireguard_parse_ipv4_ex", "out_ip");
  if (status != HAL_OK) {
    return status;
  }

  const char *p = ip_text;
  for (size_t idx = 0u; idx < HAL_WIREGUARD_IPV4_OCTETS; ++idx) {
    if (*p < '0' || *p > '9') {
      hal_derr("hal_wireguard_parse_ipv4_ex: invalid IPv4 format '%s'",
               ip_text);
      return HAL_EINVAL;
    }

    uint16_t octet = 0u;
    while (*p >= '0' && *p <= '9') {
      octet = (uint16_t)(octet * 10u + (uint16_t)(*p - '0'));
      if (octet > 255u) {
        hal_derr("hal_wireguard_parse_ipv4_ex: octet out of range in '%s'",
                 ip_text);
        return HAL_EINVAL;
      }
      ++p;
    }

    out_ip[idx] = (uint8_t)octet;
    if (idx + 1u < HAL_WIREGUARD_IPV4_OCTETS) {
      if (*p != '.') {
        hal_derr("hal_wireguard_parse_ipv4_ex: invalid IPv4 format '%s'",
                 ip_text);
        return HAL_EINVAL;
      }
      ++p;
    }
  }

  if (*p != '\0') {
    hal_derr("hal_wireguard_parse_ipv4_ex: invalid IPv4 suffix in '%s'",
             ip_text);
    return HAL_EINVAL;
  }

  return HAL_OK;
}

bool hal_wireguard_parse_ipv4(const char *ip_text,
                              uint8_t out_ip[HAL_WIREGUARD_IPV4_OCTETS]) {
  return hal_status_to_bool(hal_wireguard_parse_ipv4_ex(ip_text, out_ip));
}

hal_status_t
hal_wireguard_begin_ex(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                       const char *private_key, const char *remote_peer_address,
                       const char *remote_peer_public_key,
                       uint16_t remote_peer_port) {
  hal_status_t status =
      validate_ip_ptr(local_ip, "hal_wireguard_begin_ex", "local_ip");
  if (status != HAL_OK) {
    return status;
  }
  status =
      validate_non_empty(private_key, "hal_wireguard_begin_ex", "private_key");
  if (status != HAL_OK) {
    return status;
  }
  status = validate_non_empty(remote_peer_address, "hal_wireguard_begin_ex",
                              "remote_peer_address");
  if (status != HAL_OK) {
    return status;
  }
  status = validate_non_empty(remote_peer_public_key, "hal_wireguard_begin_ex",
                              "remote_peer_public_key");
  if (status != HAL_OK) {
    return status;
  }
  if (remote_peer_port == 0u) {
    hal_derr("hal_wireguard_begin_ex: remote_peer_port must be > 0");
    return HAL_EINVAL;
  }
  status = jh_network_require_ready();
  if (status != HAL_OK) {
    return status;
  }

  wireguard_ensure_mutex();
  hal_mutex_lock(s_wireguard_mutex);

  const bool ok = s_wireguard.begin(local_ip, private_key, remote_peer_address,
                                    remote_peer_public_key, remote_peer_port);

  hal_mutex_unlock(s_wireguard_mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_wireguard_begin(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                         const char *private_key,
                         const char *remote_peer_address,
                         const char *remote_peer_public_key,
                         uint16_t remote_peer_port) {
  return hal_status_to_bool(
      hal_wireguard_begin_ex(local_ip, private_key, remote_peer_address,
                             remote_peer_public_key, remote_peer_port));
}

hal_status_t hal_wireguard_begin_text_ex(const char *local_ip_text,
                                         const char *private_key,
                                         const char *remote_peer_address,
                                         const char *remote_peer_public_key,
                                         uint16_t remote_peer_port) {
  uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS] = {0u};
  hal_status_t status = hal_wireguard_parse_ipv4_ex(local_ip_text, local_ip);
  if (status != HAL_OK) {
    return status;
  }

  return hal_wireguard_begin_ex(local_ip, private_key, remote_peer_address,
                                remote_peer_public_key, remote_peer_port);
}

bool hal_wireguard_begin_text(const char *local_ip_text,
                              const char *private_key,
                              const char *remote_peer_address,
                              const char *remote_peer_public_key,
                              uint16_t remote_peer_port) {
  return hal_status_to_bool(hal_wireguard_begin_text_ex(
      local_ip_text, private_key, remote_peer_address, remote_peer_public_key,
      remote_peer_port));
}

hal_status_t hal_wireguard_begin_advanced_ex(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS], const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port,
    const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
    const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]) {
  hal_status_t status =
      validate_ip_ptr(local_ip, "hal_wireguard_begin_advanced_ex", "local_ip");
  if (status != HAL_OK) {
    return status;
  }
  status = validate_non_empty(private_key, "hal_wireguard_begin_advanced_ex",
                              "private_key");
  if (status != HAL_OK) {
    return status;
  }
  status =
      validate_non_empty(remote_peer_address, "hal_wireguard_begin_advanced_ex",
                         "remote_peer_address");
  if (status != HAL_OK) {
    return status;
  }
  status = validate_non_empty(remote_peer_public_key,
                              "hal_wireguard_begin_advanced_ex",
                              "remote_peer_public_key");
  if (status != HAL_OK) {
    return status;
  }
  if (remote_peer_port == 0u) {
    hal_derr("hal_wireguard_begin_advanced_ex: remote_peer_port must be > 0");
    return HAL_EINVAL;
  }
  status = validate_ip_ptr(allowed_ip, "hal_wireguard_begin_advanced_ex",
                           "allowed_ip");
  if (status != HAL_OK) {
    return status;
  }
  status = validate_ip_ptr(allowed_mask, "hal_wireguard_begin_advanced_ex",
                           "allowed_mask");
  if (status != HAL_OK) {
    return status;
  }
  status = jh_network_require_ready();
  if (status != HAL_OK) {
    return status;
  }

  wireguard_ensure_mutex();
  hal_mutex_lock(s_wireguard_mutex);

  const bool ok = s_wireguard.begin_advanced(
      local_ip, private_key, remote_peer_address, remote_peer_public_key,
      remote_peer_port, allowed_ip, allowed_mask);

  hal_mutex_unlock(s_wireguard_mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_wireguard_begin_advanced(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS], const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port,
    const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
    const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]) {
  return hal_status_to_bool(hal_wireguard_begin_advanced_ex(
      local_ip, private_key, remote_peer_address, remote_peer_public_key,
      remote_peer_port, allowed_ip, allowed_mask));
}

hal_status_t hal_wireguard_begin_advanced_text_ex(
    const char *local_ip_text, const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port, const char *allowed_ip_text,
    const char *allowed_mask_text) {
  uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS] = {0u};
  uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS] = {0u};
  uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS] = {0u};

  hal_status_t status = hal_wireguard_parse_ipv4_ex(local_ip_text, local_ip);
  if (status != HAL_OK) {
    return status;
  }
  status = hal_wireguard_parse_ipv4_ex(allowed_ip_text, allowed_ip);
  if (status != HAL_OK) {
    return status;
  }
  status = hal_wireguard_parse_ipv4_ex(allowed_mask_text, allowed_mask);
  if (status != HAL_OK) {
    return status;
  }

  return hal_wireguard_begin_advanced_ex(
      local_ip, private_key, remote_peer_address, remote_peer_public_key,
      remote_peer_port, allowed_ip, allowed_mask);
}

bool hal_wireguard_begin_advanced_text(const char *local_ip_text,
                                       const char *private_key,
                                       const char *remote_peer_address,
                                       const char *remote_peer_public_key,
                                       uint16_t remote_peer_port,
                                       const char *allowed_ip_text,
                                       const char *allowed_mask_text) {
  return hal_status_to_bool(hal_wireguard_begin_advanced_text_ex(
      local_ip_text, private_key, remote_peer_address, remote_peer_public_key,
      remote_peer_port, allowed_ip_text, allowed_mask_text));
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

hal_status_t hal_wireguard_peer_up_ex(char *endpoint_ip_out,
                                      size_t endpoint_ip_out_size,
                                      uint16_t *endpoint_port_out,
                                      bool *out_peer_up) {
  if (out_peer_up == NULL) {
    hal_derr("hal_wireguard_peer_up_ex: out_peer_up is NULL");
    return HAL_EINVAL;
  }
  *out_peer_up = false;
  if (endpoint_ip_out != NULL && endpoint_ip_out_size == 0u) {
    hal_derr("hal_wireguard_peer_up_ex: endpoint_ip_out_size is 0");
    return HAL_EINVAL;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }

  uint8_t endpoint_ip[HAL_WIREGUARD_IPV4_OCTETS] = {0u};
  uint16_t endpoint_port = 0u;

  wireguard_ensure_mutex();
  hal_mutex_lock(s_wireguard_mutex);

  if (!s_wireguard.is_initialized()) {
    hal_mutex_unlock(s_wireguard_mutex);
    return HAL_EUNINIT;
  }

  const bool up =
      s_wireguard.peer_up(endpoint_ip_out ? endpoint_ip : NULL,
                          endpoint_port_out ? &endpoint_port : NULL);

  hal_mutex_unlock(s_wireguard_mutex);

  if (!up) {
    return HAL_OK;
  }

  if (endpoint_ip_out) {
    int written = snprintf(endpoint_ip_out, endpoint_ip_out_size, "%u.%u.%u.%u",
                           (unsigned)endpoint_ip[0], (unsigned)endpoint_ip[1],
                           (unsigned)endpoint_ip[2], (unsigned)endpoint_ip[3]);
    if (written < 0) {
      hal_derr("hal_wireguard_peer_up_ex: snprintf failed");
      return HAL_EIO;
    }
    if ((size_t)written >= endpoint_ip_out_size) {
      hal_derr("hal_wireguard_peer_up_ex: endpoint output buffer too small");
      return HAL_EOVERFLOW;
    }
  }

  if (endpoint_port_out) {
    *endpoint_port_out = endpoint_port;
  }

  *out_peer_up = true;
  return HAL_OK;
}

bool hal_wireguard_peer_up(char *endpoint_ip_out, size_t endpoint_ip_out_size,
                           uint16_t *endpoint_port_out) {
  bool peer_up = false;
  const hal_status_t status = hal_wireguard_peer_up_ex(
      endpoint_ip_out, endpoint_ip_out_size, endpoint_port_out, &peer_up);
  return status == HAL_OK && peer_up;
}

hal_status_t hal_wireguard_peer_up_quick_ex(bool *out_peer_up) {
  return hal_wireguard_peer_up_ex(NULL, 0u, NULL, out_peer_up);
}

bool hal_wireguard_peer_up_quick(void) {
  bool peer_up = false;
  const hal_status_t status = hal_wireguard_peer_up_quick_ex(&peer_up);
  return status == HAL_OK && peer_up;
}

hal_status_t hal_wireguard_kick_handshake_ex(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms) {
  hal_status_t status =
      validate_ip_ptr(probe_ip, "hal_wireguard_kick_handshake_ex", "probe_ip");
  if (status != HAL_OK) {
    return status;
  }
  if (probe_port == 0u) {
    hal_derr("hal_wireguard_kick_handshake_ex: probe_port must be > 0");
    return HAL_EINVAL;
  }
  status = jh_network_require_ready();
  if (status != HAL_OK) {
    return status;
  }

  wireguard_ensure_mutex();
  hal_mutex_lock(s_wireguard_mutex);

  if (!s_wireguard.is_initialized()) {
    hal_mutex_unlock(s_wireguard_mutex);
    return HAL_EUNINIT;
  }

  const bool ok =
      s_wireguard.kick_handshake(probe_ip, probe_port, min_interval_ms);

  hal_mutex_unlock(s_wireguard_mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_wireguard_kick_handshake(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms) {
  return hal_status_to_bool(
      hal_wireguard_kick_handshake_ex(probe_ip, probe_port, min_interval_ms));
}

hal_status_t hal_wireguard_kick_handshake_text_ex(const char *probe_ip_text,
                                                  uint16_t probe_port,
                                                  uint32_t min_interval_ms) {
  uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS] = {0u};
  hal_status_t status = hal_wireguard_parse_ipv4_ex(probe_ip_text, probe_ip);
  if (status != HAL_OK) {
    return status;
  }

  return hal_wireguard_kick_handshake_ex(probe_ip, probe_port, min_interval_ms);
}

bool hal_wireguard_kick_handshake_text(const char *probe_ip_text,
                                       uint16_t probe_port,
                                       uint32_t min_interval_ms) {
  return hal_status_to_bool(hal_wireguard_kick_handshake_text_ex(
      probe_ip_text, probe_port, min_interval_ms));
}

#endif /* HAL_ENABLE_WIREGUARD */
#endif // HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474
