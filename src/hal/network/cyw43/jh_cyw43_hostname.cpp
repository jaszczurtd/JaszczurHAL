#include "hal/core/hal_config.h"

#include "jh_cyw43_hostname.h"

#if defined(JH_CYW43_HOSTNAME_TEST) || defined(HAL_CYW43_STACK_LWIP)

#include "hal/network/jh_lwip_status.h"

extern "C" {
#include "lwip/dhcp.h"
#include "lwip/netif.h"
}

extern "C" hal_status_t jh_cyw43_hostname_apply(struct netif *netif,
                                                const char *hostname) {
  if (netif == nullptr || hostname == nullptr || hostname[0] == '\0') {
    return HAL_EINVAL;
  }

  netif_set_hostname(netif, hostname);
  if (dhcp_supplied_address(netif) == 0u) {
    return HAL_OK;
  }
  return jh_lwip_status_to_hal(dhcp_renew(netif));
}

#else

extern "C" hal_status_t jh_cyw43_hostname_apply(struct netif *, const char *) {
  return HAL_EUNSUPPORTED;
}

#endif
