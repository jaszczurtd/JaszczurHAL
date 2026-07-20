#include "lwip_fake.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

std::vector<udp_pcb *> udp_pcbs;
std::vector<tcp_pcb *> tcp_pcbs;
size_t allocated_pbufs = 0u;

bool udp_fail_next_new = false;
err_t udp_bind_status = ERR_OK;
err_t udp_send_status = ERR_OK;
udp_pcb *udp_last_pcb = nullptr;
size_t udp_removed_count = 0u;
size_t udp_send_count = 0u;
std::vector<uint8_t> udp_last_send;
ip4_addr_t udp_last_send_address = {};
uint16_t udp_last_send_port = 0u;

bool tcp_fail_next_new = false;
err_t tcp_connect_status = ERR_OK;
err_t tcp_bind_status = ERR_OK;
bool tcp_fail_next_listen = false;
err_t tcp_write_status = ERR_OK;
err_t tcp_output_status = ERR_OK;
err_t tcp_close_status = ERR_OK;
tcp_pcb *tcp_last_pcb = nullptr;
size_t tcp_close_count = 0u;
size_t tcp_abort_count = 0u;
size_t tcp_output_count = 0u;
size_t tcp_recved_count = 0u;
size_t tcp_backlog_delayed_count = 0u;
size_t tcp_backlog_accepted_count = 0u;
std::vector<uint8_t> tcp_last_write;

uint16_t recompute_total_lengths(pbuf *packet) {
  if (packet == nullptr) {
    return 0u;
  }
  packet->tot_len =
      (uint16_t)(packet->len + recompute_total_lengths(packet->next));
  return packet->tot_len;
}

} // namespace

extern "C" {

struct pbuf *pbuf_alloc(pbuf_layer, uint16_t length, pbuf_type) {
  pbuf *packet = new pbuf{};
  packet->allocation = new uint8_t[std::max<uint16_t>(length, 1u)];
  packet->payload = packet->allocation;
  packet->len = length;
  packet->tot_len = length;
  ++allocated_pbufs;
  return packet;
}

uint8_t pbuf_free(struct pbuf *packet) {
  uint8_t freed = 0u;
  while (packet != nullptr) {
    pbuf *next = packet->next;
    delete[] packet->allocation;
    delete packet;
    --allocated_pbufs;
    ++freed;
    packet = next;
  }
  return freed;
}

err_t pbuf_take(struct pbuf *packet, const void *source, size_t length) {
  if ((length > 0u && source == nullptr) || packet == nullptr ||
      length > packet->tot_len) {
    return ERR_ARG;
  }

  const uint8_t *input = static_cast<const uint8_t *>(source);
  size_t remaining = length;
  while (packet != nullptr && remaining > 0u) {
    const size_t copied = std::min<size_t>(remaining, packet->len);
    std::memcpy(packet->payload, input, copied);
    input += copied;
    remaining -= copied;
    packet = packet->next;
  }
  return remaining == 0u ? ERR_OK : ERR_BUF;
}

void pbuf_cat(struct pbuf *head, struct pbuf *tail) {
  if (head == nullptr || tail == nullptr) {
    return;
  }
  const uint16_t added_length = tail->tot_len;
  pbuf *current = head;
  while (current->next != nullptr) {
    current->tot_len = (uint16_t)(current->tot_len + added_length);
    current = current->next;
  }
  current->tot_len = (uint16_t)(current->tot_len + added_length);
  current->next = tail;
}

uint16_t pbuf_copy_partial(const struct pbuf *packet, void *destination,
                           uint16_t length, uint16_t offset) {
  if (length > 0u && destination == nullptr) {
    return 0u;
  }
  while (packet != nullptr && offset >= packet->len) {
    offset = (uint16_t)(offset - packet->len);
    packet = packet->next;
  }

  uint8_t *output = static_cast<uint8_t *>(destination);
  uint16_t copied = 0u;
  while (packet != nullptr && copied < length) {
    const uint16_t available = (uint16_t)(packet->len - offset);
    const uint16_t part =
        std::min<uint16_t>(available, (uint16_t)(length - copied));
    std::memcpy(output + copied,
                static_cast<const uint8_t *>(packet->payload) + offset, part);
    copied = (uint16_t)(copied + part);
    offset = 0u;
    packet = packet->next;
  }
  return copied;
}

struct pbuf *pbuf_free_header(struct pbuf *packet, uint16_t length) {
  uint16_t remaining = length;
  while (packet != nullptr && remaining >= packet->len) {
    remaining = (uint16_t)(remaining - packet->len);
    pbuf *next = packet->next;
    packet->next = nullptr;
    pbuf_free(packet);
    packet = next;
    if (remaining == 0u) {
      break;
    }
  }
  if (packet != nullptr && remaining > 0u) {
    packet->payload = static_cast<uint8_t *>(packet->payload) + remaining;
    packet->len = (uint16_t)(packet->len - remaining);
  }
  recompute_total_lengths(packet);
  return packet;
}

struct udp_pcb *udp_new_ip_type(uint8_t) {
  if (udp_fail_next_new) {
    udp_fail_next_new = false;
    return nullptr;
  }
  udp_pcb *pcb = new udp_pcb{};
  udp_pcbs.push_back(pcb);
  udp_last_pcb = pcb;
  return pcb;
}

err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *, uint16_t local_port) {
  if (udp_bind_status == ERR_OK) {
    pcb->local_port = local_port;
  }
  return udp_bind_status;
}

void udp_recv(struct udp_pcb *pcb, udp_recv_fn receive, void *argument) {
  pcb->receive = receive;
  pcb->receive_argument = argument;
}

err_t udp_sendto(struct udp_pcb *, struct pbuf *packet,
                 const ip_addr_t *remote_address, uint16_t remote_port) {
  ++udp_send_count;
  udp_last_send.resize(packet->tot_len);
  if (!udp_last_send.empty()) {
    pbuf_copy_partial(packet, udp_last_send.data(), packet->tot_len, 0u);
  }
  ip4_addr_copy(udp_last_send_address, *ip_2_ip4(remote_address));
  udp_last_send_port = remote_port;
  return udp_send_status;
}

void udp_remove(struct udp_pcb *pcb) {
  pcb->removed = true;
  ++udp_removed_count;
}

struct tcp_pcb *tcp_new_ip_type(uint8_t) {
  if (tcp_fail_next_new) {
    tcp_fail_next_new = false;
    return nullptr;
  }
  tcp_pcb *pcb = new tcp_pcb{};
  pcb->send_buffer = 4096u;
  IP_SET_TYPE_VAL(pcb->local_ip, IPADDR_TYPE_V4);
  IP_SET_TYPE_VAL(pcb->remote_ip, IPADDR_TYPE_V4);
  tcp_pcbs.push_back(pcb);
  tcp_last_pcb = pcb;
  return pcb;
}

void tcp_arg(struct tcp_pcb *pcb, void *argument) {
  pcb->callback_argument = argument;
}

void tcp_recv(struct tcp_pcb *pcb, tcp_recv_fn receive) {
  pcb->receive = receive;
}

void tcp_err(struct tcp_pcb *pcb, tcp_err_fn error) { pcb->error = error; }

void tcp_sent(struct tcp_pcb *pcb, tcp_sent_fn sent) { pcb->sent = sent; }

void tcp_poll(struct tcp_pcb *pcb, tcp_poll_fn poll, uint8_t) {
  pcb->poll = poll;
}

void tcp_accept(struct tcp_pcb *pcb, tcp_accept_fn accept) {
  pcb->accept = accept;
}

err_t tcp_close(struct tcp_pcb *pcb) {
  ++tcp_close_count;
  if (tcp_close_status == ERR_OK) {
    tcp_backlog_accepted(pcb);
    pcb->closed = true;
    pcb->connected = nullptr;
    pcb->accept = nullptr;
  }
  return tcp_close_status;
}

void tcp_abort(struct tcp_pcb *pcb) {
  tcp_backlog_accepted(pcb);
  pcb->aborted = true;
  pcb->callback_argument = nullptr;
  pcb->receive = nullptr;
  pcb->error = nullptr;
  pcb->connected = nullptr;
  pcb->accept = nullptr;
  ++tcp_abort_count;
}

err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *local_address,
               uint16_t local_port) {
  if (tcp_bind_status == ERR_OK) {
    pcb->local_ip = *local_address;
    pcb->local_port = local_port;
  }
  return tcp_bind_status;
}

struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *pcb, uint8_t backlog) {
  if (tcp_fail_next_listen) {
    tcp_fail_next_listen = false;
    return nullptr;
  }
  tcp_pcb *listener = new tcp_pcb{};
  listener->local_ip = pcb->local_ip;
  listener->local_port = pcb->local_port;
  listener->backlog = backlog == 0u ? 1u : backlog;
  listener->listening = true;
  listener->send_buffer = 4096u;
  pcb->closed = true;
  tcp_pcbs.push_back(listener);
  tcp_last_pcb = listener;
  return listener;
}

void tcp_backlog_delayed(struct tcp_pcb *pcb) {
  if (!pcb->backlog_delayed) {
    pcb->backlog_delayed = true;
    ++tcp_backlog_delayed_count;
  }
}

void tcp_backlog_accepted(struct tcp_pcb *pcb) {
  if (pcb->backlog_delayed) {
    pcb->backlog_delayed = false;
    ++tcp_backlog_accepted_count;
  }
}

err_t tcp_connect(struct tcp_pcb *pcb, const ip_addr_t *remote_address,
                  uint16_t remote_port, tcp_connected_fn connected) {
  pcb->remote_ip = *remote_address;
  pcb->remote_port = remote_port;
  pcb->connected = connected;
  return tcp_connect_status;
}

uint16_t tcp_sndbuf(const struct tcp_pcb *pcb) { return pcb->send_buffer; }

err_t tcp_write(struct tcp_pcb *pcb, const void *data, uint16_t length,
                uint8_t) {
  if (tcp_write_status != ERR_OK) {
    return tcp_write_status;
  }
  tcp_last_write.assign(static_cast<const uint8_t *>(data),
                        static_cast<const uint8_t *>(data) + length);
  pcb->send_buffer = (uint16_t)(pcb->send_buffer - length);
  return ERR_OK;
}

err_t tcp_output(struct tcp_pcb *) {
  ++tcp_output_count;
  return tcp_output_status;
}

void tcp_recved(struct tcp_pcb *, uint16_t length) {
  tcp_recved_count += length;
}

void lwip_fake_reset(void) {
  for (udp_pcb *pcb : udp_pcbs) {
    delete pcb;
  }
  udp_pcbs.clear();
  for (tcp_pcb *pcb : tcp_pcbs) {
    delete pcb;
  }
  tcp_pcbs.clear();

  udp_fail_next_new = false;
  udp_bind_status = ERR_OK;
  udp_send_status = ERR_OK;
  udp_last_pcb = nullptr;
  udp_removed_count = 0u;
  udp_send_count = 0u;
  udp_last_send.clear();
  udp_last_send_address = {};
  udp_last_send_port = 0u;

  tcp_fail_next_new = false;
  tcp_connect_status = ERR_OK;
  tcp_bind_status = ERR_OK;
  tcp_fail_next_listen = false;
  tcp_write_status = ERR_OK;
  tcp_output_status = ERR_OK;
  tcp_close_status = ERR_OK;
  tcp_last_pcb = nullptr;
  tcp_close_count = 0u;
  tcp_abort_count = 0u;
  tcp_output_count = 0u;
  tcp_recved_count = 0u;
  tcp_backlog_delayed_count = 0u;
  tcp_backlog_accepted_count = 0u;
  tcp_last_write.clear();
}

size_t lwip_fake_pbuf_count(void) { return allocated_pbufs; }

void lwip_fake_udp_fail_next_new(void) { udp_fail_next_new = true; }
void lwip_fake_udp_set_bind_status(err_t status) { udp_bind_status = status; }
void lwip_fake_udp_set_send_status(err_t status) { udp_send_status = status; }
struct udp_pcb *lwip_fake_udp_last_pcb(void) { return udp_last_pcb; }
size_t lwip_fake_udp_removed_count(void) { return udp_removed_count; }
size_t lwip_fake_udp_send_count(void) { return udp_send_count; }
size_t lwip_fake_udp_last_send_length(void) { return udp_last_send.size(); }
const uint8_t *lwip_fake_udp_last_send_data(void) {
  return udp_last_send.data();
}
ip4_addr_t lwip_fake_udp_last_send_address(void) {
  return udp_last_send_address;
}
uint16_t lwip_fake_udp_last_send_port(void) { return udp_last_send_port; }

void lwip_fake_udp_receive(struct udp_pcb *pcb, const void *data, size_t length,
                           const ip4_addr_t *remote_address,
                           uint16_t remote_port) {
  pbuf *packet = pbuf_alloc(PBUF_RAW, (uint16_t)length, PBUF_RAM);
  if (length > 0u) {
    (void)pbuf_take(packet, data, length);
  }
  if (pcb->receive == nullptr) {
    pbuf_free(packet);
    return;
  }
  ip_addr_t address;
  IP_SET_TYPE_VAL(address, IPADDR_TYPE_V4);
  ip4_addr_copy(*ip_2_ip4(&address), *remote_address);
  pcb->receive(pcb->receive_argument, pcb, packet, &address, remote_port);
}

void lwip_fake_tcp_fail_next_new(void) { tcp_fail_next_new = true; }
void lwip_fake_tcp_set_connect_status(err_t status) {
  tcp_connect_status = status;
}
void lwip_fake_tcp_set_bind_status(err_t status) { tcp_bind_status = status; }
void lwip_fake_tcp_fail_next_listen(void) { tcp_fail_next_listen = true; }
void lwip_fake_tcp_set_write_status(err_t status) { tcp_write_status = status; }
void lwip_fake_tcp_set_output_status(err_t status) {
  tcp_output_status = status;
}
void lwip_fake_tcp_set_close_status(err_t status) { tcp_close_status = status; }
struct tcp_pcb *lwip_fake_tcp_last_pcb(void) { return tcp_last_pcb; }
size_t lwip_fake_tcp_close_count(void) { return tcp_close_count; }
size_t lwip_fake_tcp_abort_count(void) { return tcp_abort_count; }
size_t lwip_fake_tcp_output_count(void) { return tcp_output_count; }
size_t lwip_fake_tcp_recved_count(void) { return tcp_recved_count; }
size_t lwip_fake_tcp_last_write_length(void) { return tcp_last_write.size(); }
const uint8_t *lwip_fake_tcp_last_write_data(void) {
  return tcp_last_write.data();
}
size_t lwip_fake_tcp_backlog_delayed_count(void) {
  return tcp_backlog_delayed_count;
}
size_t lwip_fake_tcp_backlog_accepted_count(void) {
  return tcp_backlog_accepted_count;
}

err_t lwip_fake_tcp_connected(struct tcp_pcb *pcb, err_t status) {
  return pcb->connected == nullptr
             ? ERR_CLSD
             : pcb->connected(pcb->callback_argument, pcb, status);
}

err_t lwip_fake_tcp_incoming(struct tcp_pcb *listener,
                             const ip4_addr_t *remote_address,
                             uint16_t remote_port,
                             struct tcp_pcb **out_client) {
  if (out_client != nullptr) {
    *out_client = nullptr;
  }
  if (listener == nullptr || listener->accept == nullptr ||
      remote_address == nullptr || remote_port == 0u) {
    return ERR_ARG;
  }

  tcp_pcb *client = tcp_new_ip_type(IPADDR_TYPE_V4);
  ip4_addr_copy(*ip_2_ip4(&client->remote_ip), *remote_address);
  client->remote_port = remote_port;
  if (out_client != nullptr) {
    *out_client = client;
  }
  const err_t status =
      listener->accept(listener->callback_argument, client, ERR_OK);
  if (status != ERR_OK && status != ERR_ABRT) {
    tcp_abort(client);
  }
  return status;
}

err_t lwip_fake_tcp_receive(struct tcp_pcb *pcb, const void *data,
                            size_t length) {
  pbuf *packet = pbuf_alloc(PBUF_RAW, (uint16_t)length, PBUF_RAM);
  if (length > 0u) {
    (void)pbuf_take(packet, data, length);
  }
  if (pcb->receive == nullptr) {
    pbuf_free(packet);
    return ERR_CLSD;
  }
  const err_t status =
      pcb->receive(pcb->callback_argument, pcb, packet, ERR_OK);
  if (status != ERR_OK) {
    pbuf_free(packet);
  }
  return status;
}

err_t lwip_fake_tcp_fin(struct tcp_pcb *pcb) {
  return pcb->receive == nullptr
             ? ERR_CLSD
             : pcb->receive(pcb->callback_argument, pcb, nullptr, ERR_OK);
}

void lwip_fake_tcp_error(struct tcp_pcb *pcb, err_t status) {
  tcp_err_fn callback = pcb->error;
  void *argument = pcb->callback_argument;
  tcp_backlog_accepted(pcb);
  pcb->callback_argument = nullptr;
  pcb->receive = nullptr;
  pcb->error = nullptr;
  pcb->connected = nullptr;
  pcb->closed = true;
  if (callback != nullptr) {
    callback(argument, status);
  }
}

} // extern "C"
