#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#ifdef HAL_ENABLE_WIFI

#include "../../hal_net.h"
#include "../../hal_serial.h"

#include <IPAddress.h>
#include <WiFi.h>
#include <stddef.h>
#include <stdint.h>

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

hal_status_t hal_net_resolve_ipv4_ex(const char *host_or_ip,
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
}

bool hal_net_resolve_ipv4(const char *host_or_ip,
                          uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  return hal_status_to_bool(hal_net_resolve_ipv4_ex(host_or_ip, out_addr));
}

#endif /* HAL_ENABLE_WIFI */
#endif /* HAL_TARGET_IS_RP2040 */
