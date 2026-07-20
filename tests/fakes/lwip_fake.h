#pragma once

#include "lwip/tcp.h"
#include "lwip/udp.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void lwip_fake_reset(void);
size_t lwip_fake_pbuf_count(void);

void lwip_fake_udp_fail_next_new(void);
void lwip_fake_udp_set_bind_status(err_t status);
void lwip_fake_udp_set_send_status(err_t status);
struct udp_pcb *lwip_fake_udp_last_pcb(void);
size_t lwip_fake_udp_removed_count(void);
size_t lwip_fake_udp_send_count(void);
size_t lwip_fake_udp_last_send_length(void);
const uint8_t *lwip_fake_udp_last_send_data(void);
ip4_addr_t lwip_fake_udp_last_send_address(void);
uint16_t lwip_fake_udp_last_send_port(void);
void lwip_fake_udp_receive(struct udp_pcb *pcb, const void *data, size_t length,
                           const ip4_addr_t *remote_address,
                           uint16_t remote_port);

void lwip_fake_tcp_fail_next_new(void);
void lwip_fake_tcp_set_connect_status(err_t status);
void lwip_fake_tcp_set_bind_status(err_t status);
void lwip_fake_tcp_fail_next_listen(void);
void lwip_fake_tcp_set_write_status(err_t status);
void lwip_fake_tcp_set_output_status(err_t status);
void lwip_fake_tcp_set_close_status(err_t status);
struct tcp_pcb *lwip_fake_tcp_last_pcb(void);
size_t lwip_fake_tcp_close_count(void);
size_t lwip_fake_tcp_abort_count(void);
size_t lwip_fake_tcp_output_count(void);
size_t lwip_fake_tcp_recved_count(void);
size_t lwip_fake_tcp_last_write_length(void);
const uint8_t *lwip_fake_tcp_last_write_data(void);
size_t lwip_fake_tcp_backlog_delayed_count(void);
size_t lwip_fake_tcp_backlog_accepted_count(void);
err_t lwip_fake_tcp_connected(struct tcp_pcb *pcb, err_t status);
err_t lwip_fake_tcp_incoming(struct tcp_pcb *listener,
                             const ip4_addr_t *remote_address,
                             uint16_t remote_port, struct tcp_pcb **out_client);
err_t lwip_fake_tcp_receive(struct tcp_pcb *pcb, const void *data,
                            size_t length);
err_t lwip_fake_tcp_fin(struct tcp_pcb *pcb);
void lwip_fake_tcp_error(struct tcp_pcb *pcb, err_t status);

#ifdef __cplusplus
}
#endif
