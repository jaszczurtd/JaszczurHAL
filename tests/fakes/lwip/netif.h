#pragma once

#include "err.h"
#include "ip4_addr.h"
#include <stdbool.h>
#include <stdint.h>

struct pbuf;

#ifdef __cplusplus
extern "C" {
#endif

struct netif;
typedef struct netif netif;
typedef err_t (*netif_init_fn)(struct netif *network_interface);
typedef err_t (*netif_input_fn)(struct pbuf *packet,
                                struct netif *network_interface);

struct netif {
  void *state;
  char name[2];
  uint8_t flags;
  bool added;
  ip4_addr_t address;
  ip4_addr_t netmask;
  ip4_addr_t gateway;
};

extern struct netif *netif_default;

struct netif *netif_add(struct netif *network_interface,
                        const ip4_addr_t *address, const ip4_addr_t *netmask,
                        const ip4_addr_t *gateway, void *state,
                        netif_init_fn initialize, netif_input_fn input);
void netif_remove(struct netif *network_interface);
void netif_set_up(struct netif *network_interface);
void netif_set_default(struct netif *network_interface);
void netif_set_hostname(struct netif *network_interface, const char *hostname);

#ifdef __cplusplus
}
#endif
