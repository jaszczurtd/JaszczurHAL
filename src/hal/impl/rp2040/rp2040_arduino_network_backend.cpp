#include "../../hal_target.h"

#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) &&                                        \
    defined(HAL_NETWORK_BACKEND_ARDUINO_PICO)

#include "../shared/network/jh_lwip_extension.h"
#include "../shared/network/jh_network_backend.h"
#include "../shared/network/jh_public_network_backend_adapter.h"

static hal_status_t service_ok(void) { return HAL_OK; }

static hal_status_t stack_enter(bool require_ipv4) {
  const jh_lwip_extension_port_t *port = jh_lwip_extension_platform_port();
  const hal_status_t validation = jh_lwip_extension_validate(port);
  return validation == HAL_OK ? port->stack_enter(port->context, require_ipv4)
                              : validation;
}

static void stack_leave(void) {
  const jh_lwip_extension_port_t *port = jh_lwip_extension_platform_port();
  if (jh_lwip_extension_validate(port) == HAL_OK) {
    port->stack_leave(port->context);
  }
}

static const jh_network_service_ops_t s_service_ops = {
    service_ok, service_ok, service_ok, stack_enter, stack_leave,
};

extern "C" const jh_network_backend_descriptor_t *
jh_network_backend_selected(void) {
  static const jh_network_backend_descriptor_t backend = {
      JH_NETWORK_BACKEND_ABI_VERSION,
      "arduino-pico-lwip",
      JH_NET_CAP_WIFI_STA | JH_NET_CAP_WIFI_AP | JH_NET_CAP_WIFI_SCAN |
          JH_NET_CAP_DNS | JH_NET_CAP_PING | JH_NET_CAP_TCP_CLIENT |
          JH_NET_CAP_TCP_LISTENER | JH_NET_CAP_UDP | JH_NET_CAP_IPV4 |
          JH_NET_CAP_HOST_STACK_L3 | JH_NET_CAP_VIRTUAL_NETIF_ROUTE |
          JH_NET_CAP_SECURE_ENTROPY | JH_NET_CAP_STACK_CONTEXT,
      JH_NETWORK_EXECUTION_PLATFORM_OWNED,
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
