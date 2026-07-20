#pragma once

#include "err.h"
#include "ip4_addr.h"

struct ip_hdr {
  ip4_addr_t src;
  ip4_addr_t dest;
};

struct netif;
struct pbuf;

#ifdef __cplusplus
extern "C" {
#endif

err_t ip_input(struct pbuf *packet, struct netif *input_netif);

#ifdef __cplusplus
}
#endif
