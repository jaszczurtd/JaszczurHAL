#pragma once

#include "lwip/err.h"

#define MDNS_LABEL_MAXLEN 63

struct netif;

#ifdef __cplusplus
extern "C" {
#endif

void mdns_resp_init(void);
err_t mdns_resp_add_netif(struct netif *netif, const char *hostname);
err_t mdns_resp_remove_netif(struct netif *netif);
err_t mdns_resp_rename_netif(struct netif *netif, const char *hostname);
int mdns_resp_netif_active(struct netif *netif);

#ifdef __cplusplus
}
#endif
