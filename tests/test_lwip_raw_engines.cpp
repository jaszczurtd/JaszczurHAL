#include "fakes/lwip_fake.h"
#include "hal/core/hal_array.h"
#include "hal/network/jh_lwip_tcp.h"
#include "hal/network/jh_lwip_udp.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) {
  TEST_ASSERT_EQUAL_size_t(0u, lwip_fake_pbuf_count());
  lwip_fake_reset();
}

void tearDown(void) {
  const size_t outstanding_pbufs = lwip_fake_pbuf_count();
  lwip_fake_reset();
  TEST_ASSERT_EQUAL_size_t(0u, outstanding_pbufs);
}

static ip4_addr_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  ip4_addr_t address;
  IP4_ADDR(&address, a, b, c, d);
  return address;
}

static udp_pcb *open_udp(jh_lwip_udp_socket_t *socket, uint16_t port) {
  jh_lwip_udp_socket_init(socket);
  if (jh_lwip_udp_socket_bind(socket, port) != HAL_OK) {
    return nullptr;
  }
  return lwip_fake_udp_last_pcb();
}

static tcp_pcb *open_connected_tcp(jh_lwip_tcp_socket_t *socket) {
  const ip4_addr_t remote = ipv4(192u, 0u, 2u, 8u);
  jh_lwip_tcp_socket_init(socket);
  if (jh_lwip_tcp_socket_connect(socket, &remote, 8080u) != HAL_OK) {
    return nullptr;
  }
  tcp_pcb *pcb = lwip_fake_tcp_last_pcb();
  if (lwip_fake_tcp_connected(pcb, ERR_OK) != ERR_OK) {
    return nullptr;
  }
  return pcb;
}

static tcp_pcb *open_tcp_listener(jh_lwip_tcp_listener_t *listener,
                                  uint16_t port, uint8_t backlog) {
  const ip4_addr_t local = ipv4(0u, 0u, 0u, 0u);
  jh_lwip_tcp_listener_init(listener);
  if (jh_lwip_tcp_listener_bind(listener, &local, port) != HAL_OK ||
      jh_lwip_tcp_listener_listen(listener, backlog) != HAL_OK) {
    return nullptr;
  }
  return listener->pcb;
}

void test_udp_bind_and_send_capture(void) {
  jh_lwip_udp_socket_t socket;
  udp_pcb *pcb = open_udp(&socket, 4321u);
  TEST_ASSERT_NOT_NULL(pcb);
  TEST_ASSERT_EQUAL_UINT16(4321u, pcb->local_port);
  TEST_ASSERT_TRUE(jh_lwip_udp_socket_can_send(&socket));

  const uint8_t payload[] = {0x10u, 0x20u, 0x30u};
  const ip4_addr_t remote = ipv4(1u, 1u, 1u, 1u);
  size_t sent = 99u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lwip_udp_socket_sendto(&socket, payload,
                                                          sizeof(payload),
                                                          &remote, 53u, &sent));
  TEST_ASSERT_EQUAL_size_t(sizeof(payload), sent);
  TEST_ASSERT_EQUAL_size_t(1u, lwip_fake_udp_send_count());
  TEST_ASSERT_EQUAL_size_t(sizeof(payload), lwip_fake_udp_last_send_length());
  TEST_ASSERT_EQUAL_MEMORY(payload, lwip_fake_udp_last_send_data(),
                           sizeof(payload));
  TEST_ASSERT_EQUAL_UINT32(remote.addr, lwip_fake_udp_last_send_address().addr);
  TEST_ASSERT_EQUAL_UINT16(53u, lwip_fake_udp_last_send_port());

  lwip_fake_udp_set_send_status(ERR_RTE);
  TEST_ASSERT_EQUAL_INT(
      HAL_EIO, jh_lwip_udp_socket_sendto(&socket, payload, sizeof(payload),
                                         &remote, 53u, &sent));
  TEST_ASSERT_EQUAL_size_t(0u, sent);
  jh_lwip_udp_socket_close(&socket);
}

void test_udp_bind_failures_release_the_pcb(void) {
  jh_lwip_udp_socket_t socket;
  jh_lwip_udp_socket_init(&socket);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_lwip_udp_socket_bind(&socket, 0u));

  lwip_fake_udp_fail_next_new();
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, jh_lwip_udp_socket_bind(&socket, 1234u));

  lwip_fake_udp_set_bind_status(ERR_USE);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, jh_lwip_udp_socket_bind(&socket, 1234u));
  TEST_ASSERT_NULL(socket.pcb);
  TEST_ASSERT_EQUAL_size_t(1u, lwip_fake_udp_removed_count());
}

void test_udp_receive_preserves_queue_order_and_source(void) {
  jh_lwip_udp_socket_t socket;
  udp_pcb *pcb = open_udp(&socket, 4321u);
  TEST_ASSERT_NOT_NULL(pcb);
  const ip4_addr_t first_address = ipv4(192u, 0u, 2u, 1u);
  const ip4_addr_t second_address = ipv4(198u, 51u, 100u, 2u);
  const char first[] = "one";
  const char second[] = "two";
  lwip_fake_udp_receive(pcb, first, 3u, &first_address, 1001u);
  lwip_fake_udp_receive(pcb, second, 3u, &second_address, 1002u);

  char buffer[4] = {};
  size_t received = 0u;
  ip4_addr_t source = {};
  uint16_t source_port = 0u;
  TEST_ASSERT_EQUAL_INT(3, jh_lwip_udp_socket_parse(&socket));
  TEST_ASSERT_TRUE(
      jh_lwip_udp_socket_get_last_remote(&socket, &source, &source_port));
  TEST_ASSERT_EQUAL_UINT32(first_address.addr, source.addr);
  TEST_ASSERT_EQUAL_UINT16(1001u, source_port);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_udp_socket_read(&socket, buffer, 3u, false, &received));
  TEST_ASSERT_EQUAL_size_t(3u, received);
  TEST_ASSERT_EQUAL_MEMORY(first, buffer, 3u);

  memset(buffer, 0, sizeof(buffer));
  TEST_ASSERT_EQUAL_INT(3, jh_lwip_udp_socket_parse(&socket));
  TEST_ASSERT_TRUE(
      jh_lwip_udp_socket_get_last_remote(&socket, &source, &source_port));
  TEST_ASSERT_EQUAL_UINT32(second_address.addr, source.addr);
  TEST_ASSERT_EQUAL_UINT16(1002u, source_port);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_udp_socket_read(&socket, buffer, 3u, false, &received));
  TEST_ASSERT_EQUAL_MEMORY(second, buffer, 3u);
  jh_lwip_udp_socket_close(&socket);
}

void test_udp_partial_read_discard_and_zero_length_packet(void) {
  jh_lwip_udp_socket_t socket;
  udp_pcb *pcb = open_udp(&socket, 4321u);
  TEST_ASSERT_NOT_NULL(pcb);
  const ip4_addr_t remote = ipv4(203u, 0u, 113u, 7u);
  const char payload[] = "abcde";
  char buffer[6] = {};
  size_t received = 0u;

  lwip_fake_udp_receive(pcb, payload, 5u, &remote, 5000u);
  TEST_ASSERT_EQUAL_INT(5, jh_lwip_udp_socket_parse(&socket));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_udp_socket_read(&socket, buffer, 2u, false, &received));
  TEST_ASSERT_EQUAL_size_t(2u, received);
  TEST_ASSERT_EQUAL_MEMORY("ab", buffer, 2u);
  TEST_ASSERT_EQUAL_INT(3, jh_lwip_udp_socket_parse(&socket));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_udp_socket_read(&socket, buffer, 2u, true, &received));
  TEST_ASSERT_EQUAL_size_t(2u, received);
  TEST_ASSERT_FALSE(jh_lwip_udp_socket_has_packet(&socket));

  lwip_fake_udp_receive(pcb, nullptr, 0u, &remote, 5001u);
  TEST_ASSERT_TRUE(jh_lwip_udp_socket_has_packet(&socket));
  TEST_ASSERT_EQUAL_INT(0, jh_lwip_udp_socket_parse(&socket));
  TEST_ASSERT_TRUE(jh_lwip_udp_socket_has_packet(&socket));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_udp_socket_read(&socket, buffer, 1u, false, &received));
  TEST_ASSERT_EQUAL_size_t(0u, received);
  TEST_ASSERT_FALSE(jh_lwip_udp_socket_has_packet(&socket));
  jh_lwip_udp_socket_close(&socket);
}

void test_udp_full_queue_drops_newest_packet(void) {
  jh_lwip_udp_socket_t socket;
  udp_pcb *pcb = open_udp(&socket, 4321u);
  TEST_ASSERT_NOT_NULL(pcb);
  const ip4_addr_t remote = ipv4(192u, 0u, 2u, 9u);
  const uint8_t payloads[] = {1u, 2u, 3u};
  for (size_t index = 0u; index < sizeof(payloads); ++index) {
    lwip_fake_udp_receive(pcb, &payloads[index], 1u, &remote,
                          (uint16_t)(6000u + index));
  }
  TEST_ASSERT_EQUAL_size_t(HAL_LWIP_UDP_RX_QUEUE_DEPTH, socket.receive_count);
  TEST_ASSERT_EQUAL_size_t(HAL_LWIP_UDP_RX_QUEUE_DEPTH, lwip_fake_pbuf_count());

  for (uint8_t expected = 1u; expected <= 2u; ++expected) {
    uint8_t actual = 0u;
    size_t received = 0u;
    TEST_ASSERT_EQUAL_INT(1, jh_lwip_udp_socket_parse(&socket));
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lwip_udp_socket_read(&socket, &actual, 1u,
                                                          false, &received));
    TEST_ASSERT_EQUAL_UINT8(expected, actual);
  }
  TEST_ASSERT_FALSE(jh_lwip_udp_socket_has_packet(&socket));
  jh_lwip_udp_socket_close(&socket);
}

void test_udp_packet_builder_overflow_and_close_cleanup(void) {
  jh_lwip_udp_socket_t socket;
  udp_pcb *pcb = open_udp(&socket, 4321u);
  TEST_ASSERT_NOT_NULL(pcb);
  const ip4_addr_t remote = ipv4(8u, 8u, 8u, 8u);
  const char first[] = "abcd";
  const char second[] = "efgh";
  size_t written = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_udp_socket_begin_packet(&socket, &remote, 53u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_udp_socket_write(&socket, first, 4u, &written));
  TEST_ASSERT_EQUAL_size_t(4u, written);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_udp_socket_write(&socket, second, 4u, &written));
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        jh_lwip_udp_socket_write(&socket, "x", 1u, &written));
  TEST_ASSERT_EQUAL_size_t(0u, written);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lwip_udp_socket_end_packet(&socket));
  TEST_ASSERT_EQUAL_size_t(8u, lwip_fake_udp_last_send_length());
  TEST_ASSERT_EQUAL_MEMORY("abcdefgh", lwip_fake_udp_last_send_data(), 8u);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_udp_socket_begin_packet(&socket, &remote, 53u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_udp_socket_write(&socket, first, 4u, &written));
  lwip_fake_udp_receive(pcb, first, 4u, &remote, 7000u);
  lwip_fake_udp_receive(pcb, second, 4u, &remote, 7001u);
  TEST_ASSERT_EQUAL_INT(4, jh_lwip_udp_socket_parse(&socket));
  TEST_ASSERT_EQUAL_size_t(3u, lwip_fake_pbuf_count());

  jh_lwip_udp_socket_close(&socket);
  TEST_ASSERT_EQUAL_size_t(0u, lwip_fake_pbuf_count());
  TEST_ASSERT_NULL(pcb->receive);
  TEST_ASSERT_TRUE(pcb->removed);
  lwip_fake_udp_receive(pcb, first, 4u, &remote, 7002u);
  TEST_ASSERT_EQUAL_size_t(0u, lwip_fake_pbuf_count());
}

void test_tcp_connect_success_and_failure_states(void) {
  jh_lwip_tcp_socket_t socket;
  const ip4_addr_t remote = ipv4(192u, 0u, 2u, 8u);
  jh_lwip_tcp_socket_init(&socket);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_tcp_socket_connect(&socket, &remote, 8080u));
  tcp_pcb *pcb = lwip_fake_tcp_last_pcb();
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        jh_lwip_tcp_socket_connection_status(&socket));
  TEST_ASSERT_EQUAL_UINT32(remote.addr, ip_2_ip4(&pcb->remote_ip)->addr);
  TEST_ASSERT_EQUAL_UINT16(8080u, pcb->remote_port);
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_connected(pcb, ERR_OK));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lwip_tcp_socket_connection_status(&socket));
  jh_lwip_tcp_socket_close(&socket);

  jh_lwip_tcp_socket_init(&socket);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_tcp_socket_connect(&socket, &remote, 8080u));
  pcb = lwip_fake_tcp_last_pcb();
  TEST_ASSERT_EQUAL_INT(ERR_RST, lwip_fake_tcp_connected(pcb, ERR_RST));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, jh_lwip_tcp_socket_connection_status(&socket));
  jh_lwip_tcp_socket_close(&socket);

  jh_lwip_tcp_socket_init(&socket);
  lwip_fake_tcp_set_connect_status(ERR_RTE);
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        jh_lwip_tcp_socket_connect(&socket, &remote, 8080u));
  TEST_ASSERT_NULL(socket.pcb);
}

void test_tcp_partial_send_and_output_error(void) {
  jh_lwip_tcp_socket_t socket;
  tcp_pcb *pcb = open_connected_tcp(&socket);
  TEST_ASSERT_NOT_NULL(pcb);
  const char payload[] = "abcde";
  size_t sent = 99u;

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_tcp_socket_send(&socket, nullptr, 0u, &sent));
  TEST_ASSERT_EQUAL_size_t(0u, sent);
  pcb->send_buffer = 3u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_tcp_socket_send(&socket, payload, 5u, &sent));
  TEST_ASSERT_EQUAL_size_t(3u, sent);
  TEST_ASSERT_EQUAL_size_t(3u, lwip_fake_tcp_last_write_length());
  TEST_ASSERT_EQUAL_MEMORY("abc", lwip_fake_tcp_last_write_data(), 3u);
  TEST_ASSERT_EQUAL_size_t(1u, lwip_fake_tcp_output_count());
  TEST_ASSERT_FALSE(jh_lwip_tcp_socket_can_send(&socket));

  pcb->send_buffer = 4u;
  lwip_fake_tcp_set_output_status(ERR_RTE);
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        jh_lwip_tcp_socket_send(&socket, payload, 2u, &sent));
  TEST_ASSERT_EQUAL_size_t(2u, sent);
  jh_lwip_tcp_socket_close(&socket);
}

void test_tcp_send_backpressure_preserves_connection_and_allows_retry(void) {
  jh_lwip_tcp_socket_t socket;
  tcp_pcb *pcb = open_connected_tcp(&socket);
  TEST_ASSERT_NOT_NULL(pcb);
  pcb->send_buffer = 0u;
  size_t sent = 99u;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        jh_lwip_tcp_socket_send(&socket, "data", 4u, &sent));
  TEST_ASSERT_EQUAL_size_t(0u, sent);
  TEST_ASSERT_TRUE(jh_lwip_tcp_socket_is_connected(&socket));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_tcp_socket_send(&socket, nullptr, 0u, &sent));

  pcb->send_buffer = 4u;
  const err_t resource_errors[] = {ERR_MEM, ERR_BUF};
  for (size_t i = 0u; i < COUNTOF(resource_errors); ++i) {
    lwip_fake_tcp_set_write_status(resource_errors[i]);
    TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                          jh_lwip_tcp_socket_send(&socket, "data", 4u, &sent));
    TEST_ASSERT_EQUAL_size_t(0u, sent);
    TEST_ASSERT_TRUE(jh_lwip_tcp_socket_is_connected(&socket));
  }
  lwip_fake_tcp_set_write_status(ERR_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_tcp_socket_send(&socket, "data", 4u, &sent));
  TEST_ASSERT_EQUAL_size_t(4u, sent);
  TEST_ASSERT_EQUAL_MEMORY("data", lwip_fake_tcp_last_write_data(), 4u);
  jh_lwip_tcp_socket_close(&socket);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        jh_lwip_tcp_socket_send(&socket, "data", 4u, &sent));
  TEST_ASSERT_EQUAL_size_t(0u, sent);
}

void test_tcp_fragmented_receive_and_window_acknowledgement(void) {
  jh_lwip_tcp_socket_t socket;
  tcp_pcb *pcb = open_connected_tcp(&socket);
  TEST_ASSERT_NOT_NULL(pcb);
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_receive(pcb, "abc", 3u));
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_receive(pcb, "def", 3u));
  TEST_ASSERT_EQUAL_size_t(6u, jh_lwip_tcp_socket_available(&socket));

  char buffer[7] = {};
  size_t received = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_tcp_socket_receive(&socket, buffer, 4u, &received));
  TEST_ASSERT_EQUAL_size_t(4u, received);
  TEST_ASSERT_EQUAL_MEMORY("abcd", buffer, 4u);
  TEST_ASSERT_EQUAL_size_t(2u, jh_lwip_tcp_socket_available(&socket));
  TEST_ASSERT_EQUAL_size_t(4u, lwip_fake_tcp_recved_count());

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_tcp_socket_receive(&socket, buffer + 4u, 2u, &received));
  TEST_ASSERT_EQUAL_MEMORY("abcdef", buffer, 6u);
  TEST_ASSERT_EQUAL_size_t(6u, lwip_fake_tcp_recved_count());
  TEST_ASSERT_EQUAL_size_t(0u, jh_lwip_tcp_socket_available(&socket));
  jh_lwip_tcp_socket_close(&socket);
}

void test_tcp_fin_keeps_buffered_data_readable(void) {
  jh_lwip_tcp_socket_t socket;
  tcp_pcb *pcb = open_connected_tcp(&socket);
  TEST_ASSERT_NOT_NULL(pcb);
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_receive(pcb, "data", 4u));
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_fin(pcb));
  TEST_ASSERT_TRUE(jh_lwip_tcp_socket_is_connected(&socket));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        jh_lwip_tcp_socket_connection_status(&socket));

  char buffer[4] = {};
  size_t received = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_tcp_socket_receive(&socket, buffer, 4u, &received));
  TEST_ASSERT_EQUAL_MEMORY("data", buffer, 4u);
  TEST_ASSERT_FALSE(jh_lwip_tcp_socket_is_connected(&socket));
  jh_lwip_tcp_socket_close(&socket);
}

void test_tcp_error_callback_invalidates_pcb(void) {
  jh_lwip_tcp_socket_t socket;
  tcp_pcb *pcb = open_connected_tcp(&socket);
  TEST_ASSERT_NOT_NULL(pcb);
  lwip_fake_tcp_error(pcb, ERR_RST);
  TEST_ASSERT_NULL(socket.pcb);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, jh_lwip_tcp_socket_connection_status(&socket));
  TEST_ASSERT_FALSE(jh_lwip_tcp_socket_can_send(&socket));
  jh_lwip_tcp_socket_close(&socket);
}

void test_tcp_close_aborts_when_graceful_close_has_no_memory(void) {
  jh_lwip_tcp_socket_t socket;
  tcp_pcb *pcb = open_connected_tcp(&socket);
  TEST_ASSERT_NOT_NULL(pcb);
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_receive(pcb, "data", 4u));
  lwip_fake_tcp_set_close_status(ERR_MEM);

  jh_lwip_tcp_socket_close(&socket);
  TEST_ASSERT_EQUAL_size_t(1u, lwip_fake_tcp_close_count());
  TEST_ASSERT_EQUAL_size_t(1u, lwip_fake_tcp_abort_count());
  TEST_ASSERT_TRUE(pcb->aborted);
  TEST_ASSERT_NULL(pcb->callback_argument);
  TEST_ASSERT_NULL(pcb->receive);
  TEST_ASSERT_NULL(pcb->error);
  TEST_ASSERT_NULL(pcb->connected);
  TEST_ASSERT_EQUAL_INT(JH_LWIP_TCP_CLOSED, socket.state);
  TEST_ASSERT_EQUAL_size_t(0u, lwip_fake_pbuf_count());
}

void test_tcp_receive_limit_applies_backpressure_without_ownership_leak(void) {
  jh_lwip_tcp_socket_t socket;
  tcp_pcb *pcb = open_connected_tcp(&socket);
  TEST_ASSERT_NOT_NULL(pcb);
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_receive(pcb, "123456", 6u));
  TEST_ASSERT_EQUAL_INT(ERR_MEM, lwip_fake_tcp_receive(pcb, "789", 3u));
  TEST_ASSERT_EQUAL_size_t(6u, jh_lwip_tcp_socket_available(&socket));
  TEST_ASSERT_EQUAL_size_t(1u, lwip_fake_pbuf_count());
  jh_lwip_tcp_socket_close(&socket);
}

void test_tcp_listener_bind_and_listen_failures_are_recoverable(void) {
  jh_lwip_tcp_listener_t listener;
  const ip4_addr_t local = ipv4(192u, 0u, 2u, 10u);
  jh_lwip_tcp_listener_init(&listener);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_lwip_tcp_listener_bind(&listener, &local, 0u));

  lwip_fake_tcp_set_bind_status(ERR_USE);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        jh_lwip_tcp_listener_bind(&listener, &local, 8080u));
  TEST_ASSERT_NULL(listener.pcb);

  lwip_fake_tcp_set_bind_status(ERR_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_tcp_listener_bind(&listener, &local, 8080u));
  tcp_pcb *bound_pcb = listener.pcb;
  TEST_ASSERT_EQUAL_UINT16(8080u, bound_pcb->local_port);
  TEST_ASSERT_EQUAL_UINT32(local.addr, ip_2_ip4(&bound_pcb->local_ip)->addr);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_lwip_tcp_listener_listen(&listener, 0u));

  lwip_fake_tcp_fail_next_listen();
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, jh_lwip_tcp_listener_listen(&listener, 2u));
  TEST_ASSERT_EQUAL_PTR(bound_pcb, listener.pcb);
  TEST_ASSERT_TRUE(listener.bound);
  TEST_ASSERT_FALSE(listener.listening);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lwip_tcp_listener_listen(&listener, 9u));
  TEST_ASSERT_TRUE(listener.listening);
  TEST_ASSERT_EQUAL_UINT8(HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH, listener.backlog);
  TEST_ASSERT_EQUAL_UINT8(HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH,
                          listener.pcb->backlog);
  jh_lwip_tcp_listener_close(&listener);
}

void test_tcp_listener_accepts_fifo_and_preserves_early_data_and_fin(void) {
  jh_lwip_tcp_listener_t listener;
  tcp_pcb *listen_pcb = open_tcp_listener(&listener, 8080u, 2u);
  TEST_ASSERT_NOT_NULL(listen_pcb);
  const ip4_addr_t first_address = ipv4(192u, 0u, 2u, 21u);
  const ip4_addr_t second_address = ipv4(192u, 0u, 2u, 22u);
  tcp_pcb *first = nullptr;
  tcp_pcb *second = nullptr;
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_incoming(
                                    listen_pcb, &first_address, 5001u, &first));
  TEST_ASSERT_EQUAL_INT(
      ERR_OK,
      lwip_fake_tcp_incoming(listen_pcb, &second_address, 5002u, &second));
  TEST_ASSERT_EQUAL_size_t(2u, lwip_fake_tcp_backlog_delayed_count());
  TEST_ASSERT_TRUE(jh_lwip_tcp_listener_can_accept(&listener));

  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_receive(first, "early", 5u));
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_fin(first));
  jh_lwip_tcp_socket_t accepted_first;
  jh_lwip_tcp_socket_init(&accepted_first);
  ip4_addr_t remote = {};
  uint16_t remote_port = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_tcp_listener_accept(&listener, &accepted_first,
                                                    &remote, &remote_port));
  TEST_ASSERT_EQUAL_UINT32(first_address.addr, remote.addr);
  TEST_ASSERT_EQUAL_UINT16(5001u, remote_port);
  TEST_ASSERT_EQUAL_size_t(5u, jh_lwip_tcp_socket_available(&accepted_first));
  TEST_ASSERT_TRUE(jh_lwip_tcp_socket_is_connected(&accepted_first));
  TEST_ASSERT_EQUAL_PTR(&accepted_first, first->callback_argument);

  char buffer[6] = {};
  size_t received = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lwip_tcp_socket_receive(
                                    &accepted_first, buffer, 5u, &received));
  TEST_ASSERT_EQUAL_MEMORY("early", buffer, 5u);
  TEST_ASSERT_FALSE(jh_lwip_tcp_socket_is_connected(&accepted_first));

  jh_lwip_tcp_socket_t accepted_second;
  jh_lwip_tcp_socket_init(&accepted_second);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_tcp_listener_accept(&listener, &accepted_second,
                                                    &remote, &remote_port));
  TEST_ASSERT_EQUAL_UINT32(second_address.addr, remote.addr);
  TEST_ASSERT_EQUAL_UINT16(5002u, remote_port);
  TEST_ASSERT_EQUAL_PTR(&accepted_second, second->callback_argument);
  TEST_ASSERT_EQUAL_size_t(2u, lwip_fake_tcp_backlog_accepted_count());
  TEST_ASSERT_FALSE(jh_lwip_tcp_listener_can_accept(&listener));

  jh_lwip_tcp_socket_close(&accepted_first);
  jh_lwip_tcp_socket_close(&accepted_second);
  jh_lwip_tcp_listener_close(&listener);
}

void test_tcp_listener_full_queue_aborts_newest_connection(void) {
  jh_lwip_tcp_listener_t listener;
  tcp_pcb *listen_pcb = open_tcp_listener(&listener, 8080u, 2u);
  TEST_ASSERT_NOT_NULL(listen_pcb);
  tcp_pcb *clients[3] = {};
  for (uint8_t index = 0u; index < 2u; ++index) {
    const ip4_addr_t remote = ipv4(198u, 51u, 100u, (uint8_t)(10u + index));
    TEST_ASSERT_EQUAL_INT(ERR_OK,
                          lwip_fake_tcp_incoming(listen_pcb, &remote,
                                                 (uint16_t)(6000u + index),
                                                 &clients[index]));
  }
  const ip4_addr_t rejected_address = ipv4(198u, 51u, 100u, 12u);
  TEST_ASSERT_EQUAL_INT(ERR_ABRT,
                        lwip_fake_tcp_incoming(listen_pcb, &rejected_address,
                                               6002u, &clients[2]));
  TEST_ASSERT_TRUE(clients[2]->aborted);
  TEST_ASSERT_EQUAL_size_t(2u, listener.pending_count);
  TEST_ASSERT_EQUAL_size_t(1u, lwip_fake_tcp_abort_count());

  jh_lwip_tcp_listener_close(&listener);
  TEST_ASSERT_TRUE(clients[0]->aborted);
  TEST_ASSERT_TRUE(clients[1]->aborted);
  TEST_ASSERT_EQUAL_size_t(2u, lwip_fake_tcp_backlog_accepted_count());
  TEST_ASSERT_EQUAL_size_t(3u, lwip_fake_tcp_abort_count());
}

void test_tcp_listener_discards_reset_pending_connection(void) {
  jh_lwip_tcp_listener_t listener;
  tcp_pcb *listen_pcb = open_tcp_listener(&listener, 8080u, 2u);
  TEST_ASSERT_NOT_NULL(listen_pcb);
  const ip4_addr_t first_address = ipv4(203u, 0u, 113u, 31u);
  const ip4_addr_t second_address = ipv4(203u, 0u, 113u, 32u);
  tcp_pcb *first = nullptr;
  tcp_pcb *second = nullptr;
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_incoming(
                                    listen_pcb, &first_address, 7001u, &first));
  TEST_ASSERT_EQUAL_INT(
      ERR_OK,
      lwip_fake_tcp_incoming(listen_pcb, &second_address, 7002u, &second));
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_receive(first, "lost", 4u));
  TEST_ASSERT_EQUAL_size_t(1u, lwip_fake_pbuf_count());
  lwip_fake_tcp_error(first, ERR_RST);

  jh_lwip_tcp_socket_t accepted;
  jh_lwip_tcp_socket_init(&accepted);
  ip4_addr_t remote = {};
  uint16_t remote_port = 0u;
  TEST_ASSERT_TRUE(jh_lwip_tcp_listener_can_accept(&listener));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      jh_lwip_tcp_listener_accept(&listener, &accepted, &remote, &remote_port));
  TEST_ASSERT_EQUAL_UINT32(second_address.addr, remote.addr);
  TEST_ASSERT_EQUAL_UINT16(7002u, remote_port);
  TEST_ASSERT_EQUAL_size_t(0u, lwip_fake_pbuf_count());
  TEST_ASSERT_EQUAL_size_t(2u, lwip_fake_tcp_backlog_accepted_count());

  jh_lwip_tcp_socket_close(&accepted);
  jh_lwip_tcp_listener_close(&listener);
}

void test_tcp_listener_close_releases_pending_callbacks_and_buffers(void) {
  jh_lwip_tcp_listener_t listener;
  tcp_pcb *listen_pcb = open_tcp_listener(&listener, 8080u, 2u);
  TEST_ASSERT_NOT_NULL(listen_pcb);
  const ip4_addr_t remote = ipv4(192u, 0u, 2u, 40u);
  tcp_pcb *first = nullptr;
  tcp_pcb *second = nullptr;
  TEST_ASSERT_EQUAL_INT(
      ERR_OK, lwip_fake_tcp_incoming(listen_pcb, &remote, 8001u, &first));
  TEST_ASSERT_EQUAL_INT(
      ERR_OK, lwip_fake_tcp_incoming(listen_pcb, &remote, 8002u, &second));
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_receive(first, "one", 3u));
  TEST_ASSERT_EQUAL_INT(ERR_OK, lwip_fake_tcp_receive(second, "two", 3u));
  TEST_ASSERT_EQUAL_size_t(2u, lwip_fake_pbuf_count());

  jh_lwip_tcp_listener_close(&listener);
  TEST_ASSERT_EQUAL_size_t(0u, lwip_fake_pbuf_count());
  TEST_ASSERT_NULL(first->callback_argument);
  TEST_ASSERT_NULL(first->receive);
  TEST_ASSERT_NULL(second->callback_argument);
  TEST_ASSERT_NULL(second->receive);
  TEST_ASSERT_NULL(listen_pcb->accept);
  TEST_ASSERT_EQUAL_INT(
      ERR_ARG, lwip_fake_tcp_incoming(listen_pcb, &remote, 8003u, nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_udp_bind_and_send_capture);
  RUN_TEST(test_udp_bind_failures_release_the_pcb);
  RUN_TEST(test_udp_receive_preserves_queue_order_and_source);
  RUN_TEST(test_udp_partial_read_discard_and_zero_length_packet);
  RUN_TEST(test_udp_full_queue_drops_newest_packet);
  RUN_TEST(test_udp_packet_builder_overflow_and_close_cleanup);
  RUN_TEST(test_tcp_connect_success_and_failure_states);
  RUN_TEST(test_tcp_partial_send_and_output_error);
  RUN_TEST(test_tcp_send_backpressure_preserves_connection_and_allows_retry);
  RUN_TEST(test_tcp_fragmented_receive_and_window_acknowledgement);
  RUN_TEST(test_tcp_fin_keeps_buffered_data_readable);
  RUN_TEST(test_tcp_error_callback_invalidates_pcb);
  RUN_TEST(test_tcp_close_aborts_when_graceful_close_has_no_memory);
  RUN_TEST(test_tcp_receive_limit_applies_backpressure_without_ownership_leak);
  RUN_TEST(test_tcp_listener_bind_and_listen_failures_are_recoverable);
  RUN_TEST(test_tcp_listener_accepts_fifo_and_preserves_early_data_and_fin);
  RUN_TEST(test_tcp_listener_full_queue_aborts_newest_connection);
  RUN_TEST(test_tcp_listener_discards_reset_pending_connection);
  RUN_TEST(test_tcp_listener_close_releases_pending_callbacks_and_buffers);
  return UNITY_END();
}
