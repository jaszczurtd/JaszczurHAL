#pragma once

#include "err.h"
#include "ip_addr.h"
#include "pbuf.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TCP_WRITE_FLAG_COPY 1u

struct tcp_pcb;
typedef err_t (*tcp_accept_fn)(void *argument, struct tcp_pcb *new_pcb,
                               err_t status);
typedef err_t (*tcp_recv_fn)(void *argument, struct tcp_pcb *pcb,
                             struct pbuf *packet, err_t status);
typedef void (*tcp_err_fn)(void *argument, err_t status);
typedef err_t (*tcp_sent_fn)(void *argument, struct tcp_pcb *pcb,
                             uint16_t length);
typedef err_t (*tcp_poll_fn)(void *argument, struct tcp_pcb *pcb);
typedef err_t (*tcp_connected_fn)(void *argument, struct tcp_pcb *pcb,
                                  err_t status);

struct tcp_pcb {
  void *callback_argument;
  tcp_recv_fn receive;
  tcp_err_fn error;
  tcp_sent_fn sent;
  tcp_poll_fn poll;
  tcp_connected_fn connected;
  tcp_accept_fn accept;
  uint16_t send_buffer;
  uint16_t local_port;
  uint16_t remote_port;
  ip_addr_t local_ip;
  ip_addr_t remote_ip;
  uint8_t backlog;
  bool listening;
  bool backlog_delayed;
  bool closed;
  bool aborted;
};

struct tcp_pcb *tcp_new_ip_type(uint8_t type);
void tcp_arg(struct tcp_pcb *pcb, void *argument);
void tcp_recv(struct tcp_pcb *pcb, tcp_recv_fn receive);
void tcp_err(struct tcp_pcb *pcb, tcp_err_fn error);
void tcp_sent(struct tcp_pcb *pcb, tcp_sent_fn sent);
void tcp_poll(struct tcp_pcb *pcb, tcp_poll_fn poll, uint8_t interval);
void tcp_accept(struct tcp_pcb *pcb, tcp_accept_fn accept);
err_t tcp_close(struct tcp_pcb *pcb);
void tcp_abort(struct tcp_pcb *pcb);
err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *local_address,
               uint16_t local_port);
struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *pcb, uint8_t backlog);
void tcp_backlog_delayed(struct tcp_pcb *pcb);
void tcp_backlog_accepted(struct tcp_pcb *pcb);
err_t tcp_connect(struct tcp_pcb *pcb, const ip_addr_t *remote_address,
                  uint16_t remote_port, tcp_connected_fn connected);
uint16_t tcp_sndbuf(const struct tcp_pcb *pcb);
err_t tcp_write(struct tcp_pcb *pcb, const void *data, uint16_t length,
                uint8_t flags);
err_t tcp_output(struct tcp_pcb *pcb);
void tcp_recved(struct tcp_pcb *pcb, uint16_t length);

#ifdef __cplusplus
}
#endif
