#include "../../../hal_target.h"

#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474
#include "../../../hal_config.h"
#endif

#if defined(JH_LWIP_RAW_TEST) ||                                               \
    ((HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474) &&                      \
     defined(HAL_NETWORK_BACKEND_CYW43) && defined(HAL_ENABLE_TCP))

#include "jh_lwip_status.h"
#include "jh_lwip_tcp.h"

#include <string.h>

static err_t receive_callback(void *argument, struct tcp_pcb *pcb,
                              struct pbuf *packet, err_t status) {
  jh_lwip_tcp_socket_t *socket = (jh_lwip_tcp_socket_t *)argument;
  if (socket == nullptr) {
    if (packet != nullptr) {
      pbuf_free(packet);
    }
    return ERR_ARG;
  }
  if (packet == nullptr) {
    socket->state = JH_LWIP_TCP_REMOTE_CLOSED;
    return ERR_OK;
  }
  if (status != ERR_OK) {
    pbuf_free(packet);
    socket->last_error = status;
    socket->state = JH_LWIP_TCP_ERROR;
    return status;
  }
  if (packet->tot_len > HAL_LWIP_TCP_RX_LIMIT - socket->receive_length) {
    return ERR_MEM;
  }

  const size_t packet_length = packet->tot_len;
  if (socket->receive_packet == nullptr) {
    socket->receive_packet = packet;
  } else {
    pbuf_cat(socket->receive_packet, packet);
  }
  socket->receive_length += packet_length;
  (void)pcb;
  return ERR_OK;
}

static err_t connected_callback(void *argument, struct tcp_pcb *,
                                err_t status) {
  jh_lwip_tcp_socket_t *socket = (jh_lwip_tcp_socket_t *)argument;
  if (socket == nullptr) {
    return ERR_ARG;
  }
  socket->last_error = status;
  socket->state = status == ERR_OK ? JH_LWIP_TCP_CONNECTED : JH_LWIP_TCP_ERROR;
  return status;
}

static void error_callback(void *argument, err_t status) {
  jh_lwip_tcp_socket_t *socket = (jh_lwip_tcp_socket_t *)argument;
  if (socket == nullptr) {
    return;
  }
  socket->pcb = nullptr;
  socket->last_error = status;
  socket->state = JH_LWIP_TCP_ERROR;
}

static void configure_callbacks(jh_lwip_tcp_socket_t *socket) {
  tcp_arg(socket->pcb, socket);
  tcp_recv(socket->pcb, receive_callback);
  tcp_err(socket->pcb, error_callback);
}

static void
reset_pending_connection(jh_lwip_tcp_pending_connection_t *pending) {
  jh_lwip_tcp_socket_init(&pending->socket);
  ip4_addr_set_zero(&pending->remote_address);
  pending->remote_port = 0u;
}

static void
abort_pending_connection(jh_lwip_tcp_pending_connection_t *pending) {
  if (pending->socket.pcb != nullptr) {
    tcp_backlog_accepted(pending->socket.pcb);
    tcp_arg(pending->socket.pcb, nullptr);
    tcp_recv(pending->socket.pcb, nullptr);
    tcp_sent(pending->socket.pcb, nullptr);
    tcp_poll(pending->socket.pcb, nullptr, 0u);
    tcp_err(pending->socket.pcb, nullptr);
    tcp_abort(pending->socket.pcb);
    pending->socket.pcb = nullptr;
  }
  if (pending->socket.receive_packet != nullptr) {
    pbuf_free(pending->socket.receive_packet);
  }
  reset_pending_connection(pending);
}

static err_t accept_callback(void *argument, struct tcp_pcb *new_pcb,
                             err_t status) {
  jh_lwip_tcp_listener_t *listener = (jh_lwip_tcp_listener_t *)argument;
  if (listener == nullptr || new_pcb == nullptr || status != ERR_OK ||
      !listener->listening || !IP_IS_V4(&new_pcb->remote_ip) ||
      listener->pending_count >= listener->backlog ||
      listener->pending_count >= HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH) {
    if (new_pcb != nullptr) {
      tcp_abort(new_pcb);
      return ERR_ABRT;
    }
    if (status != ERR_OK) {
      return status;
    }
    return ERR_ARG;
  }

  const size_t tail = (listener->pending_head + listener->pending_count) %
                      HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH;
  jh_lwip_tcp_pending_connection_t *pending = &listener->pending[tail];
  reset_pending_connection(pending);
  pending->socket.pcb = new_pcb;
  pending->socket.state = JH_LWIP_TCP_CONNECTED;
  pending->socket.last_error = ERR_OK;
  ip4_addr_copy(pending->remote_address, *ip_2_ip4(&new_pcb->remote_ip));
  pending->remote_port = new_pcb->remote_port;
  configure_callbacks(&pending->socket);
  tcp_backlog_delayed(new_pcb);
  ++listener->pending_count;
  return ERR_OK;
}

void jh_lwip_tcp_socket_init(jh_lwip_tcp_socket_t *socket) {
  if (socket != nullptr) {
    memset(socket, 0, sizeof(*socket));
    socket->state = JH_LWIP_TCP_CLOSED;
    socket->last_error = ERR_OK;
  }
}

void jh_lwip_tcp_socket_close(jh_lwip_tcp_socket_t *socket) {
  if (socket == nullptr) {
    return;
  }
  if (socket->pcb != nullptr) {
    tcp_arg(socket->pcb, nullptr);
    tcp_recv(socket->pcb, nullptr);
    tcp_sent(socket->pcb, nullptr);
    tcp_poll(socket->pcb, nullptr, 0u);
    tcp_err(socket->pcb, nullptr);
    if (tcp_close(socket->pcb) != ERR_OK) {
      tcp_abort(socket->pcb);
    }
  }
  if (socket->receive_packet != nullptr) {
    pbuf_free(socket->receive_packet);
  }
  jh_lwip_tcp_socket_init(socket);
}

hal_status_t jh_lwip_tcp_socket_connect(jh_lwip_tcp_socket_t *socket,
                                        const ip4_addr_t *remote_address,
                                        uint16_t remote_port) {
  if (socket == nullptr || remote_address == nullptr || remote_port == 0u) {
    return HAL_EINVAL;
  }

  jh_lwip_tcp_socket_close(socket);
  socket->pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (socket->pcb == nullptr) {
    return HAL_ENOMEM;
  }
  configure_callbacks(socket);

  ip_addr_t destination;
  IP_SET_TYPE_VAL(destination, IPADDR_TYPE_V4);
  ip4_addr_copy(*ip_2_ip4(&destination), *remote_address);
  socket->state = JH_LWIP_TCP_CONNECTING;
  const err_t status =
      tcp_connect(socket->pcb, &destination, remote_port, connected_callback);
  if (status != ERR_OK) {
    const hal_status_t hal_status = jh_lwip_status_to_hal(status);
    jh_lwip_tcp_socket_close(socket);
    return hal_status;
  }
  return HAL_OK;
}

hal_status_t
jh_lwip_tcp_socket_connection_status(const jh_lwip_tcp_socket_t *socket) {
  if (socket == nullptr) {
    return HAL_EINVAL;
  }
  switch (socket->state) {
  case JH_LWIP_TCP_CONNECTED:
    return HAL_OK;
  case JH_LWIP_TCP_CONNECTING:
    return HAL_EAGAIN;
  case JH_LWIP_TCP_ERROR:
    return jh_lwip_status_to_hal(socket->last_error);
  case JH_LWIP_TCP_REMOTE_CLOSED:
  case JH_LWIP_TCP_CLOSED:
  default:
    return HAL_ESTATE;
  }
}

hal_status_t jh_lwip_tcp_socket_send(jh_lwip_tcp_socket_t *socket,
                                     const void *data, size_t length,
                                     size_t *out_sent) {
  if (out_sent != nullptr) {
    *out_sent = 0u;
  }
  if (socket == nullptr || out_sent == nullptr ||
      (length > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  if (!jh_lwip_tcp_socket_can_send(socket)) {
    return HAL_ESTATE;
  }
  if (length == 0u) {
    return HAL_OK;
  }

  size_t write_length = length;
  const size_t send_capacity = tcp_sndbuf(socket->pcb);
  if (write_length > send_capacity) {
    write_length = send_capacity;
  }
  if (write_length > UINT16_MAX) {
    write_length = UINT16_MAX;
  }
  if (write_length == 0u) {
    return HAL_EAGAIN;
  }

  const err_t write_status =
      tcp_write(socket->pcb, data, (uint16_t)write_length, TCP_WRITE_FLAG_COPY);
  if (write_status != ERR_OK) {
    return jh_lwip_status_to_hal(write_status);
  }
  *out_sent = write_length;
  const err_t output_status = tcp_output(socket->pcb);
  return output_status == ERR_OK ? HAL_OK
                                 : jh_lwip_status_to_hal(output_status);
}

hal_status_t jh_lwip_tcp_socket_receive(jh_lwip_tcp_socket_t *socket,
                                        void *buffer, size_t max_length,
                                        size_t *out_received) {
  if (out_received != nullptr) {
    *out_received = 0u;
  }
  if (socket == nullptr || out_received == nullptr ||
      (max_length > 0u && buffer == nullptr)) {
    return HAL_EINVAL;
  }
  if (max_length == 0u || socket->receive_length == 0u) {
    return HAL_OK;
  }

  size_t read_length = max_length;
  if (read_length > socket->receive_length) {
    read_length = socket->receive_length;
  }
  if (read_length > UINT16_MAX) {
    read_length = UINT16_MAX;
  }
  const uint16_t copied = pbuf_copy_partial(socket->receive_packet, buffer,
                                            (uint16_t)read_length, 0u);
  if (copied != read_length) {
    return HAL_EIO;
  }

  socket->receive_packet =
      pbuf_free_header(socket->receive_packet, (uint16_t)read_length);
  socket->receive_length -= read_length;
  if (socket->pcb != nullptr) {
    tcp_recved(socket->pcb, (uint16_t)read_length);
  }
  *out_received = read_length;
  return HAL_OK;
}

size_t jh_lwip_tcp_socket_available(const jh_lwip_tcp_socket_t *socket) {
  return socket == nullptr ? 0u : socket->receive_length;
}

bool jh_lwip_tcp_socket_is_connected(const jh_lwip_tcp_socket_t *socket) {
  return socket != nullptr && (socket->state == JH_LWIP_TCP_CONNECTED ||
                               socket->receive_length > 0u);
}

bool jh_lwip_tcp_socket_can_send(const jh_lwip_tcp_socket_t *socket) {
  return socket != nullptr && socket->pcb != nullptr &&
         socket->state == JH_LWIP_TCP_CONNECTED && tcp_sndbuf(socket->pcb) > 0u;
}

void jh_lwip_tcp_listener_init(jh_lwip_tcp_listener_t *listener) {
  if (listener == nullptr) {
    return;
  }
  memset(listener, 0, sizeof(*listener));
  for (size_t index = 0u; index < HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH; ++index) {
    reset_pending_connection(&listener->pending[index]);
  }
}

void jh_lwip_tcp_listener_close(jh_lwip_tcp_listener_t *listener) {
  if (listener == nullptr) {
    return;
  }

  for (size_t index = 0u; index < HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH; ++index) {
    abort_pending_connection(&listener->pending[index]);
  }
  if (listener->pcb != nullptr) {
    tcp_arg(listener->pcb, nullptr);
    if (listener->listening) {
      tcp_accept(listener->pcb, nullptr);
    }
    if (tcp_close(listener->pcb) != ERR_OK) {
      tcp_abort(listener->pcb);
    }
  }
  jh_lwip_tcp_listener_init(listener);
}

hal_status_t jh_lwip_tcp_listener_bind(jh_lwip_tcp_listener_t *listener,
                                       const ip4_addr_t *local_address,
                                       uint16_t local_port) {
  if (listener == nullptr || local_address == nullptr || local_port == 0u) {
    return HAL_EINVAL;
  }

  jh_lwip_tcp_listener_close(listener);
  listener->pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (listener->pcb == nullptr) {
    return HAL_ENOMEM;
  }

  ip_addr_t address;
  IP_SET_TYPE_VAL(address, IPADDR_TYPE_V4);
  ip4_addr_copy(*ip_2_ip4(&address), *local_address);
  const err_t status = tcp_bind(listener->pcb, &address, local_port);
  if (status != ERR_OK) {
    const hal_status_t hal_status = jh_lwip_status_to_hal(status);
    jh_lwip_tcp_listener_close(listener);
    return hal_status;
  }
  listener->bound = true;
  return HAL_OK;
}

hal_status_t jh_lwip_tcp_listener_listen(jh_lwip_tcp_listener_t *listener,
                                         uint8_t backlog) {
  if (listener == nullptr || backlog == 0u) {
    return HAL_EINVAL;
  }
  if (!listener->bound || listener->pcb == nullptr || listener->listening) {
    return HAL_ESTATE;
  }

  if ((size_t)backlog > HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH) {
    backlog = (uint8_t)HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH;
  }
  struct tcp_pcb *listen_pcb = tcp_listen_with_backlog(listener->pcb, backlog);
  if (listen_pcb == nullptr) {
    return HAL_ENOMEM;
  }

  listener->pcb = listen_pcb;
  listener->backlog = backlog;
  listener->listening = true;
  tcp_arg(listener->pcb, listener);
  tcp_accept(listener->pcb, accept_callback);
  return HAL_OK;
}

hal_status_t jh_lwip_tcp_listener_accept(jh_lwip_tcp_listener_t *listener,
                                         jh_lwip_tcp_socket_t *out_socket,
                                         ip4_addr_t *out_remote_address,
                                         uint16_t *out_remote_port) {
  if (listener == nullptr || out_socket == nullptr ||
      out_remote_address == nullptr || out_remote_port == nullptr) {
    return HAL_EINVAL;
  }
  if (!listener->listening || listener->pcb == nullptr) {
    return HAL_ESTATE;
  }

  while (listener->pending_count > 0u) {
    jh_lwip_tcp_pending_connection_t *pending =
        &listener->pending[listener->pending_head];
    listener->pending_head =
        (listener->pending_head + 1u) % HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH;
    --listener->pending_count;

    if (pending->socket.pcb == nullptr) {
      abort_pending_connection(pending);
      continue;
    }

    tcp_backlog_accepted(pending->socket.pcb);
    jh_lwip_tcp_socket_close(out_socket);
    *out_socket = pending->socket;
    ip4_addr_copy(*out_remote_address, pending->remote_address);
    *out_remote_port = pending->remote_port;
    reset_pending_connection(pending);
    configure_callbacks(out_socket);
    return HAL_OK;
  }
  return HAL_EAGAIN;
}

bool jh_lwip_tcp_listener_can_accept(const jh_lwip_tcp_listener_t *listener) {
  if (listener == nullptr || !listener->listening) {
    return false;
  }
  for (size_t offset = 0u; offset < listener->pending_count; ++offset) {
    const size_t index =
        (listener->pending_head + offset) % HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH;
    if (listener->pending[index].socket.pcb != nullptr) {
      return true;
    }
  }
  return false;
}

#endif
