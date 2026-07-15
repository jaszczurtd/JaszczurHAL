#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"

#ifdef HAL_ENABLE_WIREGUARD

#include "../../hal_serial.h"
#include "../../hal_wireguard.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

#define MOCK_WIREGUARD_TEXT_BUF 160u

static struct {
  bool initialized;
  bool begin_result;
  bool peer_up_result;
  bool kick_result;
  bool begin_advanced_called;

  uint8_t last_local_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint8_t last_allowed_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint8_t last_allowed_mask[HAL_WIREGUARD_IPV4_OCTETS];

  char last_private_key[MOCK_WIREGUARD_TEXT_BUF];
  char last_remote_peer_address[MOCK_WIREGUARD_TEXT_BUF];
  char last_remote_peer_public_key[MOCK_WIREGUARD_TEXT_BUF];
  uint16_t last_remote_peer_port;

  uint8_t peer_endpoint_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint16_t peer_endpoint_port;
  uint32_t peer_up_quick_call_count;

  uint8_t last_probe_ip[HAL_WIREGUARD_IPV4_OCTETS];
  uint16_t last_probe_port;
  uint32_t last_probe_min_interval_ms;
} s_wireguard;

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

void hal_mock_wireguard_reset(void) {
  memset(&s_wireguard, 0, sizeof(s_wireguard));
  s_wireguard.begin_result = true;
  s_wireguard.peer_up_result = false;
  s_wireguard.kick_result = true;
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

  memcpy(s_wireguard.last_local_ip, local_ip, HAL_WIREGUARD_IPV4_OCTETS);
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

  s_wireguard.initialized = s_wireguard.begin_result;
  return s_wireguard.begin_result ? HAL_OK : HAL_EIO;
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

  memcpy(s_wireguard.last_local_ip, local_ip, HAL_WIREGUARD_IPV4_OCTETS);
  snprintf(s_wireguard.last_private_key, sizeof(s_wireguard.last_private_key),
           "%s", private_key);
  snprintf(s_wireguard.last_remote_peer_address,
           sizeof(s_wireguard.last_remote_peer_address), "%s",
           remote_peer_address);
  snprintf(s_wireguard.last_remote_peer_public_key,
           sizeof(s_wireguard.last_remote_peer_public_key), "%s",
           remote_peer_public_key);
  s_wireguard.last_remote_peer_port = remote_peer_port;
  memcpy(s_wireguard.last_allowed_ip, allowed_ip, HAL_WIREGUARD_IPV4_OCTETS);
  memcpy(s_wireguard.last_allowed_mask, allowed_mask,
         HAL_WIREGUARD_IPV4_OCTETS);
  s_wireguard.initialized = s_wireguard.begin_result;
  s_wireguard.begin_advanced_called = s_wireguard.begin_result;
  return s_wireguard.begin_result ? HAL_OK : HAL_EIO;
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

void hal_wireguard_end(void) { s_wireguard.initialized = false; }

bool hal_wireguard_is_initialized(void) { return s_wireguard.initialized; }

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

  if (!s_wireguard.initialized) {
    return HAL_EUNINIT;
  }

  if (!s_wireguard.peer_up_result) {
    return HAL_OK;
  }

  if (endpoint_ip_out) {
    int written = snprintf(endpoint_ip_out, endpoint_ip_out_size, "%u.%u.%u.%u",
                           (unsigned)s_wireguard.peer_endpoint_ip[0],
                           (unsigned)s_wireguard.peer_endpoint_ip[1],
                           (unsigned)s_wireguard.peer_endpoint_ip[2],
                           (unsigned)s_wireguard.peer_endpoint_ip[3]);
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
    *endpoint_port_out = s_wireguard.peer_endpoint_port;
  }

  *out_peer_up = true;
  return HAL_OK;
}

bool hal_wireguard_peer_up(char *endpoint_ip_out, size_t endpoint_ip_out_size,
                           uint16_t *endpoint_port_out) {
  bool peer_up = false;
  hal_status_t status = hal_wireguard_peer_up_ex(
      endpoint_ip_out, endpoint_ip_out_size, endpoint_port_out, &peer_up);
  return status == HAL_OK && peer_up;
}

hal_status_t hal_wireguard_peer_up_quick_ex(bool *out_peer_up) {
  if (out_peer_up == NULL) {
    hal_derr("hal_wireguard_peer_up_quick_ex: out_peer_up is NULL");
    return HAL_EINVAL;
  }
  s_wireguard.peer_up_quick_call_count++;
  return hal_wireguard_peer_up_ex(NULL, 0u, NULL, out_peer_up);
}

bool hal_wireguard_peer_up_quick(void) {
  bool peer_up = false;
  hal_status_t status = hal_wireguard_peer_up_quick_ex(&peer_up);
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
  if (!s_wireguard.initialized) {
    return HAL_EUNINIT;
  }

  memcpy(s_wireguard.last_probe_ip, probe_ip, HAL_WIREGUARD_IPV4_OCTETS);
  s_wireguard.last_probe_port = probe_port;
  s_wireguard.last_probe_min_interval_ms = min_interval_ms;
  return s_wireguard.kick_result ? HAL_OK : HAL_EIO;
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
  if (!ip) {
    return;
  }
  memcpy(s_wireguard.peer_endpoint_ip, ip, HAL_WIREGUARD_IPV4_OCTETS);
  s_wireguard.peer_endpoint_port = port;
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

#endif /* HAL_ENABLE_WIREGUARD */
#endif // HAL_TARGET_IS_MOCK
