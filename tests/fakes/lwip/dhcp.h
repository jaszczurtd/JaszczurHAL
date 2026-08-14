#pragma once

#include "lwip/err.h"

#include <stdint.h>

struct netif;

#ifdef __cplusplus
extern "C" {
#endif

err_t dhcp_renew(struct netif *netif);
uint8_t dhcp_supplied_address(const struct netif *netif);

#ifdef __cplusplus
}
#endif
