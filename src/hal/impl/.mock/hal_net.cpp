#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_WIFI

#include "hal/network/hal_net.h"
#include "hal/network/jh_net_address_utils.h"
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
  hal_net_endpoint_t endpoint;
} hal_mock_dns_entry_t;

static hal_mock_dns_entry_t s_dns_entries[HAL_MOCK_NET_DNS_MAX_ENTRIES];
static hal_net_capabilities_t s_capabilities = HAL_NET_CAP_IPV4;

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
  s_capabilities = HAL_NET_CAP_IPV4;
}

bool hal_mock_net_set_capabilities(hal_net_capabilities_t capabilities) {
  const hal_net_capabilities_t known =
      HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6 | HAL_NET_CAP_DUAL_STACK;
  const bool has_both_families =
      (capabilities & (HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6)) ==
      (HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6);
  const bool has_dual_stack = (capabilities & HAL_NET_CAP_DUAL_STACK) != 0u;
  if ((capabilities & ~known) != 0u || has_both_families != has_dual_stack) {
    return false;
  }
  s_capabilities = capabilities;
  return true;
}

static bool parse_endpoint_literal(const char *ip,
                                   hal_net_endpoint_t *endpoint) {
  if (!ip || !endpoint) {
    return false;
  }
  memset(endpoint, 0, sizeof(*endpoint));
  if (jh_net_parse_ipv4_literal(ip, endpoint->addr)) {
    endpoint->family = HAL_NET_AF_INET;
    endpoint->addr_len = HAL_NET_IPV4_ADDR_LEN;
    return true;
  }
  if (jh_net_parse_ipv6_literal(ip, endpoint->addr, &endpoint->scope_id,
                                true)) {
    endpoint->family = HAL_NET_AF_INET6;
    endpoint->addr_len = HAL_NET_IPV6_ADDR_LEN;
    return true;
  }
  return false;
}

bool hal_mock_net_add_dns_entry(const char *host, const char *ip) {
  hal_net_endpoint_t endpoint = {};
  if (!host || host[0] == '\0' ||
      strlen(host) >= HAL_MOCK_NET_DNS_HOST_MAX_LEN ||
      !parse_endpoint_literal(ip, &endpoint)) {
    return false;
  }

  size_t slot = HAL_MOCK_NET_DNS_MAX_ENTRIES;
  for (size_t i = 0u; i < HAL_MOCK_NET_DNS_MAX_ENTRIES; ++i) {
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
  s_dns_entries[slot].endpoint = endpoint;
  s_dns_entries[slot].in_use = true;
  return true;
}

bool hal_mock_net_set_dns_entry(const char *host, const char *ip) {
  hal_net_endpoint_t endpoint = {};
  if (!host || host[0] == '\0' ||
      strlen(host) >= HAL_MOCK_NET_DNS_HOST_MAX_LEN ||
      !parse_endpoint_literal(ip, &endpoint)) {
    return false;
  }
  for (size_t i = 0u; i < HAL_MOCK_NET_DNS_MAX_ENTRIES; ++i) {
    if (s_dns_entries[i].in_use && strcmp(s_dns_entries[i].host, host) == 0) {
      memset(&s_dns_entries[i], 0, sizeof(s_dns_entries[i]));
    }
  }
  return hal_mock_net_add_dns_entry(host, ip);
}

hal_status_t
hal_net_get_capabilities_ex(hal_net_capabilities_t *out_capabilities) {
  if (!out_capabilities) {
    return HAL_EINVAL;
  }
  *out_capabilities = s_capabilities;
  return HAL_OK;
}

hal_net_capabilities_t hal_net_get_capabilities(void) { return s_capabilities; }

hal_status_t hal_net_service(void) { return HAL_OK; }

static bool family_supported(hal_net_family_t family) {
  if (family == HAL_NET_AF_INET) {
    return (s_capabilities & HAL_NET_CAP_IPV4) != 0u;
  }
  if (family == HAL_NET_AF_INET6) {
    return (s_capabilities & HAL_NET_CAP_IPV6) != 0u;
  }
  return false;
}

static bool result_matches_hint(const hal_net_endpoint_t *result,
                                hal_net_family_t family_hint) {
  return result && family_supported(result->family) &&
         (family_hint == HAL_NET_AF_UNSPEC || result->family == family_hint);
}

static hal_status_t copy_bounded_results(const hal_net_endpoint_t *candidates,
                                         size_t candidate_count,
                                         hal_net_endpoint_t *results,
                                         size_t capacity, size_t *out_count) {
  *out_count = candidate_count;
  if (candidate_count == 0u) {
    return HAL_ENOENT;
  }
  if (capacity < candidate_count) {
    return HAL_EOVERFLOW;
  }
  memcpy(results, candidates, candidate_count * sizeof(candidates[0]));
  return HAL_OK;
}

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
  if (family_hint != HAL_NET_AF_UNSPEC && !family_supported(family_hint)) {
    return HAL_EUNSUPPORTED;
  }

  hal_net_endpoint_t candidates[HAL_MOCK_NET_DNS_MAX_ENTRIES] = {};
  size_t candidate_count = 0u;
  hal_net_endpoint_t literal = {};
  if (parse_endpoint_literal(host_or_ip, &literal)) {
    if (result_matches_hint(&literal, family_hint)) {
      candidates[candidate_count++] = literal;
    }
    return copy_bounded_results(candidates, candidate_count, results, capacity,
                                out_count);
  }

  if (strcmp(host_or_ip, "localhost") == 0) {
    hal_net_endpoint_t ipv4 = {};
    ipv4.family = HAL_NET_AF_INET;
    ipv4.addr_len = HAL_NET_IPV4_ADDR_LEN;
    ipv4.addr[0] = 127u;
    ipv4.addr[3] = 1u;
    if (result_matches_hint(&ipv4, family_hint)) {
      candidates[candidate_count++] = ipv4;
    }
    hal_net_endpoint_t ipv6 = {};
    ipv6.family = HAL_NET_AF_INET6;
    ipv6.addr_len = HAL_NET_IPV6_ADDR_LEN;
    ipv6.addr[15] = 1u;
    if (result_matches_hint(&ipv6, family_hint)) {
      candidates[candidate_count++] = ipv6;
    }
    return copy_bounded_results(candidates, candidate_count, results, capacity,
                                out_count);
  }

  for (size_t i = 0u; i < HAL_MOCK_NET_DNS_MAX_ENTRIES; ++i) {
    if (s_dns_entries[i].in_use &&
        strcmp(s_dns_entries[i].host, host_or_ip) == 0 &&
        result_matches_hint(&s_dns_entries[i].endpoint, family_hint)) {
      candidates[candidate_count++] = s_dns_entries[i].endpoint;
    }
  }
  return copy_bounded_results(candidates, candidate_count, results, capacity,
                              out_count);
}

hal_status_t hal_net_resolve_ipv4_ex(const char *host_or_ip,
                                     uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (!out_addr) {
    return HAL_EINVAL;
  }
  memset(out_addr, 0, HAL_NET_IPV4_ADDR_LEN);
  hal_net_endpoint_t results[HAL_MOCK_NET_DNS_MAX_ENTRIES] = {};
  size_t count = 0u;
  const hal_status_t status =
      hal_net_resolve_ex(host_or_ip, HAL_NET_AF_INET, results,
                         HAL_MOCK_NET_DNS_MAX_ENTRIES, &count);
  if (status == HAL_OK && count > 0u) {
    memcpy(out_addr, results[0].addr, HAL_NET_IPV4_ADDR_LEN);
  }
  return status;
}

bool hal_net_resolve_ipv4(const char *host_or_ip,
                          uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  return hal_status_to_bool(hal_net_resolve_ipv4_ex(host_or_ip, out_addr));
}

#endif /* HAL_ENABLE_WIFI */
#endif /* HAL_TARGET_IS_MOCK */
