#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#ifdef HAL_ENABLE_WIFI
#if !defined(HAL_NETWORK_BACKEND_CYW43)

#include "../../hal_net.h"
#include "../../hal_serial.h"
#include <IPAddress.h>
#include <WiFi.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(HAL_NETWORK_BACKEND_CYW43)
static hal_mutex_t s_resolver_mutex = NULL;
static bool s_resolver_running = false;

static inline void resolver_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_resolver_mutex);
}
#endif

static bool parse_ipv4_literal(const char *src,
                               uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (!src || !out_addr) {
    return false;
  }

  const char *cursor = src;
  for (size_t octet = 0u; octet < HAL_NET_IPV4_ADDR_LEN; ++octet) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    unsigned value = 0u;
    while (*cursor >= '0' && *cursor <= '9') {
      value = (value * 10u) + (unsigned)(*cursor - '0');
      if (value > 255u) {
        return false;
      }
      cursor++;
    }

    out_addr[octet] = (uint8_t)value;
    if (octet < (HAL_NET_IPV4_ADDR_LEN - 1u)) {
      if (*cursor != '.') {
        return false;
      }
      cursor++;
    } else if (*cursor != '\0') {
      return false;
    }
  }

  return true;
}

static hal_status_t
resolve_ipv4_backend(const char *host_or_ip,
                     uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (!host_or_ip || host_or_ip[0] == '\0' || !out_addr) {
    hal_derr("hal_net_resolve_ipv4: host_or_ip/output is invalid");
    return HAL_EINVAL;
  }
  for (size_t i = 0u; i < HAL_NET_IPV4_ADDR_LEN; ++i) {
    out_addr[i] = 0u;
  }

  if (parse_ipv4_literal(host_or_ip, out_addr)) {
    return HAL_OK;
  }

#if defined(HAL_NETWORK_BACKEND_CYW43)
  resolver_ensure_mutex();
  hal_mutex_lock(s_resolver_mutex);
  if (s_resolver_running) {
    hal_mutex_unlock(s_resolver_mutex);
    return HAL_EBUSY;
  }
  s_resolver_running = true;
  hal_mutex_unlock(s_resolver_mutex);

  const hal_status_t status =
      jh_rp2040_cyw43_provider_resolve_ipv4(host_or_ip, out_addr);

  hal_mutex_lock(s_resolver_mutex);
  s_resolver_running = false;
  hal_mutex_unlock(s_resolver_mutex);
  if (status != HAL_OK) {
    hal_derr("hal_net_resolve_ipv4: DNS lookup failed for '%s' (%s)",
             host_or_ip, hal_status_to_string(status));
  }
  return status;
#else
  IPAddress resolved(0, 0, 0, 0);
  if (WiFi.hostByName(host_or_ip, resolved) != 1) {
    hal_derr("hal_net_resolve_ipv4: DNS lookup failed for '%s'", host_or_ip);
    return HAL_ENOENT;
  }

  out_addr[0] = (uint8_t)resolved[0];
  out_addr[1] = (uint8_t)resolved[1];
  out_addr[2] = (uint8_t)resolved[2];
  out_addr[3] = (uint8_t)resolved[3];
  return HAL_OK;
#endif
}

hal_status_t
hal_net_get_capabilities_ex(hal_net_capabilities_t *out_capabilities) {
  if (!out_capabilities) {
    return HAL_EINVAL;
  }
  *out_capabilities = HAL_NET_CAP_IPV4;
  return HAL_OK;
}

hal_net_capabilities_t hal_net_get_capabilities(void) {
  hal_net_capabilities_t capabilities = 0u;
  (void)hal_net_get_capabilities_ex(&capabilities);
  return capabilities;
}

hal_status_t hal_net_service(void) { return HAL_OK; }

hal_status_t hal_net_resolve_ex(const char *host_or_ip,
                                hal_net_family_t family_hint,
                                hal_net_endpoint_t *results, size_t capacity,
                                size_t *out_count) {
  if (out_count) {
    *out_count = 0u;
  }
  if (!host_or_ip || host_or_ip[0] == '\0' || !out_count ||
      (capacity > 0u && !results)) {
    return HAL_EINVAL;
  }
  if (family_hint != HAL_NET_AF_UNSPEC && family_hint != HAL_NET_AF_INET &&
      family_hint != HAL_NET_AF_INET6) {
    return HAL_EINVAL;
  }
  if (family_hint == HAL_NET_AF_INET6 || strchr(host_or_ip, ':') != NULL) {
    return HAL_EUNSUPPORTED;
  }

  uint8_t address[HAL_NET_IPV4_ADDR_LEN] = {};
  const hal_status_t status = resolve_ipv4_backend(host_or_ip, address);
  if (status != HAL_OK) {
    return status;
  }

  *out_count = 1u;
  if (capacity < 1u) {
    return HAL_EOVERFLOW;
  }

  memset(&results[0], 0, sizeof(results[0]));
  results[0].family = HAL_NET_AF_INET;
  memcpy(results[0].addr, address, HAL_NET_IPV4_ADDR_LEN);
  results[0].addr_len = HAL_NET_IPV4_ADDR_LEN;
  return HAL_OK;
}

hal_status_t hal_net_resolve_ipv4_ex(const char *host_or_ip,
                                     uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (!out_addr) {
    return HAL_EINVAL;
  }
  memset(out_addr, 0, HAL_NET_IPV4_ADDR_LEN);

  hal_net_endpoint_t result = {};
  size_t count = 0u;
  const hal_status_t status =
      hal_net_resolve_ex(host_or_ip, HAL_NET_AF_INET, &result, 1u, &count);
  if (status == HAL_OK && count == 1u) {
    memcpy(out_addr, result.addr, HAL_NET_IPV4_ADDR_LEN);
  }
  return status;
}

bool hal_net_resolve_ipv4(const char *host_or_ip,
                          uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  return hal_status_to_bool(hal_net_resolve_ipv4_ex(host_or_ip, out_addr));
}

#endif /* HAL_ENABLE_WIFI */
#endif /* !HAL_NETWORK_BACKEND_CYW43 */
#endif /* HAL_TARGET_IS_RP2040 */
