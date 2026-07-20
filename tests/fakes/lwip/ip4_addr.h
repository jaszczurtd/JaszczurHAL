#pragma once

#include <stdint.h>

typedef struct ip4_addr {
  uint32_t addr;
} ip4_addr_t;

#define IP4_ADDR(address, a, b, c, d)                                          \
  ((address)->addr = ((uint32_t)(a) << 24u) | ((uint32_t)(b) << 16u) |         \
                     ((uint32_t)(c) << 8u) | (uint32_t)(d))

#define ip4_addr_copy(destination, source) ((destination).addr = (source).addr)
#define ip4_addr_set_zero(address) ((address)->addr = 0u)
#define ip4_addr_isany_val(address) ((address).addr == 0u)
#define ip4_addr1(address) ((uint8_t)((address)->addr >> 24u))
#define ip4_addr2(address) ((uint8_t)((address)->addr >> 16u))
#define ip4_addr3(address) ((uint8_t)((address)->addr >> 8u))
#define ip4_addr4(address) ((uint8_t)((address)->addr))
