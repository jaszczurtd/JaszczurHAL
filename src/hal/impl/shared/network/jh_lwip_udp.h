#pragma once

#include "../../../hal_status.h"
#include <lwip/ip4_addr.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAL_LWIP_UDP_RX_QUEUE_DEPTH
#define HAL_LWIP_UDP_RX_QUEUE_DEPTH 4u
#endif

#ifndef HAL_LWIP_UDP_MAX_PAYLOAD
#define HAL_LWIP_UDP_MAX_PAYLOAD 65507u
#endif

typedef struct {
  struct pbuf *packet;
  ip4_addr_t remote_address;
  uint16_t remote_port;
} jh_lwip_udp_datagram_t;

typedef struct {
  struct udp_pcb *pcb;
  bool bound;
  jh_lwip_udp_datagram_t receive_queue[HAL_LWIP_UDP_RX_QUEUE_DEPTH];
  size_t receive_head;
  size_t receive_count;
  jh_lwip_udp_datagram_t current_receive;
  uint16_t current_receive_offset;
  ip4_addr_t last_remote_address;
  uint16_t last_remote_port;
  struct pbuf *transmit_packet;
  ip4_addr_t transmit_remote_address;
  uint16_t transmit_remote_port;
  bool transmit_started;
} jh_lwip_udp_socket_t;

void jh_lwip_udp_socket_init(jh_lwip_udp_socket_t *socket);
hal_status_t jh_lwip_udp_socket_bind(jh_lwip_udp_socket_t *socket,
                                     uint16_t local_port);
hal_status_t jh_lwip_udp_socket_sendto(jh_lwip_udp_socket_t *socket,
                                       const void *data, size_t length,
                                       const ip4_addr_t *remote_address,
                                       uint16_t remote_port, size_t *out_sent);
int jh_lwip_udp_socket_parse(jh_lwip_udp_socket_t *socket);
bool jh_lwip_udp_socket_has_packet(const jh_lwip_udp_socket_t *socket);
hal_status_t jh_lwip_udp_socket_read(jh_lwip_udp_socket_t *socket, void *buffer,
                                     size_t max_length, bool discard_remainder,
                                     size_t *out_received);
bool jh_lwip_udp_socket_get_last_remote(const jh_lwip_udp_socket_t *socket,
                                        ip4_addr_t *out_address,
                                        uint16_t *out_port);
hal_status_t jh_lwip_udp_socket_begin_packet(jh_lwip_udp_socket_t *socket,
                                             const ip4_addr_t *remote_address,
                                             uint16_t remote_port);
hal_status_t jh_lwip_udp_socket_write(jh_lwip_udp_socket_t *socket,
                                      const void *data, size_t length,
                                      size_t *out_written);
hal_status_t jh_lwip_udp_socket_end_packet(jh_lwip_udp_socket_t *socket);
bool jh_lwip_udp_socket_can_send(const jh_lwip_udp_socket_t *socket);
void jh_lwip_udp_socket_close(jh_lwip_udp_socket_t *socket);

#ifdef __cplusplus
}
#endif
