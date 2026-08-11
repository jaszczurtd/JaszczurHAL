#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_CYW43)

#include "hal/network/jh_cyw43_provider.h"
#include "hal/network/jh_net_address_utils.h"
#include "hal/network/jh_network_backend.h"

#include <string.h>

#if defined(HAL_ENABLE_TCP)
extern "C" const jh_network_tcp_ops_t *jh_rp2040_cyw43_tcp_ops(void);
extern "C" hal_status_t jh_rp2040_tcp_reset_all(void);
#endif
#if defined(HAL_ENABLE_UDP)
extern "C" const jh_network_udp_ops_t *jh_rp2040_cyw43_udp_ops(void);
extern "C" hal_status_t jh_rp2040_udp_reset_all(void);
#endif

static hal_status_t service_initialize(void) {
  return jh_cyw43_provider_init();
}

static hal_status_t service_deinitialize(void) {
  return jh_cyw43_provider_deinit();
}

static hal_status_t wifi_set_mode(hal_wifi_mode_t mode) {
  if (mode == HAL_WIFI_MODE_STA) {
    return jh_cyw43_provider_init();
  }
  if (mode == HAL_WIFI_MODE_OFF) {
    return jh_cyw43_provider_leave();
  }
  return mode == HAL_WIFI_MODE_AP || mode == HAL_WIFI_MODE_AP_STA
             ? HAL_EUNSUPPORTED
             : HAL_EINVAL;
}

static hal_status_t wifi_disconnect(bool erase_credentials) {
  (void)erase_credentials;
  hal_status_t status = HAL_OK;
#if defined(HAL_ENABLE_TCP)
  status = jh_rp2040_tcp_reset_all();
#endif
#if defined(HAL_ENABLE_UDP)
  if (status == HAL_OK) {
    status = jh_rp2040_udp_reset_all();
  }
#endif
  return status == HAL_OK ? jh_cyw43_provider_leave() : status;
}

static hal_status_t wifi_join(const char *ssid, const char *password,
                              bool non_blocking, uint32_t timeout_ms) {
  hal_status_t status = HAL_OK;
#if defined(HAL_ENABLE_TCP)
  status = jh_rp2040_tcp_reset_all();
#endif
#if defined(HAL_ENABLE_UDP)
  if (status == HAL_OK) {
    status = jh_rp2040_udp_reset_all();
  }
#endif
  return status == HAL_OK
             ? jh_cyw43_provider_join(ssid, password, non_blocking, timeout_ms)
             : status;
}

static hal_status_t wifi_get_state(hal_wifi_state_t *out_state) {
  if (out_state == nullptr) {
    return HAL_EINVAL;
  }
  jh_cyw43_link_status_t link = JH_CYW43_LINK_UNKNOWN;
  const hal_status_t status = jh_cyw43_provider_link_status(&link);
  if (status != HAL_OK) {
    return status;
  }
  switch (link) {
  case JH_CYW43_LINK_DOWN:
    *out_state = HAL_WIFI_STATE_IDLE;
    break;
  case JH_CYW43_LINK_CONNECTING:
    *out_state = HAL_WIFI_STATE_CONNECTING;
    break;
  case JH_CYW43_LINK_NO_IP:
    *out_state = HAL_WIFI_STATE_CONNECTED_NO_IP;
    break;
  case JH_CYW43_LINK_JOINED:
    *out_state = HAL_WIFI_STATE_CONNECTED;
    break;
  case JH_CYW43_LINK_NO_NETWORK:
    *out_state = HAL_WIFI_STATE_NO_NETWORK;
    break;
  case JH_CYW43_LINK_BAD_AUTH:
    *out_state = HAL_WIFI_STATE_AUTH_FAILED;
    break;
  case JH_CYW43_LINK_FAILED:
  case JH_CYW43_LINK_UNKNOWN:
  default:
    *out_state = HAL_WIFI_STATE_FAILED;
    break;
  }
  return HAL_OK;
}

static hal_status_t ipv4_endpoint(
    hal_status_t (*getter)(uint8_t out_address[HAL_NET_IPV4_ADDR_LEN]),
    hal_net_endpoint_t *out_address) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  uint8_t address[HAL_NET_IPV4_ADDR_LEN] = {};
  const hal_status_t status = getter(address);
  if (status != HAL_OK) {
    return status;
  }
  memset(out_address, 0, sizeof(*out_address));
  out_address->family = HAL_NET_AF_INET;
  out_address->addr_len = HAL_NET_IPV4_ADDR_LEN;
  memcpy(out_address->addr, address, sizeof(address));
  return HAL_OK;
}

static hal_status_t wifi_get_local(hal_net_endpoint_t *out_address) {
  return ipv4_endpoint(jh_cyw43_provider_get_local_ipv4, out_address);
}

static hal_status_t wifi_get_dns(hal_net_endpoint_t *out_address) {
  return ipv4_endpoint(jh_cyw43_provider_get_dns_ipv4, out_address);
}

static hal_status_t wifi_ping(const hal_net_endpoint_t *remote,
                              uint32_t timeout_ms, int *out_result) {
  const hal_status_t shape =
      jh_net_validate_endpoint_shape(remote, false, false);
  if (shape != HAL_OK) {
    return shape;
  }
  if (remote->family != HAL_NET_AF_INET) {
    return HAL_EUNSUPPORTED;
  }
  return jh_cyw43_provider_ping_ipv4(remote->addr, timeout_ms, out_result);
}

static hal_status_t resolver_resolve(const char *hostname,
                                     hal_net_family_t family_hint,
                                     hal_net_endpoint_t *results,
                                     size_t capacity, size_t *out_count) {
  if (out_count != nullptr) {
    *out_count = 0u;
  }
  if (hostname == nullptr || hostname[0] == '\0' || out_count == nullptr ||
      (capacity > 0u && results == nullptr)) {
    return HAL_EINVAL;
  }
  if (family_hint == HAL_NET_AF_INET6) {
    return HAL_EUNSUPPORTED;
  }
  if (family_hint != HAL_NET_AF_UNSPEC && family_hint != HAL_NET_AF_INET) {
    return HAL_EINVAL;
  }
  uint8_t address[HAL_NET_IPV4_ADDR_LEN] = {};
  const hal_status_t status = jh_cyw43_provider_resolve_ipv4(hostname, address);
  if (status != HAL_OK) {
    return status;
  }
  *out_count = 1u;
  if (capacity < 1u) {
    return HAL_EOVERFLOW;
  }
  memset(&results[0], 0, sizeof(results[0]));
  results[0].family = HAL_NET_AF_INET;
  results[0].addr_len = HAL_NET_IPV4_ADDR_LEN;
  memcpy(results[0].addr, address, sizeof(address));
  return HAL_OK;
}

static const jh_network_service_ops_t s_service_ops = {
    service_initialize,
    service_deinitialize,
    jh_cyw43_provider_service,
    jh_cyw43_provider_stack_enter,
    jh_cyw43_provider_stack_leave,
};

static const jh_network_wifi_ops_t s_wifi_ops = {
    wifi_set_mode,
    wifi_disconnect,
    jh_cyw43_provider_set_hostname,
    wifi_join,
    wifi_get_state,
    wifi_get_local,
    wifi_get_dns,
    jh_cyw43_provider_get_mac,
    jh_cyw43_provider_get_rssi,
    wifi_ping,
    jh_cyw43_provider_scan,
    jh_cyw43_provider_get_scan_result,
};

static const jh_network_resolver_ops_t s_resolver_ops = {resolver_resolve};

extern "C" const jh_network_backend_descriptor_t *
jh_network_backend_selected(void) {
  static const jh_network_backend_descriptor_t backend = {
      JH_NETWORK_BACKEND_ABI_VERSION,
      "cyw43-host-lwip",
      JH_NET_CAP_WIFI_STA | JH_NET_CAP_WIFI_SCAN | JH_NET_CAP_DNS |
          JH_NET_CAP_PING | JH_NET_CAP_TCP_CLIENT | JH_NET_CAP_TCP_LISTENER |
          JH_NET_CAP_UDP | JH_NET_CAP_IPV4 | JH_NET_CAP_AUX_GPIO |
          JH_NET_CAP_HOST_STACK_L3 | JH_NET_CAP_VIRTUAL_NETIF_ROUTE |
          JH_NET_CAP_SECURE_ENTROPY | JH_NET_CAP_STACK_CONTEXT,
      JH_NETWORK_EXECUTION_POLL,
      &s_service_ops,
      &s_wifi_ops,
      &s_resolver_ops,
#if defined(HAL_ENABLE_TCP)
      jh_rp2040_cyw43_tcp_ops(),
#else
      nullptr,
#endif
#if defined(HAL_ENABLE_UDP)
      jh_rp2040_cyw43_udp_ops(),
#else
      nullptr,
#endif
  };
  return &backend;
}

#endif
#endif
