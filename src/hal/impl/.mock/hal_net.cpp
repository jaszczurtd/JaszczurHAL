#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"

#ifdef HAL_ENABLE_WIFI

#include "../../hal_net.h"
#include "hal_mock.h"

#include <string.h>

#ifndef HAL_MOCK_NET_DNS_MAX_ENTRIES
#define HAL_MOCK_NET_DNS_MAX_ENTRIES 8u
#endif

#ifndef HAL_MOCK_NET_DNS_HOST_MAX_LEN
#define HAL_MOCK_NET_DNS_HOST_MAX_LEN 64u
#endif

typedef struct {
  bool in_use;
  char host[HAL_MOCK_NET_DNS_HOST_MAX_LEN];
  uint8_t addr[HAL_NET_IPV4_ADDR_LEN];
} hal_mock_dns_entry_t;

static hal_mock_dns_entry_t s_dns_entries[HAL_MOCK_NET_DNS_MAX_ENTRIES];

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

static bool copy_host(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0u || !src || src[0] == '\0') {
    return false;
  }

  const size_t len = strlen(src);
  if (len >= dst_size) {
    return false;
  }

  memcpy(dst, src, len + 1u);
  return true;
}

void hal_mock_net_reset(void) {
  memset(s_dns_entries, 0, sizeof(s_dns_entries));
}

bool hal_mock_net_set_dns_entry(const char *host, const char *ip) {
  uint8_t addr[HAL_NET_IPV4_ADDR_LEN] = {};
  if (!host || host[0] == '\0' ||
      strlen(host) >= HAL_MOCK_NET_DNS_HOST_MAX_LEN ||
      !parse_ipv4_literal(ip, addr)) {
    return false;
  }

  size_t slot = HAL_MOCK_NET_DNS_MAX_ENTRIES;
  for (size_t i = 0u; i < HAL_MOCK_NET_DNS_MAX_ENTRIES; ++i) {
    if (s_dns_entries[i].in_use && strcmp(s_dns_entries[i].host, host) == 0) {
      slot = i;
      break;
    }
    if (!s_dns_entries[i].in_use && slot == HAL_MOCK_NET_DNS_MAX_ENTRIES) {
      slot = i;
    }
  }
  if (slot == HAL_MOCK_NET_DNS_MAX_ENTRIES) {
    return false;
  }

  if (!copy_host(s_dns_entries[slot].host, sizeof(s_dns_entries[slot].host),
                 host)) {
    return false;
  }
  memcpy(s_dns_entries[slot].addr, addr, sizeof(addr));
  s_dns_entries[slot].in_use = true;
  return true;
}

hal_status_t hal_net_resolve_ipv4_ex(const char *host_or_ip,
                                     uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (!host_or_ip || host_or_ip[0] == '\0' || !out_addr) {
    return HAL_EINVAL;
  }
  memset(out_addr, 0, HAL_NET_IPV4_ADDR_LEN);

  if (parse_ipv4_literal(host_or_ip, out_addr)) {
    return HAL_OK;
  }

  if (strcmp(host_or_ip, "localhost") == 0) {
    out_addr[0] = 127u;
    out_addr[1] = 0u;
    out_addr[2] = 0u;
    out_addr[3] = 1u;
    return HAL_OK;
  }

  for (size_t i = 0u; i < HAL_MOCK_NET_DNS_MAX_ENTRIES; ++i) {
    if (s_dns_entries[i].in_use &&
        strcmp(s_dns_entries[i].host, host_or_ip) == 0) {
      memcpy(out_addr, s_dns_entries[i].addr, HAL_NET_IPV4_ADDR_LEN);
      return HAL_OK;
    }
  }

  return HAL_ENOENT;
}

bool hal_net_resolve_ipv4(const char *host_or_ip,
                          uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  return hal_status_to_bool(hal_net_resolve_ipv4_ex(host_or_ip, out_addr));
}

#endif /* HAL_ENABLE_WIFI */
#endif /* HAL_TARGET_IS_MOCK */
