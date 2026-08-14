#pragma once

#include "hal/core/hal_status.h"

struct netif;

#ifdef __cplusplus
extern "C" {
#endif

/** Publish or rename a host while already holding the CYW43/lwIP context. */
hal_status_t jh_cyw43_mdns_publish(struct netif *netif, const char *hostname);

/** Remove the responder state before its netif is destroyed. */
hal_status_t jh_cyw43_mdns_remove(struct netif *netif);

#ifdef __cplusplus
}
#endif
