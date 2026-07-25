#include "hal_config.h"

#if defined(HAL_ENABLE_WIFI) && defined(HAL_NETWORK_BACKEND_CYW43)

#include "hal_net.h"
#include "impl/shared/network/jh_net_address_utils.h"
#include "impl/shared/network/jh_network_backend.h"
#include "impl/shared/network/jh_network_runtime.h"

#include <string.h>

hal_status_t
hal_net_get_capabilities_ex(hal_net_capabilities_t *out_capabilities) {
  if (out_capabilities == nullptr) {
    return HAL_EINVAL;
  }
  *out_capabilities = 0u;
  const hal_status_t hardware_status = jh_network_require_hardware();
  if (hardware_status != HAL_OK) {
    return hardware_status;
  }
  const jh_network_capabilities_t caps =
      jh_network_backend_selected()->capabilities;
  hal_net_capabilities_t public_caps = 0u;
  if ((caps & JH_NET_CAP_IPV4) != 0u) {
    public_caps |= HAL_NET_CAP_IPV4;
  }
  if ((caps & JH_NET_CAP_IPV6) != 0u) {
    public_caps |= HAL_NET_CAP_IPV6;
  }
  if ((public_caps & (HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6)) ==
      (HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6)) {
    public_caps |= HAL_NET_CAP_DUAL_STACK;
  }
  *out_capabilities = public_caps;
  return HAL_OK;
}

hal_net_capabilities_t hal_net_get_capabilities(void) {
  hal_net_capabilities_t capabilities = 0u;
  (void)hal_net_get_capabilities_ex(&capabilities);
  return capabilities;
}

hal_status_t hal_net_service(void) {
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  const jh_network_backend_descriptor_t *backend =
      jh_network_backend_selected();
  const hal_status_t validation = jh_network_backend_validate(backend, 0u);
  if (validation != HAL_OK) {
    return validation;
  }
  return backend->service->service();
}

hal_status_t hal_net_resolve_ex(const char *host_or_ip,
                                hal_net_family_t family_hint,
                                hal_net_endpoint_t *results, size_t capacity,
                                size_t *out_count) {
  if (out_count != nullptr) {
    *out_count = 0u;
  }
  if (host_or_ip == nullptr || host_or_ip[0] == '\0' || out_count == nullptr ||
      (capacity > 0u && results == nullptr)) {
    return HAL_EINVAL;
  }
  if (family_hint != HAL_NET_AF_UNSPEC && family_hint != HAL_NET_AF_INET &&
      family_hint != HAL_NET_AF_INET6) {
    return HAL_EINVAL;
  }

  hal_net_endpoint_t literal = {};
  if (jh_net_parse_ipv4_literal(host_or_ip, literal.addr)) {
    if (family_hint == HAL_NET_AF_INET6) {
      return HAL_EUNSUPPORTED;
    }
    literal.family = HAL_NET_AF_INET;
    literal.addr_len = HAL_NET_IPV4_ADDR_LEN;
    *out_count = 1u;
    if (capacity < 1u) {
      return HAL_EOVERFLOW;
    }
    results[0] = literal;
    return HAL_OK;
  }
  if (strchr(host_or_ip, ':') != nullptr) {
    return HAL_EUNSUPPORTED;
  }

  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  const jh_network_backend_descriptor_t *backend =
      jh_network_backend_selected();
  const hal_status_t validation =
      jh_network_backend_validate(backend, JH_NET_CAP_DNS);
  if (validation != HAL_OK) {
    return validation;
  }
  return backend->resolver->resolve(host_or_ip, family_hint, results, capacity,
                                    out_count);
}

hal_status_t hal_net_resolve_ipv4_ex(const char *host_or_ip,
                                     uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]) {
  if (out_addr == nullptr) {
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

#endif
