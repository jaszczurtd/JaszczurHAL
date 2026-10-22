/**
 * @file basic_suite.cpp
 * @brief Basic suite: WiFi lifecycle, resolver and UDP/TCP against the host
 *        fixture; tasks_suite.cpp and servers_suite.cpp add the two-task and
 *        server parts.
 */
#include "net_test_support.h"

#if !defined(JH_NET_TEST_SERVICES)

#include <hal/network/hal_udp.h>
#include <hal/system/hal_system.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifndef JH_NET_TEST_DNS_NAME
#define JH_NET_TEST_DNS_NAME "example.com"
#endif

namespace {

constexpr uint16_t kUdpEchoPort = 18190u;
constexpr uint16_t kTcpEchoPort = 18191u;
constexpr uint16_t kTcpSinkPort = 18192u;
constexpr uint16_t kTcpClosePort = 18193u;
constexpr uint16_t kTcpClosedPort = 18194u;
constexpr uint16_t kLocalUdpPort = 18197u;

uint8_t s_chunk[4096];

void check_scan(void) {
  int count = 0;
  const hal_status_t status = hal_wifi_scan_networks_ex(&count);
  bool seen = false;
  for (int index = 0; status == HAL_OK && index < count; ++index) {
    hal_wifi_scan_result_t result = {};
    if (hal_wifi_get_scan_result_ex(static_cast<size_t>(index), &result) ==
            HAL_OK &&
        strcmp(result.ssid, JH_NET_TEST_SSID) == 0) {
      seen = true;
    }
  }
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "networks=%d target=%u", count,
                 seen ? 1u : 0u);
  net_test_report("scan", status == HAL_OK && count > 0 && seen, status, extra);
}

void check_failed_join(const char *check, const char *ssid,
                       const char *password) {
  hal_wifi_state_t state = HAL_WIFI_STATE_FAILED;
  const hal_status_t status = net_test_join(ssid, password, &state);
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "state=%s",
                 hal_wifi_state_to_string(state));
  net_test_report(check, status != HAL_OK && state != HAL_WIFI_STATE_CONNECTED,
                  status, extra);
  (void)hal_wifi_disconnect_ex(false);
}

void check_dns(void) {
  hal_net_endpoint_t results[4] = {};
  size_t count = 0u;
  const hal_status_t status = hal_net_resolve_ex(
      JH_NET_TEST_DNS_NAME, HAL_NET_AF_UNSPEC, results, 4u, &count);
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "name=%s count=%u", JH_NET_TEST_DNS_NAME,
                 (unsigned)count);
  net_test_report("dns", status == HAL_OK && count >= 1u, status, extra);
}

void check_ping(void) {
  int result = -1;
  const hal_status_t status =
      hal_wifi_ping_status_ex(JH_NET_TEST_HOST, 2000u, &result);
  char extra[24];
  (void)snprintf(extra, sizeof(extra), "result=%d", result);
  net_test_report("ping", status == HAL_OK && result >= 0, status, extra);
}

void check_udp(void) {
  hal_udp_socket_t socket = nullptr;
  hal_status_t status = hal_udp_socket_open_ex(&socket);
  const hal_net_endpoint_t local = net_test_any_ipv4(kLocalUdpPort);
  if (status == HAL_OK) {
    status = hal_udp_socket_bind_ex(socket, &local);
  }
  hal_net_endpoint_t remote = {};
  if (status == HAL_OK &&
      !net_test_endpoint(JH_NET_TEST_HOST, kUdpEchoPort, &remote)) {
    status = HAL_EINVAL;
  }
  size_t sent = 0u;
  if (status == HAL_OK) {
    status =
        hal_udp_socket_sendto_ex(socket, "JH-ESP-UDP", 10u, &remote, &sent);
  }
  char buffer[32] = {};
  hal_net_endpoint_t sender = {};
  size_t received = 0u;
  if (status == HAL_OK) {
    status = hal_udp_socket_recvfrom_ex(socket, buffer, sizeof(buffer) - 1u,
                                        &sender, kNetTestWaitMs, &received);
  }
  net_test_report("udp_echo",
                  status == HAL_OK && received == 10u &&
                      memcmp(buffer, "JH-ESP-UDP", 10u) == 0 &&
                      sender.port == kUdpEchoPort,
                  status, "");

  const uint32_t start = hal_millis();
  const hal_status_t timeout_status = hal_udp_socket_recvfrom_ex(
      socket, buffer, sizeof(buffer), &sender, 100u, &received);
  const uint32_t waited = hal_millis() - start;
  char extra[24];
  (void)snprintf(extra, sizeof(extra), "waited_ms=%" PRIu32, waited);
  net_test_report("udp_timeout",
                  timeout_status == HAL_ETIMEOUT && waited >= 80u &&
                      waited < 1000u,
                  timeout_status, extra);
  hal_udp_socket_close(socket);
}

void check_tcp_echo(void) {
  hal_tcp_socket_t socket = nullptr;
  hal_status_t status =
      net_test_connect(JH_NET_TEST_HOST, kTcpEchoPort, &socket);
  if (status == HAL_OK) {
    status = net_test_tcp_roundtrip(socket, "JH-ESP-TCP", 10u, kNetTestWaitMs);
  }
  net_test_report("tcp_echo", status == HAL_OK, status, "");
  hal_tcp_socket_close(socket);
}

void check_tcp_refused(void) {
  hal_tcp_socket_t socket = nullptr;
  const hal_status_t status =
      net_test_connect(JH_NET_TEST_HOST, kTcpClosedPort, &socket);
  net_test_report("tcp_refused", status == HAL_EIO, status, "");
  hal_tcp_socket_close(socket);
}

void check_tcp_peer_close(void) {
  hal_tcp_socket_t socket = nullptr;
  hal_status_t status =
      net_test_connect(JH_NET_TEST_HOST, kTcpClosePort, &socket);
  char byte = 0;
  size_t received = 1u;
  if (status == HAL_OK) {
    status =
        hal_tcp_socket_recv_ex(socket, &byte, 1u, kNetTestWaitMs, &received);
  }
  net_test_report("tcp_peer_close", status == HAL_OK && received == 0u, status,
                  "");
  hal_tcp_socket_close(socket);
}

// The host accepts but never reads: every send returns at once, with a
// partial count, until the buffers are full and the socket reports EAGAIN.
void check_tcp_backpressure(void) {
  hal_tcp_socket_t socket = nullptr;
  hal_status_t status =
      net_test_connect(JH_NET_TEST_HOST, kTcpSinkPort, &socket);
  size_t total = 0u;
  uint32_t slowest_ms = 0u;
  for (int attempt = 0; attempt < 4096 && status == HAL_OK; ++attempt) {
    size_t sent = 0u;
    const uint32_t start = hal_millis();
    status = hal_tcp_socket_send_ex(socket, s_chunk, sizeof(s_chunk), &sent);
    const uint32_t took = hal_millis() - start;
    slowest_ms = took > slowest_ms ? took : slowest_ms;
    total += sent;
  }
  const bool can_send = hal_tcp_socket_can_send(socket);
  char extra[64];
  (void)snprintf(extra, sizeof(extra), "bytes=%u slowest_ms=%" PRIu32,
                 (unsigned)total, slowest_ms);
  net_test_report("tcp_backpressure",
                  status == HAL_EAGAIN && total > 0u && slowest_ms < 500u &&
                      !can_send,
                  status, extra);
  hal_tcp_socket_close(socket);
}

// A disconnect invalidates open handles; after a new join sockets work again.
void check_stale_handle_after_reconnect(void) {
  hal_tcp_socket_t socket = nullptr;
  const hal_status_t connect_status =
      net_test_connect(JH_NET_TEST_HOST, kTcpEchoPort, &socket);
  const hal_status_t disconnect_status = hal_wifi_disconnect_ex(false);
  size_t sent = 0u;
  const hal_status_t stale_status =
      hal_tcp_socket_send_ex(socket, "X", 1u, &sent);
  hal_tcp_socket_close(socket);
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "disconnect=%s",
                 hal_status_to_string(disconnect_status));
  net_test_report("stale_handle",
                  connect_status == HAL_OK && stale_status != HAL_OK,
                  stale_status, extra);
  if (net_test_check_join("rejoin")) {
    check_tcp_echo();
  }
}

// Turning WiFi off with an open socket and starting again.
void check_wifi_off_and_reinit(void) {
  hal_udp_socket_t socket = nullptr;
  (void)hal_udp_socket_open_ex(&socket);
  const hal_status_t off_status = hal_wifi_set_mode_ex(HAL_WIFI_MODE_OFF);
  hal_net_endpoint_t remote = {};
  (void)net_test_endpoint(JH_NET_TEST_HOST, kUdpEchoPort, &remote);
  size_t sent = 0u;
  const hal_status_t stale_status =
      hal_udp_socket_sendto_ex(socket, "X", 1u, &remote, &sent);
  hal_udp_socket_close(socket);
  net_test_report("wifi_off", off_status == HAL_OK && stale_status != HAL_OK,
                  off_status, "");
  const hal_status_t on_status = hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA);
  net_test_report("wifi_on", on_status == HAL_OK, on_status, "");
  if (net_test_check_join("reinit_join")) {
    check_dns();
    check_udp();
  }
}

} // namespace

const char *net_test_ready_fields(void) {
  static char fields[160];
  (void)snprintf(fields, sizeof(fields),
                 "suite=basic udp_echo=%u tcp_echo=%u tcp_sink=%u "
                 "tcp_close=%u tcp_closed=%u",
                 (unsigned)kUdpEchoPort, (unsigned)kTcpEchoPort,
                 (unsigned)kTcpSinkPort, (unsigned)kTcpClosePort,
                 (unsigned)kTcpClosedPort);
  return fields;
}

void net_test_run_suite(void) {
  const hal_status_t mode_status = hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA);
  net_test_report("wifi_mode", mode_status == HAL_OK, mode_status, "");
  check_scan();
  check_failed_join("auth_failure", JH_NET_TEST_SSID, "jh-wrong-password-0");
  check_failed_join("no_network", "jh-absent-network-7f3c", "jh-password-0");
  if (net_test_check_join("join")) {
    check_dns();
    check_ping();
    check_udp();
    check_tcp_echo();
    check_tcp_refused();
    check_tcp_peer_close();
    check_tcp_backpressure();
    net_test_run_tasks();
    check_stale_handle_after_reconnect();
    check_wifi_off_and_reinit();
    net_test_run_servers();
  }
}

#endif /* !JH_NET_TEST_SERVICES */
