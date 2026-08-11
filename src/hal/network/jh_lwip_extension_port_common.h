#ifndef JH_LWIP_EXTENSION_PORT_COMMON_H
#define JH_LWIP_EXTENSION_PORT_COMMON_H

#include "hal/network/hal_net.h"
#include "hal/system/hal_system.h"
#include "jh_lwip_extension.h"
#include "jh_lwip_status.h"

#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>

static inline hal_status_t jh_lwip_extension_resolve_ipv4(
    const char *host_or_ip, uint8_t out_address[JH_LWIP_EXTENSION_IPV4_SIZE]) {
  return hal_net_resolve_ipv4_ex(host_or_ip, out_address);
}

static inline hal_status_t
jh_lwip_extension_monotonic_ms(uint32_t *out_millis) {
  if (out_millis == nullptr) {
    return HAL_EINVAL;
  }
  *out_millis = hal_millis();
  return HAL_OK;
}

static inline hal_status_t jh_lwip_extension_send_udp_probe(
    const uint8_t address[JH_LWIP_EXTENSION_IPV4_SIZE], uint16_t port) {
  jh_lwip_extension_guard_t guard = {};
  hal_status_t status = jh_lwip_extension_guard_enter(
      jh_lwip_extension_platform_port(), true, &guard);
  if (status != HAL_OK) {
    return status;
  }
  struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
  if (pcb == nullptr) {
    jh_lwip_extension_guard_leave(&guard);
    return HAL_ENOMEM;
  }
  struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, 1u, PBUF_RAM);
  if (packet == nullptr) {
    udp_remove(pcb);
    jh_lwip_extension_guard_leave(&guard);
    return HAL_ENOMEM;
  }
  const uint8_t payload = 0u;
  const err_t copy_status = pbuf_take(packet, &payload, sizeof(payload));
  if (copy_status == ERR_OK) {
    ip_addr_t remote = {};
    IP_ADDR4(&remote, address[0], address[1], address[2], address[3]);
    status = jh_lwip_status_to_hal(udp_sendto(pcb, packet, &remote, port));
  } else {
    status = jh_lwip_status_to_hal(copy_status);
  }
  pbuf_free(packet);
  udp_remove(pcb);
  jh_lwip_extension_guard_leave(&guard);
  return status;
}

#endif
