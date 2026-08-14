#include "hal/core/hal_config.h"

#include "jh_cyw43_mdns.h"

#if defined(JH_CYW43_MDNS_TEST) ||                                             \
    (defined(HAL_ENABLE_OTA) && defined(HAL_CYW43_STACK_LWIP))

#include "hal/network/jh_lwip_status.h"

extern "C" {
#include "lwip/apps/mdns.h"
#include "lwip/netif.h"
}

#include <string.h>

namespace {

bool s_initialized;

} // namespace

extern "C" hal_status_t jh_cyw43_mdns_publish(struct netif *netif,
                                              const char *hostname) {
  if (netif == nullptr || hostname == nullptr || hostname[0] == '\0') {
    return HAL_EINVAL;
  }
  if (strlen(hostname) > MDNS_LABEL_MAXLEN) {
    return HAL_EOVERFLOW;
  }
  if (!s_initialized) {
    mdns_resp_init();
    s_initialized = true;
  }
  const err_t status = mdns_resp_netif_active(netif) != 0
                           ? mdns_resp_rename_netif(netif, hostname)
                           : mdns_resp_add_netif(netif, hostname);
  return jh_lwip_status_to_hal(status);
}

extern "C" hal_status_t jh_cyw43_mdns_remove(struct netif *netif) {
  if (netif == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_initialized || mdns_resp_netif_active(netif) == 0) {
    return HAL_OK;
  }
  return jh_lwip_status_to_hal(mdns_resp_remove_netif(netif));
}

#else

extern "C" hal_status_t jh_cyw43_mdns_publish(struct netif *, const char *) {
  return HAL_EUNSUPPORTED;
}

extern "C" hal_status_t jh_cyw43_mdns_remove(struct netif *) {
  return HAL_EUNSUPPORTED;
}

#endif
