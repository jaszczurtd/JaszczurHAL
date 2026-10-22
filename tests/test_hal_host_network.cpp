// HAL_NETWORK_BACKEND_HOST over the operating system's loopback interface:
// TCP and UDP through the shared native socket core, the resolver, and the
// HTTP and WebSocket servers bound to 127.0.0.1. Real sockets and real time;
// no other host is contacted.
#include "hal/network/hal_net.h"
#include "hal/network/hal_tcp.h"
#include "hal/network/hal_udp.h"
#include "hal/network/hal_wifi.h"
#include "hal/network/http/hal_http_server.h"
#include "hal/network/native_sockets/jh_native_sockets.h"
#include "hal/network/websocket/hal_websocket.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

#include <atomic>
#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kWaitMs = 2000u;

// Tests bind fixed ports; start from a per-run offset and step past ports in
// use, which the backend reports as HAL_EBUSY. The range stays below the
// ephemeral ports of Linux (32768+) and Windows (49152+), which other
// programs receive for their outgoing connections at any moment.
constexpr uint16_t kFirstPort = 20000u;
constexpr uint16_t kPortCount = 12000u;
uint16_t s_next_port = 0u;

uint16_t next_port(void) {
  if (s_next_port == 0u) {
    const auto ticks = Clock::now().time_since_epoch().count();
    s_next_port = static_cast<uint16_t>(kFirstPort + (ticks % kPortCount));
  }
  const uint16_t port = s_next_port;
  s_next_port = static_cast<uint16_t>(
      port + 1u >= kFirstPort + kPortCount ? kFirstPort : port + 1u);
  return port;
}

hal_net_endpoint_t loopback(uint16_t port) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.addr[0] = 127u;
  endpoint.addr[3] = 1u;
  endpoint.port = port;
  return endpoint;
}

uint32_t elapsed_ms(Clock::time_point start) {
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                            start)
          .count());
}

// Open a listener on a free loopback port and return the port.
hal_tcp_listener_t open_listener(uint16_t *out_port) {
  hal_tcp_listener_t listener = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_listener_open_ex(&listener));
  for (int attempt = 0; attempt < 64; ++attempt) {
    const uint16_t port = next_port();
    const hal_net_endpoint_t local = loopback(port);
    const hal_status_t status = hal_tcp_listener_bind_ex(listener, &local);
    if (status == HAL_OK) {
      TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_listener_listen_ex(listener, 2u));
      *out_port = port;
      return listener;
    }
    TEST_ASSERT_EQUAL_INT(HAL_EBUSY, status);
    hal_tcp_listener_close(listener);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_listener_open_ex(&listener));
  }
  TEST_FAIL_MESSAGE("no free loopback port");
  return NULL;
}

// Bind @p socket to a free loopback port and return the port.
uint16_t bind_udp_socket(hal_udp_socket_t socket) {
  for (int attempt = 0; attempt < 64; ++attempt) {
    const hal_net_endpoint_t local = loopback(next_port());
    const hal_status_t status = hal_udp_socket_bind_ex(socket, &local);
    if (status == HAL_OK) {
      return local.port;
    }
    TEST_ASSERT_EQUAL_INT(HAL_EBUSY, status);
  }
  TEST_FAIL_MESSAGE("no free loopback port");
  return 0u;
}

// A port that refuses connections: bound once, then released.
uint16_t closed_port(void) {
  uint16_t port = 0u;
  hal_tcp_listener_t listener = open_listener(&port);
  hal_tcp_listener_close(listener);
  return port;
}

hal_tcp_socket_t connect_client(uint16_t port) {
  hal_tcp_socket_t client = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_socket_open_ex(&client));
  const hal_net_endpoint_t remote = loopback(port);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_tcp_socket_connect_ex(client, &remote, kWaitMs));
  return client;
}

void connected_pair(hal_tcp_listener_t *out_listener,
                    hal_tcp_socket_t *out_client,
                    hal_tcp_socket_t *out_server) {
  uint16_t port = 0u;
  *out_listener = open_listener(&port);
  *out_client = connect_client(port);
  hal_net_endpoint_t remote = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_tcp_listener_accept_ex(*out_listener, &remote, kWaitMs, out_server));
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET, remote.family);
  TEST_ASSERT_EQUAL_UINT8(127u, remote.addr[0]);
  TEST_ASSERT_NOT_EQUAL(0u, remote.port);
}

void send_all(hal_tcp_socket_t socket, const char *text) {
  const size_t length = strlen(text);
  size_t offset = 0u;
  const Clock::time_point start = Clock::now();
  while (offset < length) {
    size_t sent = 0u;
    const hal_status_t status =
        hal_tcp_socket_send_ex(socket, text + offset, length - offset, &sent);
    TEST_ASSERT_TRUE_MESSAGE(status == HAL_OK || status == HAL_EAGAIN,
                             hal_status_to_string(status));
    offset += sent;
    TEST_ASSERT_TRUE(elapsed_ms(start) < kWaitMs);
  }
}

// Read until @p expected appears or the wait ends; services the servers that
// are running while waiting.
bool receive_until(hal_tcp_socket_t socket, const char *expected, char *buffer,
                   size_t capacity, size_t *out_length) {
  size_t length = 0u;
  buffer[0] = '\0';
  const Clock::time_point start = Clock::now();
  while (elapsed_ms(start) < kWaitMs) {
    hal_http_server_poll();
    hal_websocket_server_poll();
    size_t received = 0u;
    const hal_status_t status = hal_tcp_socket_recv_ex(
        socket, buffer + length, capacity - 1u - length, 10u, &received);
    if (status == HAL_OK) {
      if (received == 0u) {
        break;
      }
      length += received;
      buffer[length] = '\0';
      if (strstr(buffer, expected) != NULL || length + 1u >= capacity) {
        break;
      }
    } else {
      TEST_ASSERT_TRUE_MESSAGE(status == HAL_ETIMEOUT || status == HAL_EAGAIN,
                               hal_status_to_string(status));
    }
  }
  if (out_length != NULL) {
    *out_length = length;
  }
  return strstr(buffer, expected) != NULL;
}

// Start a server on a free loopback port; returns the port.
uint16_t start_http_server(void) {
  for (int attempt = 0; attempt < 64; ++attempt) {
    const hal_net_endpoint_t local = loopback(next_port());
    const hal_status_t status = hal_http_server_start_ex(&local);
    if (status == HAL_OK) {
      return local.port;
    }
    TEST_ASSERT_EQUAL_INT(HAL_EBUSY, status);
  }
  TEST_FAIL_MESSAGE("no free loopback port");
  return 0u;
}

uint16_t start_websocket_server(const char *path) {
  for (int attempt = 0; attempt < 64; ++attempt) {
    const hal_net_endpoint_t local = loopback(next_port());
    const hal_status_t status = hal_websocket_server_start_ex(&local, path);
    if (status == HAL_OK) {
      return local.port;
    }
    TEST_ASSERT_EQUAL_INT(HAL_EBUSY, status);
  }
  TEST_FAIL_MESSAGE("no free loopback port");
  return 0u;
}

hal_status_t pong_handler(const hal_http_request_t *request,
                          hal_http_response_t *response, void *user) {
  (void)request;
  (void)user;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_http_response_set_content_type(response, "text/plain"));
  return hal_http_response_write_str(response, "pong");
}

} // namespace

void setUp(void) {}

void tearDown(void) {
  hal_http_server_stop();
  hal_http_server_clear_routes();
  hal_websocket_server_stop();
}

void test_host_backend_reports_os_link_and_dual_stack(void) {
  hal_net_capabilities_t capabilities = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_get_capabilities_ex(&capabilities));
  TEST_ASSERT_EQUAL_UINT32(HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6 |
                               HAL_NET_CAP_DUAL_STACK,
                           capabilities);
  // The operating system owns the link.
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA));
  hal_wifi_state_t state = HAL_WIFI_STATE_CONNECTED;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_wifi_get_state_ex(&state));
  TEST_ASSERT_FALSE(hal_wifi_is_connected());
}

void test_hal_time_follows_the_host_clock(void) {
  const Clock::time_point start = Clock::now();
  const uint32_t millis = hal_millis();
  const uint64_t micros = hal_micros64();
  hal_delay_ms(60u);
  const uint32_t slept = elapsed_ms(start);
  const uint32_t hal_elapsed = hal_millis() - millis;
  TEST_ASSERT_TRUE(slept >= 60u);
  TEST_ASSERT_TRUE(hal_elapsed >= 60u);
  TEST_ASSERT_TRUE(hal_elapsed <= slept + 5u);
  TEST_ASSERT_TRUE(hal_micros64() - micros >= UINT64_C(60000));
}

void test_http_server_closes_a_silent_client_on_real_time(void) {
  const uint16_t port = start_http_server();
  hal_tcp_socket_t client = connect_client(port);
  // The client sends nothing; the server's first-byte timeout (300 ms in this
  // build) runs on hal_millis(), so only polling time passes. The server
  // answers 408 and closes the connection.
  const Clock::time_point start = Clock::now();
  char response[256] = {};
  size_t length = 0u;
  bool closed = false;
  while (!closed && elapsed_ms(start) < kWaitMs) {
    hal_http_server_poll();
    size_t received = 0u;
    const hal_status_t status =
        hal_tcp_socket_recv_ex(client, response + length,
                               sizeof(response) - 1u - length, 10u, &received);
    if (status == HAL_OK) {
      closed = received == 0u;
      length += received;
    } else {
      TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_ETIMEOUT, status,
                                    hal_status_to_string(status));
    }
  }
  const uint32_t closed_after = elapsed_ms(start);
  TEST_ASSERT_TRUE(closed);
  TEST_ASSERT_EQUAL_INT(0, strncmp(response, "HTTP/1.1 408", 12));
  TEST_ASSERT_TRUE(closed_after >= 250u);
  TEST_ASSERT_TRUE(closed_after < kWaitMs);
  hal_tcp_socket_close(client);
}

void test_tcp_round_trip_over_loopback(void) {
  hal_tcp_listener_t listener = NULL;
  hal_tcp_socket_t client = NULL;
  hal_tcp_socket_t server = NULL;
  connected_pair(&listener, &client, &server);
  TEST_ASSERT_TRUE(hal_tcp_socket_is_connected(client));
  TEST_ASSERT_TRUE(hal_tcp_socket_is_connected(server));
  TEST_ASSERT_TRUE(hal_tcp_socket_can_send(client));

  send_all(client, "ping");
  char buffer[32];
  TEST_ASSERT_TRUE(receive_until(server, "ping", buffer, sizeof(buffer), NULL));
  send_all(server, "pong");
  TEST_ASSERT_TRUE(receive_until(client, "pong", buffer, sizeof(buffer), NULL));

  hal_tcp_socket_close(client);
  hal_tcp_socket_close(server);
  hal_tcp_listener_close(listener);
}

void test_tcp_receive_waits_for_its_timeout(void) {
  hal_tcp_listener_t listener = NULL;
  hal_tcp_socket_t client = NULL;
  hal_tcp_socket_t server = NULL;
  connected_pair(&listener, &client, &server);

  char byte = 0;
  size_t received = 1u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_tcp_socket_recv_ex(server, &byte, 1u, 0u, &received));
  TEST_ASSERT_FALSE(hal_tcp_socket_can_recv(server));
  const Clock::time_point start = Clock::now();
  TEST_ASSERT_EQUAL_INT(
      HAL_ETIMEOUT, hal_tcp_socket_recv_ex(server, &byte, 1u, 100u, &received));
  const uint32_t waited = elapsed_ms(start);
  TEST_ASSERT_EQUAL_UINT(0u, received);
  TEST_ASSERT_TRUE(waited >= 80u);
  TEST_ASSERT_TRUE(waited < 1500u);

  hal_tcp_socket_close(client);
  hal_tcp_socket_close(server);
  hal_tcp_listener_close(listener);
}

void test_tcp_peer_close_reads_as_end_of_stream(void) {
  hal_tcp_listener_t listener = NULL;
  hal_tcp_socket_t client = NULL;
  hal_tcp_socket_t server = NULL;
  connected_pair(&listener, &client, &server);

  hal_tcp_socket_close(client);
  char byte = 0;
  size_t received = 1u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tcp_socket_recv_ex(server, &byte, 1u, kWaitMs, &received));
  TEST_ASSERT_EQUAL_UINT(0u, received);
  TEST_ASSERT_TRUE(hal_tcp_socket_can_recv(server));

  // Writing to the closed peer fails with a status; the process gets no
  // SIGPIPE. The first writes may still be accepted before the reset arrives.
  hal_status_t status = HAL_OK;
  for (int attempt = 0; attempt < 100 && status == HAL_OK; ++attempt) {
    size_t sent = 0u;
    status = hal_tcp_socket_send_ex(server, "x", 1u, &sent);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  TEST_ASSERT_EQUAL_INT(HAL_EIO, status);

  hal_tcp_socket_close(server);
  hal_tcp_listener_close(listener);
}

void test_tcp_send_to_a_peer_that_stops_reading_does_not_block(void) {
  hal_tcp_listener_t listener = NULL;
  hal_tcp_socket_t client = NULL;
  hal_tcp_socket_t server = NULL;
  connected_pair(&listener, &client, &server);

  // The server never reads; the client's loop must keep running. Each call
  // returns at once, accepting part of the data until the buffers are full.
  static uint8_t chunk[64u * 1024u];
  hal_status_t status = HAL_OK;
  size_t total = 0u;
  for (int attempt = 0; attempt < 8192 && status == HAL_OK; ++attempt) {
    size_t sent = 0u;
    const Clock::time_point start = Clock::now();
    status = hal_tcp_socket_send_ex(client, chunk, sizeof(chunk), &sent);
    TEST_ASSERT_TRUE(elapsed_ms(start) < 500u);
    TEST_ASSERT_TRUE(sent <= sizeof(chunk));
    total += sent;
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_EAGAIN, status,
                                hal_status_to_string(status));
  TEST_ASSERT_TRUE(total > 0u);
  TEST_ASSERT_FALSE(hal_tcp_socket_can_send(client));

  hal_tcp_socket_close(client);
  hal_tcp_socket_close(server);
  hal_tcp_listener_close(listener);
}

void test_tcp_connect_to_closed_port_is_refused(void) {
  const uint16_t port = closed_port();
  hal_tcp_socket_t client = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_socket_open_ex(&client));
  const hal_net_endpoint_t remote = loopback(port);
  // Windows retries a refused loopback connection for about two seconds.
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        hal_tcp_socket_connect_ex(client, &remote, 10000u));
  TEST_ASSERT_FALSE(hal_tcp_socket_is_connected(client));
  hal_tcp_socket_close(client);
}

void test_tcp_listener_address_in_use_and_rebind_after_close(void) {
  uint16_t port = 0u;
  hal_tcp_listener_t listener = open_listener(&port);
  const hal_net_endpoint_t local = loopback(port);

  hal_tcp_listener_t busy = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_listener_open_ex(&busy));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_tcp_listener_bind_ex(busy, &local));
  hal_tcp_listener_close(busy);

  // A tool that restarts its server binds the same port again at once, while
  // the server side of a connection it closed first is still in TIME_WAIT.
  hal_tcp_socket_t client = connect_client(port);
  hal_tcp_socket_t server = NULL;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tcp_listener_accept_ex(listener, NULL, kWaitMs, &server));
  hal_tcp_socket_close(server);
  hal_tcp_socket_close(client);
  hal_tcp_listener_close(listener);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_listener_open_ex(&listener));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_listener_bind_ex(listener, &local));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_listener_listen_ex(listener, 2u));
  hal_tcp_listener_close(listener);
}

void test_udp_datagram_round_trip_reports_sender(void) {
  hal_udp_socket_t first = NULL;
  hal_udp_socket_t second = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&first));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&second));
  const uint16_t first_port = bind_udp_socket(first);
  const uint16_t second_port = bind_udp_socket(second);

  const hal_net_endpoint_t destination = loopback(second_port);
  size_t sent = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_sendto_ex(first, "probe", 5u,
                                                         &destination, &sent));
  TEST_ASSERT_EQUAL_UINT(5u, sent);

  char buffer[16] = {};
  hal_net_endpoint_t sender = {};
  size_t received = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_udp_socket_recvfrom_ex(second, buffer, sizeof(buffer),
                                         &sender, kWaitMs, &received));
  TEST_ASSERT_EQUAL_UINT(5u, received);
  TEST_ASSERT_EQUAL_MEMORY("probe", buffer, 5u);
  TEST_ASSERT_EQUAL_UINT16(first_port, sender.port);
  TEST_ASSERT_EQUAL_UINT8(127u, sender.addr[0]);

  TEST_ASSERT_EQUAL_INT(
      HAL_ETIMEOUT, hal_udp_socket_recvfrom_ex(second, buffer, sizeof(buffer),
                                               &sender, 50u, &received));

  // A datagram longer than the buffer arrives truncated and is consumed.
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_sendto_ex(first, "123456789", 9u,
                                                         &destination, &sent));
  char small[4] = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_udp_socket_recvfrom_ex(second, small, sizeof(small), &sender,
                                         kWaitMs, &received));
  TEST_ASSERT_EQUAL_UINT(sizeof(small), received);
  TEST_ASSERT_EQUAL_MEMORY("1234", small, sizeof(small));
  TEST_ASSERT_EQUAL_UINT16(first_port, sender.port);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        hal_udp_socket_recvfrom_ex(second, small, sizeof(small),
                                                   &sender, 50u, &received));
  hal_udp_socket_close(first);
  hal_udp_socket_close(second);
}

void test_resolver_handles_names_literals_and_capacity(void) {
  hal_net_endpoint_t results[8] = {};
  size_t count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_resolve_ex("localhost", HAL_NET_AF_INET,
                                                   results, 8u, &count));
  TEST_ASSERT_TRUE(count >= 1u);
  const uint8_t loopback_address[HAL_NET_IPV4_ADDR_LEN] = {127u, 0u, 0u, 1u};
  bool found = false;
  for (size_t index = 0u; index < count; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET, results[index].family);
    TEST_ASSERT_EQUAL_UINT16(0u, results[index].port);
    found = found || memcmp(results[index].addr, loopback_address,
                            HAL_NET_IPV4_ADDR_LEN) == 0;
    // One entry per address, not one per socket type.
    for (size_t other = 0u; other < index; ++other) {
      TEST_ASSERT_TRUE(memcmp(results[index].addr, results[other].addr,
                              HAL_NET_IPV4_ADDR_LEN) != 0);
    }
  }
  TEST_ASSERT_TRUE(found);

  // Too small a result array reports the needed capacity.
  size_t needed = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      hal_net_resolve_ex("localhost", HAL_NET_AF_INET, NULL, 0u, &needed));
  TEST_ASSERT_EQUAL_UINT(count, needed);

  // IPv6 literals parse without a lookup on the host backend.
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_net_resolve_ex("::1", HAL_NET_AF_INET6, results, 1u, &count));
  TEST_ASSERT_EQUAL_UINT(1u, count);
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET6, results[0].family);
  TEST_ASSERT_EQUAL_UINT8(1u, results[0].addr[15]);

  // An unknown name is absent, or the resolver is offline; never success.
  const hal_status_t unknown = hal_net_resolve_ex(
      "jaszczurhal-host-test.invalid", HAL_NET_AF_UNSPEC, results, 8u, &count);
  TEST_ASSERT_TRUE_MESSAGE(unknown == HAL_ENOENT || unknown == HAL_EAGAIN,
                           hal_status_to_string(unknown));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_net_resolve_ex("", HAL_NET_AF_UNSPEC,
                                                       results, 8u, &count));
}

void test_http_server_answers_on_loopback_address(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_http_server_route(HAL_HTTP_METHOD_GET, "/ping", pong_handler, NULL));
  const uint16_t port = start_http_server();

  hal_tcp_socket_t client = connect_client(port);
  send_all(client, "GET /ping HTTP/1.1\r\nHost: local\r\n\r\n");
  char response[512];
  TEST_ASSERT_TRUE(
      receive_until(client, "pong", response, sizeof(response), NULL));
  TEST_ASSERT_EQUAL_INT(0, strncmp(response, "HTTP/1.1 200 OK\r\n", 17));
  hal_tcp_socket_close(client);
  hal_http_server_stop();

  // The port of a running listener is in use.
  hal_tcp_listener_t listener = NULL;
  uint16_t busy_port = 0u;
  listener = open_listener(&busy_port);
  const hal_net_endpoint_t busy = loopback(busy_port);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_http_server_start_ex(&busy));
  TEST_ASSERT_FALSE(hal_http_server_is_running());
  hal_tcp_listener_close(listener);
}

void test_websocket_broadcast_reaches_loopback_client(void) {
  const uint16_t port = start_websocket_server("/ws");

  hal_tcp_socket_t client = connect_client(port);
  send_all(client, "GET /ws HTTP/1.1\r\n"
                   "Host: local\r\n"
                   "Upgrade: websocket\r\n"
                   "Connection: Upgrade\r\n"
                   "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                   "Sec-WebSocket-Version: 13\r\n\r\n");
  char response[512];
  size_t length = 0u;
  TEST_ASSERT_TRUE(
      receive_until(client, "\r\n\r\n", response, sizeof(response), &length));
  TEST_ASSERT_NOT_NULL(strstr(response, "HTTP/1.1 101"));
  TEST_ASSERT_NOT_NULL(strstr(response, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
  TEST_ASSERT_EQUAL_UINT(1u, hal_websocket_client_count());

  size_t sent_count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_websocket_broadcast_text("hello", &sent_count));
  TEST_ASSERT_EQUAL_UINT(1u, sent_count);
  const char frame[] = {'\x81', '\x05', 'h', 'e', 'l', 'l', 'o'};
  uint8_t received[16] = {};
  size_t total = 0u;
  const Clock::time_point start = Clock::now();
  while (total < sizeof(frame) && elapsed_ms(start) < kWaitMs) {
    hal_websocket_server_poll();
    size_t chunk = 0u;
    const hal_status_t status = hal_tcp_socket_recv_ex(
        client, received + total, sizeof(received) - total, 10u, &chunk);
    TEST_ASSERT_TRUE_MESSAGE(status == HAL_OK || status == HAL_ETIMEOUT,
                             hal_status_to_string(status));
    total += chunk;
  }
  TEST_ASSERT_EQUAL_UINT(sizeof(frame), total);
  TEST_ASSERT_EQUAL_MEMORY(frame, received, sizeof(frame));
  hal_tcp_socket_close(client);
}

// A child process that lives for about two seconds; it inherits every
// inheritable handle of this process.
struct ChildProcess {
#if defined(_WIN32)
  PROCESS_INFORMATION info;
#else
  pid_t pid;
#endif
};

ChildProcess start_child(void) {
  ChildProcess child = {};
#if defined(_WIN32)
  STARTUPINFOA startup = {};
  startup.cb = sizeof(startup);
  char command[] = "cmd.exe /c ping -n 3 127.0.0.1 >nul";
  TEST_ASSERT_TRUE(CreateProcessA(NULL, command, NULL, NULL, TRUE,
                                  CREATE_NO_WINDOW, NULL, NULL, &startup,
                                  &child.info) != 0);
#else
  char program[] = "sleep";
  char seconds[] = "2";
  char *const argv[] = {program, seconds, NULL};
  TEST_ASSERT_EQUAL_INT(
      0, posix_spawnp(&child.pid, "sleep", NULL, NULL, argv, environ));
#endif
  return child;
}

void stop_child(ChildProcess *child) {
#if defined(_WIN32)
  (void)TerminateProcess(child->info.hProcess, 0);
  (void)WaitForSingleObject(child->info.hProcess, INFINITE);
  CloseHandle(child->info.hThread);
  CloseHandle(child->info.hProcess);
#else
  (void)kill(child->pid, SIGTERM);
  int status = 0;
  (void)waitpid(child->pid, &status, 0);
#endif
}

// A network teardown ends waits that other threads are blocked in, also
// where shutdown() does not wake the waiter (UDP), instead of leaving them
// blocked on a handle that is no longer valid.
void test_teardown_ends_blocked_waits_in_other_threads(void) {
  hal_udp_socket_t udp = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&udp));
  (void)bind_udp_socket(udp);
  hal_tcp_listener_t listener = NULL;
  hal_tcp_socket_t client = NULL;
  hal_tcp_socket_t server = NULL;
  connected_pair(&listener, &client, &server);

  constexpr int kWaits = 3;
  std::atomic<int> statuses[kWaits];
  std::atomic<uint32_t> returned_ms[kWaits];
  for (int index = 0; index < kWaits; ++index) {
    statuses[index].store(HAL_NONE);
    returned_ms[index].store(UINT32_MAX);
  }
  const auto start = Clock::now();
  std::thread waits[kWaits] = {
      std::thread([&] {
        char buffer[8];
        hal_net_endpoint_t sender = {};
        size_t received = 0u;
        statuses[0].store(
            hal_udp_socket_recvfrom_ex(udp, buffer, sizeof(buffer), &sender,
                                       HAL_NET_TIMEOUT_FOREVER, &received));
        returned_ms[0].store(elapsed_ms(start));
      }),
      std::thread([&] {
        char buffer[8];
        size_t received = 0u;
        statuses[1].store(hal_tcp_socket_recv_ex(server, buffer, sizeof(buffer),
                                                 HAL_NET_TIMEOUT_FOREVER,
                                                 &received));
        returned_ms[1].store(elapsed_ms(start));
      }),
      std::thread([&] {
        hal_net_endpoint_t remote = {};
        hal_tcp_socket_t accepted = NULL;
        statuses[2].store(hal_tcp_listener_accept_ex(
            listener, &remote, HAL_NET_TIMEOUT_FOREVER, &accepted));
        returned_ms[2].store(elapsed_ms(start));
      }),
  };
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  for (int index = 0; index < kWaits; ++index) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_NONE, statuses[index].load(),
                                  "a wait ended before the teardown");
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_native_sockets_shutdown_all());
  for (std::thread &wait : waits) {
    wait.join();
  }
  for (int index = 0; index < kWaits; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_ECANCELED, statuses[index].load());
    TEST_ASSERT_LESS_THAN_UINT32(1300u, returned_ms[index].load());
  }
  hal_tcp_socket_close(server);
  hal_tcp_socket_close(client);
  hal_tcp_listener_close(listener);
  hal_udp_socket_close(udp);
}

void test_sockets_are_not_inherited_by_child_processes(void) {
  uint16_t port = 0u;
  hal_tcp_listener_t listener = open_listener(&port);
  hal_tcp_socket_t client = connect_client(port);
  hal_tcp_socket_t server = NULL;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tcp_listener_accept_ex(listener, NULL, kWaitMs, &server));
  hal_udp_socket_t datagram = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&datagram));
  const uint16_t udp_port = bind_udp_socket(datagram);

  // A tool starts a child process while it serves; closing its sockets must
  // still release the ports and end the connection.
  ChildProcess child = start_child();
  hal_tcp_socket_close(server);
  hal_tcp_listener_close(listener);
  hal_udp_socket_close(datagram);

  char byte = 0;
  size_t received = 1u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tcp_socket_recv_ex(client, &byte, 1u, 1000u, &received));
  TEST_ASSERT_EQUAL_UINT(0u, received);

  const hal_net_endpoint_t tcp_local = loopback(port);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_listener_open_ex(&listener));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_listener_bind_ex(listener, &tcp_local));
  const hal_net_endpoint_t udp_local = loopback(udp_port);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&datagram));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_bind_ex(datagram, &udp_local));
  stop_child(&child);

  hal_udp_socket_close(datagram);
  hal_tcp_listener_close(listener);
  hal_tcp_socket_close(client);
}

int main(void) {
  // Keep every result line when a test crashes the process.
  (void)setvbuf(stdout, NULL, _IONBF, 0);
  UNITY_BEGIN();
  RUN_TEST(test_host_backend_reports_os_link_and_dual_stack);
  RUN_TEST(test_hal_time_follows_the_host_clock);
  RUN_TEST(test_http_server_closes_a_silent_client_on_real_time);
  RUN_TEST(test_tcp_round_trip_over_loopback);
  RUN_TEST(test_tcp_receive_waits_for_its_timeout);
  RUN_TEST(test_tcp_peer_close_reads_as_end_of_stream);
  RUN_TEST(test_tcp_send_to_a_peer_that_stops_reading_does_not_block);
  RUN_TEST(test_tcp_connect_to_closed_port_is_refused);
  RUN_TEST(test_tcp_listener_address_in_use_and_rebind_after_close);
  RUN_TEST(test_udp_datagram_round_trip_reports_sender);
  RUN_TEST(test_resolver_handles_names_literals_and_capacity);
  RUN_TEST(test_http_server_answers_on_loopback_address);
  RUN_TEST(test_websocket_broadcast_reaches_loopback_client);
  RUN_TEST(test_sockets_are_not_inherited_by_child_processes);
  // Last: the teardown invalidates every open handle.
  RUN_TEST(test_teardown_ends_blocked_waits_in_other_threads);
  return UNITY_END();
}
