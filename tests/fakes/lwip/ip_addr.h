#pragma once

#include "ip4_addr.h"
#include <stdint.h>

#define IPADDR_TYPE_V4 0u

typedef struct ip_addr {
  uint8_t type;
  ip4_addr_t address;
} ip_addr_t;

#define IP_SET_TYPE_VAL(ip, value) ((ip).type = (value))
#define IP_IS_V4(ip) ((ip) != 0 && (ip)->type == IPADDR_TYPE_V4)
#define ip_2_ip4(ip) (&((ip)->address))
#define ip_addr_copy_from_ip4(destination, source)                             \
  do {                                                                         \
    (destination).type = IPADDR_TYPE_V4;                                       \
    ip4_addr_copy((destination).address, (source));                            \
  } while (0)
#define ip_addr_netcmp(addr_ptr, network_ptr, netmask_ptr)                     \
  ((((addr_ptr)->address.addr) & ((netmask_ptr)->addr)) ==                     \
   (((network_ptr)->address.addr) & ((netmask_ptr)->addr)))
#define IP_ADDR4(ip, a, b, c, d)                                               \
  do {                                                                         \
    (ip)->type = IPADDR_TYPE_V4;                                               \
    IP4_ADDR(&((ip)->address), (a), (b), (c), (d));                            \
  } while (0)
