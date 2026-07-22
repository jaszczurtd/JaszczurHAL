#include "../../../hal_target.h"

#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474
#include "../../../hal_config.h"
#endif

#if defined(JH_LWIP_RAW_TEST) ||                                               \
    ((HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474) &&                      \
     defined(HAL_NETWORK_BACKEND_CYW43) && defined(HAL_ENABLE_UDP))

#include "jh_lwip_status.h"
#include "jh_lwip_udp.h"

#include <string.h>

static void reset_datagram(jh_lwip_udp_datagram_t *datagram) {
  datagram->packet = nullptr;
  ip4_addr_set_zero(&datagram->remote_address);
  datagram->remote_port = 0u;
}

static void release_datagram(jh_lwip_udp_datagram_t *datagram) {
  if (datagram->packet != nullptr) {
    pbuf_free(datagram->packet);
  }
  reset_datagram(datagram);
}

static void receive_callback(void *argument, struct udp_pcb *,
                             struct pbuf *packet,
                             const ip_addr_t *remote_address,
                             uint16_t remote_port) {
  jh_lwip_udp_socket_t *socket = (jh_lwip_udp_socket_t *)argument;
  if (socket == nullptr || packet == nullptr || remote_address == nullptr ||
      !socket->bound || !IP_IS_V4(remote_address)) {
    if (packet != nullptr) {
      pbuf_free(packet);
    }
    return;
  }
  if (socket->receive_count >= HAL_LWIP_UDP_RX_QUEUE_DEPTH) {
    pbuf_free(packet);
    return;
  }

  const size_t tail = (socket->receive_head + socket->receive_count) %
                      HAL_LWIP_UDP_RX_QUEUE_DEPTH;
  jh_lwip_udp_datagram_t *datagram = &socket->receive_queue[tail];
  datagram->packet = packet;
  ip4_addr_copy(datagram->remote_address, *ip_2_ip4(remote_address));
  datagram->remote_port = remote_port;
  ++socket->receive_count;
}

static void release_receive_state(jh_lwip_udp_socket_t *socket) {
  release_datagram(&socket->current_receive);
  socket->current_receive_offset = 0u;
  for (size_t index = 0u; index < HAL_LWIP_UDP_RX_QUEUE_DEPTH; ++index) {
    release_datagram(&socket->receive_queue[index]);
  }
  socket->receive_head = 0u;
  socket->receive_count = 0u;
  ip4_addr_set_zero(&socket->last_remote_address);
  socket->last_remote_port = 0u;
}

static void release_transmit_state(jh_lwip_udp_socket_t *socket) {
  if (socket->transmit_packet != nullptr) {
    pbuf_free(socket->transmit_packet);
  }
  socket->transmit_packet = nullptr;
  ip4_addr_set_zero(&socket->transmit_remote_address);
  socket->transmit_remote_port = 0u;
  socket->transmit_started = false;
}

static void ip_address_from_ipv4(const ip4_addr_t *source,
                                 ip_addr_t *destination) {
  IP_SET_TYPE_VAL(*destination, IPADDR_TYPE_V4);
  ip4_addr_copy(*ip_2_ip4(destination), *source);
}

void jh_lwip_udp_socket_init(jh_lwip_udp_socket_t *socket) {
  if (socket != nullptr) {
    memset(socket, 0, sizeof(*socket));
  }
}

void jh_lwip_udp_socket_close(jh_lwip_udp_socket_t *socket) {
  if (socket == nullptr) {
    return;
  }
  if (socket->pcb != nullptr) {
    udp_recv(socket->pcb, nullptr, nullptr);
    udp_remove(socket->pcb);
  }
  socket->pcb = nullptr;
  socket->bound = false;
  release_receive_state(socket);
  release_transmit_state(socket);
}

hal_status_t jh_lwip_udp_socket_bind(jh_lwip_udp_socket_t *socket,
                                     uint16_t local_port) {
  if (socket == nullptr || local_port == 0u) {
    return HAL_EINVAL;
  }

  jh_lwip_udp_socket_close(socket);
  socket->pcb = udp_new_ip_type(IPADDR_TYPE_V4);
  if (socket->pcb == nullptr) {
    return HAL_ENOMEM;
  }

  ip_addr_t local_address;
  IP_ADDR4(&local_address, 0u, 0u, 0u, 0u);
  const err_t bind_status = udp_bind(socket->pcb, &local_address, local_port);
  if (bind_status != ERR_OK) {
    const hal_status_t status = jh_lwip_status_to_hal(bind_status);
    jh_lwip_udp_socket_close(socket);
    return status;
  }

  socket->bound = true;
  udp_recv(socket->pcb, receive_callback, socket);
  return HAL_OK;
}

hal_status_t jh_lwip_udp_socket_sendto(jh_lwip_udp_socket_t *socket,
                                       const void *data, size_t length,
                                       const ip4_addr_t *remote_address,
                                       uint16_t remote_port, size_t *out_sent) {
  if (out_sent != nullptr) {
    *out_sent = 0u;
  }
  if (socket == nullptr || remote_address == nullptr || remote_port == 0u ||
      out_sent == nullptr || (length > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  if (!socket->bound || socket->pcb == nullptr) {
    return HAL_ESTATE;
  }
  if (length > HAL_LWIP_UDP_MAX_PAYLOAD) {
    return HAL_EOVERFLOW;
  }

  struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, (uint16_t)length, PBUF_RAM);
  if (packet == nullptr) {
    return HAL_ENOMEM;
  }
  if (length > 0u && pbuf_take(packet, data, length) != ERR_OK) {
    pbuf_free(packet);
    return HAL_EIO;
  }

  ip_addr_t destination;
  ip_address_from_ipv4(remote_address, &destination);
  const hal_status_t status = jh_lwip_status_to_hal(
      udp_sendto(socket->pcb, packet, &destination, remote_port));
  pbuf_free(packet);
  if (status == HAL_OK) {
    *out_sent = length;
  }
  return status;
}

int jh_lwip_udp_socket_parse(jh_lwip_udp_socket_t *socket) {
  if (socket == nullptr || !socket->bound) {
    return 0;
  }
  if (socket->current_receive.packet != nullptr) {
    return (int)(socket->current_receive.packet->tot_len -
                 socket->current_receive_offset);
  }
  if (socket->receive_count == 0u) {
    return 0;
  }

  jh_lwip_udp_datagram_t *queued = &socket->receive_queue[socket->receive_head];
  socket->current_receive = *queued;
  reset_datagram(queued);
  socket->receive_head =
      (socket->receive_head + 1u) % HAL_LWIP_UDP_RX_QUEUE_DEPTH;
  --socket->receive_count;
  socket->current_receive_offset = 0u;
  ip4_addr_copy(socket->last_remote_address,
                socket->current_receive.remote_address);
  socket->last_remote_port = socket->current_receive.remote_port;
  return (int)socket->current_receive.packet->tot_len;
}

bool jh_lwip_udp_socket_has_packet(const jh_lwip_udp_socket_t *socket) {
  return socket != nullptr && socket->bound &&
         (socket->current_receive.packet != nullptr ||
          socket->receive_count > 0u);
}

hal_status_t jh_lwip_udp_socket_read(jh_lwip_udp_socket_t *socket, void *buffer,
                                     size_t max_length, bool discard_remainder,
                                     size_t *out_received) {
  if (out_received != nullptr) {
    *out_received = 0u;
  }
  if (socket == nullptr || out_received == nullptr ||
      (max_length > 0u && buffer == nullptr)) {
    return HAL_EINVAL;
  }
  if (!socket->bound) {
    return HAL_ESTATE;
  }
  if (socket->current_receive.packet == nullptr || max_length == 0u) {
    return HAL_OK;
  }

  const size_t remaining =
      socket->current_receive.packet->tot_len - socket->current_receive_offset;
  const size_t copy_length = max_length < remaining ? max_length : remaining;
  const uint16_t copied =
      pbuf_copy_partial(socket->current_receive.packet, buffer,
                        (uint16_t)copy_length, socket->current_receive_offset);
  if (copied != copy_length) {
    return HAL_EIO;
  }

  socket->current_receive_offset += copied;
  *out_received = copied;
  if (discard_remainder || socket->current_receive_offset >=
                               socket->current_receive.packet->tot_len) {
    release_datagram(&socket->current_receive);
    socket->current_receive_offset = 0u;
  }
  return HAL_OK;
}

bool jh_lwip_udp_socket_get_last_remote(const jh_lwip_udp_socket_t *socket,
                                        ip4_addr_t *out_address,
                                        uint16_t *out_port) {
  if (socket == nullptr || out_address == nullptr || out_port == nullptr ||
      socket->last_remote_port == 0u ||
      ip4_addr_isany_val(socket->last_remote_address)) {
    return false;
  }
  ip4_addr_copy(*out_address, socket->last_remote_address);
  *out_port = socket->last_remote_port;
  return true;
}

hal_status_t jh_lwip_udp_socket_begin_packet(jh_lwip_udp_socket_t *socket,
                                             const ip4_addr_t *remote_address,
                                             uint16_t remote_port) {
  if (socket == nullptr || remote_address == nullptr || remote_port == 0u) {
    return HAL_EINVAL;
  }
  if (!socket->bound || socket->pcb == nullptr) {
    return HAL_ESTATE;
  }

  release_transmit_state(socket);
  ip4_addr_copy(socket->transmit_remote_address, *remote_address);
  socket->transmit_remote_port = remote_port;
  socket->transmit_started = true;
  return HAL_OK;
}

hal_status_t jh_lwip_udp_socket_write(jh_lwip_udp_socket_t *socket,
                                      const void *data, size_t length,
                                      size_t *out_written) {
  if (out_written != nullptr) {
    *out_written = 0u;
  }
  if (socket == nullptr || out_written == nullptr ||
      (length > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  if (!socket->transmit_started) {
    return HAL_ESTATE;
  }
  if (length == 0u) {
    return HAL_OK;
  }
  const size_t current_length = socket->transmit_packet == nullptr
                                    ? 0u
                                    : socket->transmit_packet->tot_len;
  if (length > HAL_LWIP_UDP_MAX_PAYLOAD - current_length) {
    return HAL_EOVERFLOW;
  }

  const pbuf_layer layer =
      socket->transmit_packet == nullptr ? PBUF_TRANSPORT : PBUF_RAW;
  struct pbuf *segment = pbuf_alloc(layer, (uint16_t)length, PBUF_RAM);
  if (segment == nullptr) {
    return HAL_ENOMEM;
  }
  if (pbuf_take(segment, data, length) != ERR_OK) {
    pbuf_free(segment);
    return HAL_EIO;
  }

  if (socket->transmit_packet == nullptr) {
    socket->transmit_packet = segment;
  } else {
    pbuf_cat(socket->transmit_packet, segment);
  }
  *out_written = length;
  return HAL_OK;
}

hal_status_t jh_lwip_udp_socket_end_packet(jh_lwip_udp_socket_t *socket) {
  if (socket == nullptr || !socket->bound || socket->pcb == nullptr ||
      !socket->transmit_started) {
    return HAL_ESTATE;
  }
  if (socket->transmit_packet == nullptr) {
    socket->transmit_packet = pbuf_alloc(PBUF_TRANSPORT, 0u, PBUF_RAM);
    if (socket->transmit_packet == nullptr) {
      release_transmit_state(socket);
      return HAL_ENOMEM;
    }
  }

  ip_addr_t destination;
  ip_address_from_ipv4(&socket->transmit_remote_address, &destination);
  const hal_status_t status = jh_lwip_status_to_hal(
      udp_sendto(socket->pcb, socket->transmit_packet, &destination,
                 socket->transmit_remote_port));
  release_transmit_state(socket);
  return status;
}

bool jh_lwip_udp_socket_can_send(const jh_lwip_udp_socket_t *socket) {
  return socket != nullptr && socket->bound && socket->pcb != nullptr;
}

#endif
