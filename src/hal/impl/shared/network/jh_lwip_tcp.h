#pragma once

#include "../../../hal_status.h"
#include <lwip/ip4_addr.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAL_LWIP_TCP_RX_LIMIT
#define HAL_LWIP_TCP_RX_LIMIT (16u * 1024u)
#endif

#ifndef HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH
#ifdef HAL_TCP_LISTENER_BACKLOG_MAX
#define HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH HAL_TCP_LISTENER_BACKLOG_MAX
#else
#define HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH 5u
#endif
#endif

#if HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH < 1u
#error "HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH must be at least 1"
#endif

typedef enum {
  JH_LWIP_TCP_CLOSED = 0,
  JH_LWIP_TCP_CONNECTING,
  JH_LWIP_TCP_CONNECTED,
  JH_LWIP_TCP_REMOTE_CLOSED,
  JH_LWIP_TCP_ERROR,
} jh_lwip_tcp_state_t;

typedef struct {
  struct tcp_pcb *pcb;
  struct pbuf *receive_packet;
  size_t receive_length;
  jh_lwip_tcp_state_t state;
  err_t last_error;
} jh_lwip_tcp_socket_t;

typedef struct {
  jh_lwip_tcp_socket_t socket;
  ip4_addr_t remote_address;
  uint16_t remote_port;
} jh_lwip_tcp_pending_connection_t;

typedef struct {
  struct tcp_pcb *pcb;
  bool bound;
  bool listening;
  uint8_t backlog;
  jh_lwip_tcp_pending_connection_t pending[HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH];
  size_t pending_head;
  size_t pending_count;
} jh_lwip_tcp_listener_t;

void jh_lwip_tcp_socket_init(jh_lwip_tcp_socket_t *socket);
hal_status_t jh_lwip_tcp_socket_connect(jh_lwip_tcp_socket_t *socket,
                                        const ip4_addr_t *remote_address,
                                        uint16_t remote_port);
hal_status_t
jh_lwip_tcp_socket_connection_status(const jh_lwip_tcp_socket_t *socket);
hal_status_t jh_lwip_tcp_socket_send(jh_lwip_tcp_socket_t *socket,
                                     const void *data, size_t length,
                                     size_t *out_sent);
hal_status_t jh_lwip_tcp_socket_receive(jh_lwip_tcp_socket_t *socket,
                                        void *buffer, size_t max_length,
                                        size_t *out_received);
size_t jh_lwip_tcp_socket_available(const jh_lwip_tcp_socket_t *socket);
bool jh_lwip_tcp_socket_is_connected(const jh_lwip_tcp_socket_t *socket);
bool jh_lwip_tcp_socket_can_send(const jh_lwip_tcp_socket_t *socket);
void jh_lwip_tcp_socket_close(jh_lwip_tcp_socket_t *socket);

void jh_lwip_tcp_listener_init(jh_lwip_tcp_listener_t *listener);
hal_status_t jh_lwip_tcp_listener_bind(jh_lwip_tcp_listener_t *listener,
                                       const ip4_addr_t *local_address,
                                       uint16_t local_port);
hal_status_t jh_lwip_tcp_listener_listen(jh_lwip_tcp_listener_t *listener,
                                         uint8_t backlog);
hal_status_t jh_lwip_tcp_listener_accept(jh_lwip_tcp_listener_t *listener,
                                         jh_lwip_tcp_socket_t *out_socket,
                                         ip4_addr_t *out_remote_address,
                                         uint16_t *out_remote_port);
bool jh_lwip_tcp_listener_can_accept(const jh_lwip_tcp_listener_t *listener);
void jh_lwip_tcp_listener_close(jh_lwip_tcp_listener_t *listener);

#ifdef __cplusplus
}
#endif
