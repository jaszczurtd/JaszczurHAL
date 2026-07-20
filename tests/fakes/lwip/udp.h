#pragma once

#include "err.h"
#include "ip_addr.h"
#include "pbuf.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct udp_pcb;
typedef void (*udp_recv_fn)(void *argument, struct udp_pcb *pcb,
                            struct pbuf *packet,
                            const ip_addr_t *remote_address,
                            uint16_t remote_port);

struct udp_pcb {
  udp_recv_fn receive;
  void *receive_argument;
  uint16_t local_port;
  bool removed;
};

struct udp_pcb *udp_new_ip_type(uint8_t type);
err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *local_address,
               uint16_t local_port);
void udp_recv(struct udp_pcb *pcb, udp_recv_fn receive, void *argument);
err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *packet,
                 const ip_addr_t *remote_address, uint16_t remote_port);
void udp_remove(struct udp_pcb *pcb);

#ifdef __cplusplus
}
#endif
