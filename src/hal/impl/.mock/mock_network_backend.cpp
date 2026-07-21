#include "../../hal_target.h"

#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_MOCK)

#include "../shared/network/jh_network_backend.h"
#include "../shared/network/jh_public_network_backend_adapter.h"

static hal_status_t service_ok(void) { return HAL_OK; }
static hal_status_t stack_enter(bool require_ipv4) {
  (void)require_ipv4;
  return HAL_OK;
}
static void stack_leave(void) {}

static const jh_network_service_ops_t s_service_ops = {
    service_ok, service_ok, service_ok, stack_enter, stack_leave,
};

extern "C" const jh_network_backend_descriptor_t *
jh_network_backend_selected(void) {
  static const jh_network_backend_descriptor_t backend = {
      JH_NETWORK_BACKEND_ABI_VERSION,
      "mock-host-stack",
      JH_NET_CAP_WIFI_STA | JH_NET_CAP_WIFI_SCAN | JH_NET_CAP_DNS |
          JH_NET_CAP_PING | JH_NET_CAP_TCP_CLIENT | JH_NET_CAP_TCP_LISTENER |
          JH_NET_CAP_UDP | JH_NET_CAP_IPV4 | JH_NET_CAP_IPV6 |
          JH_NET_CAP_HOST_STACK_L3 | JH_NET_CAP_VIRTUAL_NETIF_ROUTE |
          JH_NET_CAP_SECURE_ENTROPY | JH_NET_CAP_STACK_CONTEXT,
      JH_NETWORK_EXECUTION_POLL,
      &s_service_ops,
      jh_public_network_wifi_ops(),
      jh_public_network_resolver_ops(),
      jh_public_network_tcp_ops(),
      jh_public_network_udp_ops(),
  };
  return &backend;
}

#endif
#endif
