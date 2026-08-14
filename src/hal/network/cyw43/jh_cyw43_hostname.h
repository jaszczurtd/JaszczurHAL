#pragma once

#include "hal/core/hal_status.h"

struct netif;

#ifdef __cplusplus
extern "C" {
#endif

/** Apply a DHCP hostname while already holding the CYW43/lwIP context. */
hal_status_t jh_cyw43_hostname_apply(struct netif *netif, const char *hostname);

#ifdef __cplusplus
}
#endif
