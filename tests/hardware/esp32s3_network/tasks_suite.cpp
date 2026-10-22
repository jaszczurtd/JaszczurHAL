/**
 * @file tasks_suite.cpp
 * @brief Basic suite, two-task part: sockets used from both application tasks
 *        at once, a WiFi teardown while the other task blocks in a receive,
 *        and the native BSD socket API.
 */
#include "net_test_support.h"

#if !defined(JH_NET_TEST_SERVICES)

#include <hal/network/hal_udp.h>
#include <hal/system/hal_system.h>

#include <atomic>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr uint16_t kUdpEchoPort = 18190u;
constexpr uint16_t kTcpEchoPort = 18191u;
constexpr uint16_t kTcpSinkPort = 18192u;
constexpr uint16_t kTask1UdpPort = 18198u;
constexpr uint16_t kBsdUdpPort = 18202u;
constexpr unsigned kRoundtrips = 100u;
constexpr uint32_t kBlockedWaitMs = 10000u;
constexpr uint32_t kTeardownDelayMs = 500u;

enum class Job : int { kNone, kUdpEcho, kBlockedUdp, kBlockedTcp, kUdpSoak };

std::atomic<int> s_job{static_cast<int>(Job::kNone)};
std::atomic<bool> s_job_done{false};
// Task1 result: status, completed roundtrips and how long the job took.
std::atomic<int> s_job_status{HAL_OK};
std::atomic<unsigned> s_job_count{0u};
std::atomic<uint32_t> s_job_elapsed_ms{0u};
// Soak: how long task1 runs and its counts.
std::atomic<uint32_t> s_soak_duration_ms{0u};
std::atomic<unsigned> s_soak_udp_ok{0u};
std::atomic<unsigned> s_soak_udp_errors{0u};

void finish_job(hal_status_t status, unsigned count, uint32_t started) {
  s_job_status.store(status);
  s_job_count.store(count);
  s_job_elapsed_ms.store(hal_millis() - started);
  s_job.store(static_cast<int>(Job::kNone));
  s_job_done.store(true);
}

void run_udp_echo_job(void) {
  const uint32_t started = hal_millis();
  hal_udp_socket_t socket = nullptr;
  hal_status_t status = hal_udp_socket_open_ex(&socket);
  const hal_net_endpoint_t local = net_test_any_ipv4(kTask1UdpPort);
  if (status == HAL_OK) {
    status = hal_udp_socket_bind_ex(socket, &local);
  }
  hal_net_endpoint_t remote = {};
  if (status == HAL_OK &&
      !net_test_endpoint(JH_NET_TEST_HOST, kUdpEchoPort, &remote)) {
    status = HAL_EINVAL;
  }
  unsigned done = 0u;
  for (; status == HAL_OK && done < kRoundtrips; ++done) {
    char payload[16];
    const int length = snprintf(payload, sizeof(payload), "JH-T1-%u", done);
    size_t sent = 0u;
    status = hal_udp_socket_sendto_ex(socket, payload, (size_t)length, &remote,
                                      &sent);
    char reply[16] = {};
    hal_net_endpoint_t sender = {};
    size_t received = 0u;
    if (status == HAL_OK) {
      status = hal_udp_socket_recvfrom_ex(socket, reply, sizeof(reply), &sender,
                                          kNetTestWaitMs, &received);
    }
    if (status == HAL_OK &&
        (received != (size_t)length || memcmp(reply, payload, received) != 0)) {
      status = HAL_EPROTO;
    }
  }
  hal_udp_socket_close(socket);
  finish_job(status, status == HAL_OK ? done : done - 1u, started);
}

// Block in a receive that only a teardown can end early.
void run_blocked_job(bool tcp) {
  hal_status_t status = HAL_OK;
  hal_udp_socket_t udp = nullptr;
  hal_tcp_socket_t socket = nullptr;
  if (tcp) {
    // The sink accepts and never sends.
    status = net_test_connect(JH_NET_TEST_HOST, kTcpSinkPort, &socket);
  } else {
    status = hal_udp_socket_open_ex(&udp);
    const hal_net_endpoint_t local = net_test_any_ipv4(kTask1UdpPort);
    if (status == HAL_OK) {
      status = hal_udp_socket_bind_ex(udp, &local);
    }
  }
  const uint32_t started = hal_millis();
  char buffer[8];
  size_t received = 0u;
  if (status == HAL_OK && tcp) {
    status = hal_tcp_socket_recv_ex(socket, buffer, sizeof(buffer),
                                    kBlockedWaitMs, &received);
  } else if (status == HAL_OK) {
    hal_net_endpoint_t sender = {};
    status = hal_udp_socket_recvfrom_ex(udp, buffer, sizeof(buffer), &sender,
                                        kBlockedWaitMs, &received);
  }
  hal_tcp_socket_close(socket);
  hal_udp_socket_close(udp);
  finish_job(status, 0u, started);
}

// UDP echo round trips on task1 until the soak deadline.
void run_udp_soak_job(void) {
  const uint32_t started = hal_millis();
  hal_udp_socket_t socket = nullptr;
  hal_status_t status = hal_udp_socket_open_ex(&socket);
  const hal_net_endpoint_t local = net_test_any_ipv4(kTask1UdpPort);
  if (status == HAL_OK) {
    status = hal_udp_socket_bind_ex(socket, &local);
  }
  hal_net_endpoint_t remote = {};
  if (status == HAL_OK &&
      !net_test_endpoint(JH_NET_TEST_HOST, kUdpEchoPort, &remote)) {
    status = HAL_EINVAL;
  }
  unsigned sequence = 0u;
  while (status == HAL_OK &&
         hal_millis() - started < s_soak_duration_ms.load()) {
    char payload[16];
    const int length =
        snprintf(payload, sizeof(payload), "JH-S1-%u", sequence++);
    size_t sent = 0u;
    hal_status_t exchange = hal_udp_socket_sendto_ex(
        socket, payload, (size_t)length, &remote, &sent);
    char reply[16] = {};
    hal_net_endpoint_t sender = {};
    size_t received = 0u;
    if (exchange == HAL_OK) {
      exchange = hal_udp_socket_recvfrom_ex(socket, reply, sizeof(reply),
                                            &sender, 1000u, &received);
    }
    if (exchange == HAL_OK &&
        (received != (size_t)length || memcmp(reply, payload, received) != 0)) {
      exchange = HAL_EPROTO;
    }
    (exchange == HAL_OK ? s_soak_udp_ok : s_soak_udp_errors).fetch_add(1u);
    hal_delay_ms(10u);
  }
  hal_udp_socket_close(socket);
  finish_job(status, 0u, started);
}

bool start_job(Job job) {
  s_job_done.store(false);
  s_job.store(static_cast<int>(job));
  return true;
}

bool wait_job(uint32_t timeout_ms) {
  const uint32_t start = hal_millis();
  while (!s_job_done.load() && hal_millis() - start < timeout_ms) {
    hal_delay_ms(5u);
  }
  return s_job_done.load();
}

// Task0 runs TCP echo roundtrips while task1 runs UDP echo roundtrips.
void check_concurrent_sockets(void) {
  start_job(Job::kUdpEcho);
  hal_tcp_socket_t socket = nullptr;
  hal_status_t status =
      net_test_connect(JH_NET_TEST_HOST, kTcpEchoPort, &socket);
  unsigned tcp_done = 0u;
  for (; status == HAL_OK && tcp_done < kRoundtrips; ++tcp_done) {
    char payload[16];
    const int length = snprintf(payload, sizeof(payload), "JH-T0-%u", tcp_done);
    status =
        net_test_tcp_roundtrip(socket, payload, (size_t)length, kNetTestWaitMs);
  }
  hal_tcp_socket_close(socket);
  const bool finished = wait_job(60000u);
  const hal_status_t udp_status =
      static_cast<hal_status_t>(s_job_status.load());
  char extra[80];
  (void)snprintf(extra, sizeof(extra), "tcp=%u/%u udp=%u/%u udp_status=%s",
                 status == HAL_OK ? tcp_done : tcp_done - 1u, kRoundtrips,
                 s_job_count.load(), kRoundtrips,
                 hal_status_to_string(udp_status));
  net_test_report("concurrent_sockets",
                  status == HAL_OK && finished && udp_status == HAL_OK &&
                      s_job_count.load() == kRoundtrips,
                  status, extra);
}

// WiFi goes down while task1 waits in a receive: the receive ends with an
// error well before its timeout, and a new join works afterwards.
void check_teardown_while_blocked(const char *check, Job job) {
  start_job(job);
  hal_delay_ms(kTeardownDelayMs);
  const uint32_t disconnect_at = hal_millis();
  const hal_status_t disconnect = hal_wifi_disconnect_ex(false);
  const bool finished = wait_job(kBlockedWaitMs + 5000u);
  const uint32_t after_disconnect = hal_millis() - disconnect_at;
  const hal_status_t status = static_cast<hal_status_t>(s_job_status.load());
  char extra[96];
  (void)snprintf(extra, sizeof(extra),
                 "disconnect=%s blocked_ms=%" PRIu32
                 " returned_after_ms=%" PRIu32,
                 hal_status_to_string(disconnect), s_job_elapsed_ms.load(),
                 after_disconnect);
  net_test_report(check,
                  finished && disconnect == HAL_OK && status != HAL_OK &&
                      status != HAL_ETIMEOUT && after_disconnect < 3000u,
                  status, extra);
  (void)net_test_check_join("teardown_rejoin");
}

// The native ESP-IDF BSD API over the same connection.
void check_bsd_sockets(void) {
  hal_net_endpoint_t host = {};
  const bool resolved = net_test_endpoint(JH_NET_TEST_HOST, 0u, &host);
  sockaddr_in remote = {};
  remote.sin_family = AF_INET;
  memcpy(&remote.sin_addr.s_addr, host.addr, 4u);

  remote.sin_port = htons(kTcpEchoPort);
  const int tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  timeval timeout = {3, 0};
  (void)setsockopt(tcp, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  bool tcp_ok = resolved && tcp >= 0 &&
                connect(tcp, reinterpret_cast<sockaddr *>(&remote),
                        sizeof(remote)) == 0 &&
                send(tcp, "JH-BSD-TCP", 10, 0) == 10;
  char buffer[16] = {};
  size_t total = 0u;
  while (tcp_ok && total < 10u) {
    const ssize_t received = recv(tcp, buffer + total, 10u - total, 0);
    tcp_ok = received > 0;
    total += tcp_ok ? (size_t)received : 0u;
  }
  tcp_ok = tcp_ok && memcmp(buffer, "JH-BSD-TCP", 10u) == 0;
  if (tcp >= 0) {
    close(tcp);
  }
  net_test_report("bsd_tcp_echo", tcp_ok, tcp_ok ? HAL_OK : HAL_EIO, "");

  remote.sin_port = htons(kUdpEchoPort);
  const int udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in local = {};
  local.sin_family = AF_INET;
  local.sin_port = htons(kBsdUdpPort);
  (void)setsockopt(udp, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in sender = {};
  socklen_t sender_length = sizeof(sender);
  memset(buffer, 0, sizeof(buffer));
  const bool udp_ok =
      resolved && udp >= 0 &&
      bind(udp, reinterpret_cast<sockaddr *>(&local), sizeof(local)) == 0 &&
      sendto(udp, "JH-BSD-UDP", 10, 0, reinterpret_cast<sockaddr *>(&remote),
             sizeof(remote)) == 10 &&
      recvfrom(udp, buffer, sizeof(buffer), 0,
               reinterpret_cast<sockaddr *>(&sender), &sender_length) == 10 &&
      memcmp(buffer, "JH-BSD-UDP", 10u) == 0 &&
      sender.sin_port == htons(kUdpEchoPort);
  if (udp >= 0) {
    close(udp);
  }
  net_test_report("bsd_udp_echo", udp_ok, udp_ok ? HAL_OK : HAL_EIO, "");
}

} // namespace

// TCP echo on task0 over one connection, renewed after a failure, with a DNS
// lookup every ten seconds; task1 runs UDP echo at the same time.
void net_test_run_soak(uint32_t seconds) {
  s_soak_duration_ms.store(seconds * 1000u);
  s_soak_udp_ok.store(0u);
  s_soak_udp_errors.store(0u);
  NetTestSoak soak = {};
  net_test_soak_begin(&soak);
  start_job(Job::kUdpSoak);
  hal_tcp_socket_t socket = nullptr;
  unsigned reconnects = 0u;
  unsigned sequence = 0u;
  uint32_t last_dns_ms = soak.started_ms;
  while (hal_millis() - soak.started_ms < seconds * 1000u) {
    hal_status_t status = HAL_OK;
    if (socket == nullptr) {
      ++reconnects;
      status = net_test_connect(JH_NET_TEST_HOST, kTcpEchoPort, &socket);
    }
    if (status == HAL_OK) {
      char payload[24];
      const int length =
          snprintf(payload, sizeof(payload), "JH-S0-%u", sequence++);
      status = net_test_tcp_roundtrip(socket, payload, (size_t)length, 2000u);
    }
    if (status != HAL_OK) {
      hal_tcp_socket_close(socket);
      socket = nullptr;
    }
    net_test_soak_count(&soak, status);
    if (hal_millis() - last_dns_ms >= 10000u) {
      last_dns_ms = hal_millis();
      hal_net_endpoint_t result = {};
      net_test_soak_count(&soak, net_test_endpoint("example.com", 0u, &result)
                                     ? HAL_OK
                                     : HAL_EIO);
    }
    hal_delay_ms(10u);
  }
  hal_tcp_socket_close(socket);
  const bool finished = wait_job(5000u);
  soak.ok += s_soak_udp_ok.load();
  soak.errors += s_soak_udp_errors.load() + (finished ? 0u : 1u);
  char extra[64];
  (void)snprintf(extra, sizeof(extra), "udp_ok=%u udp_errors=%u connects=%u",
                 s_soak_udp_ok.load(), s_soak_udp_errors.load(), reconnects);
  net_test_soak_finish(&soak, extra);
}

void net_test_run_tasks(void) {
  check_concurrent_sockets();
  check_bsd_sockets();
  check_teardown_while_blocked("teardown_blocked_udp", Job::kBlockedUdp);
  check_teardown_while_blocked("teardown_blocked_tcp", Job::kBlockedTcp);
}

extern "C" void app_task1(void) {
  switch (static_cast<Job>(s_job.load())) {
  case Job::kUdpEcho:
    run_udp_echo_job();
    break;
  case Job::kBlockedUdp:
    run_blocked_job(false);
    break;
  case Job::kBlockedTcp:
    run_blocked_job(true);
    break;
  case Job::kUdpSoak:
    run_udp_soak_job();
    break;
  case Job::kNone:
    hal_delay_ms(5u);
    break;
  }
}

#endif /* !JH_NET_TEST_SERVICES */
