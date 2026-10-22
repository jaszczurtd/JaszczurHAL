/**
 * @file services_suite.cpp
 * @brief SERVICES suite: NTP and the wall clock, the TLS client and its
 *        rejections, HTTP/HTTPS clients, MQTT/MQTTS and WireGuard against the
 *        host fixture.
 */
#include "net_test_support.h"

#if defined(JH_NET_TEST_SERVICES)

#include <hal/network/hal_udp.h>
#include <hal/network/http/hal_http_client.h>
#include <hal/network/mqtt/hal_mqtt.h>
#include <hal/network/tls/hal_tls.h>
#include <hal/network/wireguard/hal_wireguard.h>
#include <hal/system/hal_system.h>
#include <hal/time/hal_time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/opt.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#if !__has_include("net_test_tls.h")
#error                                                                         \
    "Run verify_network.py prepare-tls --host <host address> first (see README.md)"
#endif
#include "net_test_tls.h"

#if !defined(JH_NET_TEST_WG_PRIVATE_KEY) ||                                    \
    !defined(JH_NET_TEST_WG_PEER_PUBLIC_KEY)
#error                                                                         \
    "The SERVICES suite needs JH_NET_TEST_WG_PRIVATE_KEY and JH_NET_TEST_WG_PEER_PUBLIC_KEY in net_test_config.h"
#endif
#ifndef JH_NET_TEST_WG_LOCAL_IP
#define JH_NET_TEST_WG_LOCAL_IP "10.77.0.2"
#endif
#ifndef JH_NET_TEST_WG_PEER_IP
#define JH_NET_TEST_WG_PEER_IP "10.77.0.1"
#endif
#ifndef JH_NET_TEST_WG_PORT
#define JH_NET_TEST_WG_PORT 51821u
#endif

namespace {

constexpr uint16_t kTlsPort = 18443u;
constexpr uint16_t kTlsDropPort = 18445u;
constexpr uint16_t kTlsSilentPort = 18446u;
constexpr uint16_t kHttpPort = 18447u;
constexpr uint16_t kLanTcpEchoPort = 18448u;
constexpr uint16_t kMqttPort = 18883u;
constexpr uint16_t kMqttsPort = 18884u;
constexpr uint16_t kWgUdpEchoPort = 42045u;
constexpr uint16_t kWgTcpEchoPort = 42046u;
constexpr uint16_t kWgLocalUdpPort = 42047u;
constexpr const char *kUnreachableNtp = "192.0.2.1";
constexpr const char *kAbsentNtp = "jh-absent-ntp.invalid";
constexpr const char *kMqttTopic = "jh/esp32/net";
constexpr const char *kMqttPayload = "JH-ESP-MQTT";
constexpr const char *kTlsBody = "JH-ESP-TLS-OK";
constexpr uint64_t kDay = 86400u;

hal_tls_trust_anchor_storage_t s_anchor;
hal_tls_trust_anchor_storage_t s_other_anchor;
hal_tls_security_config_t s_security;
bool s_mqtt_received = false;
char s_response[1024];

// ── Wall clock ──────────────────────────────────────────────────────────────

hal_time_status_t wait_for_ntp(uint32_t timeout_ms) {
  hal_time_status_t status = {};
  const uint32_t start = hal_millis();
  do {
    (void)hal_time_unix(); // services the pending request
    (void)hal_time_get_status_ex(&status);
    if (status.ntp_state != HAL_TIME_NTP_IN_PROGRESS) {
      break;
    }
    hal_delay_ms(20u);
  } while (hal_millis() - start < timeout_ms);
  return status;
}

void report_ntp(const char *check, hal_status_t start_status,
                const hal_time_status_t &status, bool pass) {
  char extra[96];
  (void)snprintf(extra, sizeof(extra),
                 "state=%d last=%s valid=%u unix=%" PRIu64,
                 static_cast<int>(status.ntp_state),
                 hal_status_to_string(status.last_ntp_status),
                 status.valid ? 1u : 0u, status.unix_time);
  net_test_report(check, pass, start_status, extra);
}

// A primary that never answers fails the request and leaves the clock unset.
void check_ntp_unreachable(void) {
  const hal_status_t start = hal_time_sync_ntp_ex(kUnreachableNtp, nullptr);
  const hal_time_status_t status = wait_for_ntp(15000u);
  report_ntp("ntp_unreachable", start, status,
             start == HAL_OK && status.ntp_state == HAL_TIME_NTP_FAILED &&
                 !status.valid);
}

// A primary name that does not resolve falls back to the secondary server.
bool check_ntp_fallback(void) {
  const hal_status_t start = hal_time_sync_ntp_ex(kAbsentNtp, JH_NET_TEST_HOST);
  const hal_time_status_t status = wait_for_ntp(20000u);
  const bool pass = start == HAL_OK &&
                    status.ntp_state == HAL_TIME_NTP_SYNCHRONIZED &&
                    status.valid && status.source == HAL_TIME_SOURCE_NTP &&
                    status.unix_time > JH_NET_TEST_TLS_NOT_BEFORE;
  report_ntp("ntp_fallback", start, status, pass);
  return pass;
}

// libc time follows the HAL wall clock once NTP set it.
void check_libc_time(void) {
  const uint64_t hal_now = hal_time_unix();
  timeval now = {};
  const int result = gettimeofday(&now, nullptr);
  const time_t libc_now = time(nullptr);
  const int64_t drift =
      static_cast<int64_t>(libc_now) - static_cast<int64_t>(hal_now);
  char extra[64];
  (void)snprintf(extra, sizeof(extra), "hal=%" PRIu64 " libc=%lld", hal_now,
                 static_cast<long long>(libc_now));
  net_test_report("libc_time",
                  result == 0 && drift >= -1 && drift <= 1 &&
                      static_cast<int64_t>(now.tv_sec) - libc_now <= 1,
                  result == 0 ? HAL_OK : HAL_EIO, extra);
}

// ── TLS client ──────────────────────────────────────────────────────────────

struct TlsCase {
  const char *hostname;
  uint16_t port;
  const hal_tls_trust_anchor_t *anchor;
  uint64_t fixed_time; // 0 reads the wall clock
  bool entropy_failure;
  bool cancelled;
  uint32_t operation_timeout_ms; // 0 keeps the default
  bool exchange;
};

hal_status_t case_time(void *context, uint64_t *out_seconds) {
  const TlsCase *tls_case = static_cast<const TlsCase *>(context);
  if (tls_case->fixed_time == 0u) {
    return hal_tls_default_time(nullptr, out_seconds);
  }
  *out_seconds = tls_case->fixed_time;
  return HAL_OK;
}

hal_status_t case_entropy(void *context, void *buffer, size_t length) {
  return static_cast<const TlsCase *>(context)->entropy_failure
             ? HAL_EHW
             : hal_tls_default_entropy(nullptr, buffer, length);
}

bool case_cancelled(void *context) {
  return static_cast<const TlsCase *>(context)->cancelled;
}

hal_status_t wait_for_tls_state(hal_tls_client_t client, hal_tls_state_t wanted,
                                uint32_t timeout_ms) {
  const uint32_t start = hal_millis();
  while (hal_millis() - start < timeout_ms) {
    hal_tls_state_t state = HAL_TLS_STATE_FAILED;
    hal_status_t status = hal_tls_client_get_state_ex(client, &state);
    if (status == HAL_OK && state == HAL_TLS_STATE_FAILED) {
      hal_status_t last = HAL_EIO;
      int32_t provider = 0;
      (void)hal_tls_client_get_last_error_ex(client, &last, &provider);
      return last;
    }
    if (status != HAL_OK || state == wanted) {
      return status;
    }
    status = hal_tls_client_poll_ex(client);
    if (status != HAL_OK && status != HAL_EAGAIN) {
      return status;
    }
    hal_delay_ms(1u);
  }
  return HAL_ETIMEOUT;
}

hal_status_t tls_exchange(hal_tls_client_t client, const char *hostname) {
  char request[128];
  const int length = snprintf(
      request, sizeof(request),
      "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", hostname);
  hal_status_t status =
      length > 0 && static_cast<size_t>(length) < sizeof(request)
          ? HAL_OK
          : HAL_EOVERFLOW;
  size_t offset = 0u;
  const uint32_t start = hal_millis();
  while (status == HAL_OK && offset < static_cast<size_t>(length)) {
    size_t written = 0u;
    status =
        hal_tls_client_write_ex(client, request + offset,
                                static_cast<size_t>(length) - offset, &written);
    if (status == HAL_EAGAIN && hal_millis() - start < 10000u) {
      status = HAL_OK;
      hal_delay_ms(1u);
    }
    offset += written;
  }
  size_t total = 0u;
  s_response[0] = '\0';
  while (status == HAL_OK && strstr(s_response, kTlsBody) == nullptr &&
         total + 1u < sizeof(s_response)) {
    if (hal_millis() - start >= 10000u) {
      return HAL_ETIMEOUT;
    }
    size_t received = 0u;
    status = hal_tls_client_read_ex(client, s_response + total,
                                    sizeof(s_response) - 1u - total, &received);
    if (status == HAL_EAGAIN) {
      status = HAL_OK;
      hal_delay_ms(1u);
    }
    total += received;
    s_response[total] = '\0';
  }
  return status == HAL_OK && strstr(s_response, kTlsBody) == nullptr
             ? HAL_EPROTO
             : status;
}

hal_status_t run_tls(TlsCase *tls_case, uint32_t *out_elapsed_ms) {
  const uint32_t start = hal_millis();
  hal_tls_client_config_t config = {};
  hal_status_t status = hal_tls_client_config_init(&config);
  if (tls_case->operation_timeout_ms != 0u) {
    config.operation_timeout_ms = tls_case->operation_timeout_ms;
  }
  hal_tls_client_t client = nullptr;
  if (status == HAL_OK) {
    status = hal_tls_client_create_ex(&config, &client);
  }
  hal_tls_security_config_t security = {};
  security.trust_anchors = tls_case->anchor;
  security.trust_anchor_count = 1u;
  security.get_time = case_time;
  security.get_entropy = case_entropy;
  security.is_cancelled = case_cancelled;
  security.callback_context = tls_case;
  if (status == HAL_OK) {
    status = hal_tls_client_configure_security_ex(client, &security);
  }
  if (status == HAL_OK) {
    status = hal_tls_client_configure_server_ex(client, tls_case->hostname,
                                                tls_case->port);
  }
  if (status == HAL_OK) {
    status = hal_tls_client_connect_ex(client);
  }
  if (status == HAL_EAGAIN) {
    status = wait_for_tls_state(client, HAL_TLS_STATE_CONNECTED, 30000u);
  }
  if (status == HAL_OK && tls_case->exchange) {
    status = tls_exchange(client, tls_case->hostname);
  }
  if (client != nullptr) {
    if (status == HAL_OK) {
      hal_status_t shutdown = hal_tls_client_shutdown_ex(client);
      if (shutdown == HAL_EAGAIN) {
        shutdown = wait_for_tls_state(client, HAL_TLS_STATE_CLOSED, 5000u);
      }
      // A peer that closes right after its response may end the stream
      // before close-notify; the authenticated exchange already passed.
      (void)shutdown;
    } else {
      (void)hal_tls_client_cancel_ex(client);
    }
    (void)hal_tls_client_close_ex(client);
  }
  *out_elapsed_ms = hal_millis() - start;
  return status;
}

TlsCase valid_case(void) {
  TlsCase tls_case = {};
  tls_case.hostname = JH_NET_TEST_TLS_HOSTNAME;
  tls_case.port = kTlsPort;
  tls_case.anchor = &s_anchor.anchor;
  return tls_case;
}

void check_tls(const char *check, TlsCase tls_case, hal_status_t expected,
               uint32_t max_elapsed_ms) {
  uint32_t elapsed = 0u;
  const hal_status_t status = run_tls(&tls_case, &elapsed);
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "elapsed_ms=%" PRIu32, elapsed);
  net_test_report(check, status == expected && elapsed <= max_elapsed_ms,
                  status, extra);
}

// A peer that drops the connection mid-handshake fails without a timeout.
void check_tls_transport_drop(void) {
  TlsCase tls_case = valid_case();
  tls_case.port = kTlsDropPort;
  uint32_t elapsed = 0u;
  const hal_status_t status = run_tls(&tls_case, &elapsed);
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "elapsed_ms=%" PRIu32, elapsed);
  net_test_report("tls_transport_drop",
                  status != HAL_OK && status != HAL_ETIMEOUT && elapsed < 5000u,
                  status, extra);
}

// Repeated sessions return their memory: after a warm-up session the free
// heap moves only by lwIP's own churn (closing PCBs), far below one session.
void check_tls_repeat(void) {
  constexpr unsigned kRounds = 8u;
  constexpr uint32_t kChurnBytes = 4096u;
  TlsCase tls_case = valid_case();
  tls_case.exchange = true;
  uint32_t elapsed = 0u;
  hal_status_t status = run_tls(&tls_case, &elapsed);
  const uint32_t heap_before = hal_get_free_heap();
  for (unsigned round = 0u; round < kRounds && status == HAL_OK; ++round) {
    status = run_tls(&tls_case, &elapsed);
  }
  const uint32_t heap_after = hal_get_free_heap();
  char extra[80];
  (void)snprintf(extra, sizeof(extra),
                 "rounds=%u heap_before=%" PRIu32 " heap_after=%" PRIu32,
                 kRounds, heap_before, heap_after);
  net_test_report("tls_repeat",
                  status == HAL_OK && heap_after + kChurnBytes >= heap_before,
                  status, extra);
}

void check_tls_suite(void) {
  TlsCase tls_case = valid_case();
  tls_case.exchange = true;
  check_tls("tls_valid", tls_case, HAL_OK, 30000u);
  check_tls_repeat();

  tls_case = valid_case();
  tls_case.hostname = "wrong." JH_NET_TEST_TLS_HOSTNAME;
  check_tls("tls_wrong_host", tls_case, HAL_EAUTH, 30000u);

  tls_case = valid_case();
  tls_case.anchor = &s_other_anchor.anchor;
  check_tls("tls_wrong_ca", tls_case, HAL_EAUTH, 30000u);

  tls_case = valid_case();
  tls_case.fixed_time = JH_NET_TEST_TLS_NOT_AFTER + kDay;
  check_tls("tls_expired", tls_case, HAL_EAUTH, 30000u);

  tls_case = valid_case();
  tls_case.fixed_time = JH_NET_TEST_TLS_NOT_BEFORE - kDay;
  check_tls("tls_not_yet_valid", tls_case, HAL_EAUTH, 30000u);

  tls_case = valid_case();
  tls_case.entropy_failure = true;
  check_tls("tls_entropy_failure", tls_case, HAL_EHW, 30000u);

  tls_case = valid_case();
  tls_case.cancelled = true;
  check_tls("tls_cancel", tls_case, HAL_ECANCELED, 5000u);

  check_tls_transport_drop();

  tls_case = valid_case();
  tls_case.port = kTlsSilentPort;
  tls_case.operation_timeout_ms = 2000u;
  check_tls("tls_timeout", tls_case, HAL_ETIMEOUT, 6000u);
}

// ── HTTP clients ────────────────────────────────────────────────────────────

void check_http_client(const char *check, hal_http_client_transport_t transport,
                       const char *host, uint16_t port) {
  hal_http_client_request_t request = {};
  hal_status_t status = hal_http_client_request_init(&request);
  request.transport = transport;
  request.host = host;
  request.port = port;
  request.path = "/";
  request.timeout_ms = 15000u;
  request.tls_security =
      transport == HAL_HTTP_CLIENT_TRANSPORT_TLS ? &s_security : nullptr;
  hal_http_client_response_t response = {};
  if (status == HAL_OK) {
    status = hal_http_client_perform_ex(&request, s_response,
                                        sizeof(s_response) - 1u, &response);
  }
  const size_t length = status == HAL_OK ? response.body_length : 0u;
  s_response[length] = '\0';
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "code=%u body=%u",
                 (unsigned)response.status_code, (unsigned)length);
  net_test_report(check,
                  status == HAL_OK && response.status_code == 200u &&
                      strstr(s_response, kTlsBody) != nullptr,
                  status, extra);
}

// ── MQTT ────────────────────────────────────────────────────────────────────

void mqtt_message(const char *topic, const uint8_t *payload,
                  uint16_t payload_len, void *user) {
  (void)user;
  s_mqtt_received = topic != nullptr && strcmp(topic, kMqttTopic) == 0 &&
                    payload_len == strlen(kMqttPayload) &&
                    memcmp(payload, kMqttPayload, payload_len) == 0;
}

// Connect, subscribe, publish and receive the broker's echo.
hal_status_t mqtt_roundtrip(const char *host, uint16_t port,
                            const char *client_id) {
  s_mqtt_received = false;
  hal_status_t status = hal_mqtt_set_server_ex(host, port);
  if (status == HAL_OK) {
    status = hal_mqtt_set_callback_ex(mqtt_message, nullptr);
  }
  if (status == HAL_OK) {
    status = hal_mqtt_set_socket_timeout_ex(10u);
  }
  if (status == HAL_OK) {
    status = hal_mqtt_connect_ex(client_id);
  }
  if (status == HAL_OK) {
    status = hal_mqtt_subscribe_ex(kMqttTopic, 0u);
  }
  if (status == HAL_OK) {
    status = hal_mqtt_publish_str_ex(kMqttTopic, kMqttPayload, false);
  }
  const uint32_t start = hal_millis();
  while (status == HAL_OK && !s_mqtt_received &&
         hal_millis() - start < 10000u) {
    status = hal_mqtt_loop_ex();
    hal_delay_ms(10u);
  }
  if (status == HAL_OK && !s_mqtt_received) {
    status = HAL_ETIMEOUT;
  }
  hal_mqtt_disconnect();
  return status;
}

void check_mqtt(const char *check, const char *host, uint16_t port,
                const char *client_id, bool expect_success) {
  const hal_status_t status = mqtt_roundtrip(host, port, client_id);
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "state=%d", hal_mqtt_state());
  net_test_report(check, (status == HAL_OK) == expect_success, status, extra);
}

void check_mqtt_suite(void) {
  check_mqtt("mqtt", JH_NET_TEST_HOST, kMqttPort, "jh-esp-mqtt", true);
  const hal_status_t tls_status = hal_mqtt_configure_tls_ex(&s_security);
  net_test_report("mqtts_configure", tls_status == HAL_OK, tls_status, "");
  if (tls_status == HAL_OK) {
    check_mqtt("mqtts", JH_NET_TEST_TLS_HOSTNAME, kMqttsPort, "jh-esp-mqtts",
               true);
    check_mqtt("mqtts_reconnect", JH_NET_TEST_TLS_HOSTNAME, kMqttsPort,
               "jh-esp-mqtts-again", true);
    check_mqtt("mqtts_wrong_host", "wrong." JH_NET_TEST_TLS_HOSTNAME,
               kMqttsPort, "jh-esp-mqtts-wrong", false);
  }
  const hal_status_t disable_status = hal_mqtt_disable_tls_ex();
  net_test_report("mqtts_disable", disable_status == HAL_OK, disable_status,
                  "");
  check_mqtt("mqtt_after_tls", JH_NET_TEST_HOST, kMqttPort, "jh-esp-mqtt-plain",
             true);
}

// ── WireGuard ───────────────────────────────────────────────────────────────

struct Tunnel {
  hal_status_t begin;
  bool peer_up;
  uint32_t handshake_ms;
};

Tunnel start_tunnel(bool full, const char *endpoint, uint32_t timeout_ms) {
  Tunnel tunnel = {};
  tunnel.begin =
      full ? hal_wireguard_begin_text_ex(
                 JH_NET_TEST_WG_LOCAL_IP, JH_NET_TEST_WG_PRIVATE_KEY, endpoint,
                 JH_NET_TEST_WG_PEER_PUBLIC_KEY, JH_NET_TEST_WG_PORT)
           : hal_wireguard_begin_advanced_text_ex(
                 JH_NET_TEST_WG_LOCAL_IP, JH_NET_TEST_WG_PRIVATE_KEY, endpoint,
                 JH_NET_TEST_WG_PEER_PUBLIC_KEY, JH_NET_TEST_WG_PORT,
                 "10.77.0.0", "255.255.255.0");
  const uint32_t start = hal_millis();
  while (tunnel.begin == HAL_OK && !tunnel.peer_up &&
         hal_millis() - start < timeout_ms) {
    (void)hal_wireguard_kick_handshake_text_ex(JH_NET_TEST_WG_PEER_IP,
                                               kWgUdpEchoPort, 1000u);
    (void)hal_wireguard_peer_up_quick_ex(&tunnel.peer_up);
    if (!tunnel.peer_up) {
      hal_delay_ms(50u);
    }
  }
  tunnel.handshake_ms = hal_millis() - start;
  return tunnel;
}

void report_tunnel(const char *check, const Tunnel &tunnel) {
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "peer_up=%u handshake_ms=%" PRIu32,
                 tunnel.peer_up ? 1u : 0u, tunnel.handshake_ms);
  net_test_report(check, tunnel.begin == HAL_OK && tunnel.peer_up, tunnel.begin,
                  extra);
}

hal_status_t udp_echo(const char *address, uint16_t port) {
  hal_udp_socket_t socket = nullptr;
  hal_status_t status = hal_udp_socket_open_ex(&socket);
  const hal_net_endpoint_t local = net_test_any_ipv4(kWgLocalUdpPort);
  if (status == HAL_OK) {
    status = hal_udp_socket_bind_ex(socket, &local);
  }
  hal_net_endpoint_t remote = {};
  if (status == HAL_OK && !net_test_endpoint(address, port, &remote)) {
    status = HAL_EINVAL;
  }
  size_t sent = 0u;
  if (status == HAL_OK) {
    status =
        hal_udp_socket_sendto_ex(socket, "JH-ESP-WG-UDP", 13u, &remote, &sent);
  }
  char buffer[32] = {};
  hal_net_endpoint_t sender = {};
  size_t received = 0u;
  if (status == HAL_OK) {
    status = hal_udp_socket_recvfrom_ex(socket, buffer, sizeof(buffer), &sender,
                                        kNetTestWaitMs, &received);
  }
  if (status == HAL_OK &&
      (received != 13u || memcmp(buffer, "JH-ESP-WG-UDP", 13u) != 0 ||
       sender.port != port ||
       memcmp(sender.addr, remote.addr, HAL_NET_IPV4_ADDR_LEN) != 0)) {
    status = HAL_EPROTO;
  }
  hal_udp_socket_close(socket);
  return status;
}

hal_status_t tcp_echo(const char *address, uint16_t port) {
  hal_tcp_socket_t socket = nullptr;
  hal_status_t status = net_test_connect(address, port, &socket);
  if (status == HAL_OK) {
    status = net_test_tcp_roundtrip(socket, "JH-ESP-WG-TCP", 13u, 5000u);
  }
  hal_tcp_socket_close(socket);
  return status;
}

void report_status(const char *check, hal_status_t status) {
  net_test_report(check, status == HAL_OK, status, "");
}

void end_tunnel(const char *check) {
  hal_wireguard_end();
  const bool down = !hal_wireguard_is_initialized();
  // After teardown the LAN path still works.
  const hal_status_t lan = tcp_echo(JH_NET_TEST_HOST, kLanTcpEchoPort);
  char extra[32];
  (void)snprintf(extra, sizeof(extra), "initialized=%u", down ? 0u : 1u);
  net_test_report(check, down && lan == HAL_OK, lan, extra);
}

// Without a valid wall clock no handshake can be timestamped.
void check_wireguard_without_time(void) {
  const Tunnel tunnel = start_tunnel(false, JH_NET_TEST_HOST, 3000u);
  hal_wireguard_end();
  char extra[32];
  (void)snprintf(extra, sizeof(extra), "peer_up=%u", tunnel.peer_up ? 1u : 0u);
  net_test_report("wg_no_time",
                  !tunnel.peer_up && !hal_wireguard_is_initialized(),
                  tunnel.begin, extra);
}

void check_wireguard_split(void) {
  const Tunnel tunnel = start_tunnel(false, JH_NET_TEST_HOST, 15000u);
  report_tunnel("wg_split_handshake", tunnel);
  if (tunnel.peer_up) {
    report_status("wg_split_udp",
                  udp_echo(JH_NET_TEST_WG_PEER_IP, kWgUdpEchoPort));
    report_status("wg_split_tcp",
                  tcp_echo(JH_NET_TEST_WG_PEER_IP, kWgTcpEchoPort));
    report_status("wg_split_mqtt", mqtt_roundtrip(JH_NET_TEST_WG_PEER_IP,
                                                  kMqttPort, "jh-esp-wg-mqtt"));
    // The split route leaves the LAN outside the tunnel.
    report_status("wg_split_lan", tcp_echo(JH_NET_TEST_HOST, kLanTcpEchoPort));
  }
  // Teardown with a TCP connection still open through the tunnel: removing
  // the interface aborts it and sends its RST through the tunnel device.
  hal_tcp_socket_t open_socket = nullptr;
  const hal_status_t open_status =
      tunnel.peer_up ? net_test_connect(JH_NET_TEST_WG_PEER_IP, kWgTcpEchoPort,
                                        &open_socket)
                     : HAL_ETIMEOUT;
  end_tunnel("wg_split_end");
  size_t sent = 0u;
  const hal_status_t after =
      hal_tcp_socket_send_ex(open_socket, "X", 1u, &sent);
  hal_tcp_socket_close(open_socket);
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "open=%s",
                 hal_status_to_string(open_status));
  net_test_report("wg_end_open_tcp", open_status == HAL_OK && after != HAL_OK,
                  after, extra);
}

void check_wireguard_cycles(void) {
  unsigned passed = 0u;
  hal_status_t last = HAL_OK;
  for (unsigned cycle = 0u; cycle < 3u; ++cycle) {
    const Tunnel tunnel = start_tunnel(false, JH_NET_TEST_HOST, 15000u);
    last = tunnel.peer_up
               ? udp_echo(JH_NET_TEST_WG_PEER_IP, kWgUdpEchoPort)
               : (tunnel.begin != HAL_OK ? tunnel.begin : HAL_ETIMEOUT);
    hal_wireguard_end();
    passed += last == HAL_OK && !hal_wireguard_is_initialized() ? 1u : 0u;
  }
  char extra[24];
  (void)snprintf(extra, sizeof(extra), "cycles=%u/3", passed);
  net_test_report("wg_cycles", passed == 3u, last, extra);
}

void check_wireguard_full(void) {
  const Tunnel tunnel = start_tunnel(true, JH_NET_TEST_HOST, 15000u);
  report_tunnel("wg_full_handshake", tunnel);
  if (tunnel.peer_up) {
    report_status("wg_full_udp",
                  udp_echo(JH_NET_TEST_WG_PEER_IP, kWgUdpEchoPort));
    report_status("wg_full_tcp",
                  tcp_echo(JH_NET_TEST_WG_PEER_IP, kWgTcpEchoPort));
  }
  end_tunnel("wg_full_end");
}

// The endpoint given as a name goes through the resolver.
void check_wireguard_hostname(void) {
  const Tunnel tunnel = start_tunnel(false, JH_NET_TEST_TLS_HOSTNAME, 15000u);
  report_tunnel("wg_hostname_handshake", tunnel);
  if (tunnel.peer_up) {
    report_status("wg_hostname_udp",
                  udp_echo(JH_NET_TEST_WG_PEER_IP, kWgUdpEchoPort));
  }
  hal_wireguard_end();
}

// Losing WiFi under an active tunnel, then a new join and a new tunnel.
void check_wireguard_underlay_loss(void) {
  Tunnel tunnel = start_tunnel(false, JH_NET_TEST_HOST, 15000u);
  const hal_status_t before =
      tunnel.peer_up ? udp_echo(JH_NET_TEST_WG_PEER_IP, kWgUdpEchoPort)
                     : HAL_ETIMEOUT;
  const hal_status_t leave = hal_wifi_disconnect_ex(false);
  const hal_status_t lost = udp_echo(JH_NET_TEST_WG_PEER_IP, kWgUdpEchoPort);
  hal_wireguard_end();
  char extra[64];
  (void)snprintf(extra, sizeof(extra), "before=%s leave=%s",
                 hal_status_to_string(before), hal_status_to_string(leave));
  net_test_report("wg_underlay_loss",
                  before == HAL_OK && leave == HAL_OK && lost != HAL_OK &&
                      !hal_wireguard_is_initialized(),
                  lost, extra);
  if (net_test_check_join("wg_rejoin")) {
    tunnel = start_tunnel(false, JH_NET_TEST_HOST, 15000u);
    report_tunnel("wg_recovered_handshake", tunnel);
    report_status("wg_recovered_udp",
                  tunnel.peer_up
                      ? udp_echo(JH_NET_TEST_WG_PEER_IP, kWgUdpEchoPort)
                      : HAL_ETIMEOUT);
    end_tunnel("wg_recovered_end");
  }
}

// lwIP's thread runs the tunnel's receive path, handshakes included: report
// how much of its stack the suite left unused.
void check_tcpip_stack(void) {
  TaskHandle_t task = xTaskGetHandle(TCPIP_THREAD_NAME);
  const unsigned unused =
      task != nullptr ? static_cast<unsigned>(uxTaskGetStackHighWaterMark(task))
                      : 0u;
  char extra[32];
  (void)snprintf(extra, sizeof(extra), "unused_bytes=%u", unused);
  net_test_report("tcpip_stack", unused >= 1024u,
                  task != nullptr ? HAL_OK : HAL_ENOENT, extra);
}

} // namespace

// Traffic through a split tunnel for the whole run: it spans several session
// rekeys (WireGuard renews keys every two minutes).
void net_test_run_soak(uint32_t seconds) {
  NetTestSoak soak = {};
  net_test_soak_begin(&soak);
  const Tunnel tunnel = start_tunnel(false, JH_NET_TEST_HOST, 15000u);
  hal_tcp_socket_t socket = nullptr;
  unsigned connects = 0u;
  unsigned sequence = 0u;
  while (tunnel.peer_up && hal_millis() - soak.started_ms < seconds * 1000u) {
    net_test_soak_count(&soak,
                        udp_echo(JH_NET_TEST_WG_PEER_IP, kWgUdpEchoPort));
    hal_status_t status = HAL_OK;
    if (socket == nullptr) {
      ++connects;
      status =
          net_test_connect(JH_NET_TEST_WG_PEER_IP, kWgTcpEchoPort, &socket);
    }
    if (status == HAL_OK) {
      char payload[24];
      const int length =
          snprintf(payload, sizeof(payload), "JH-WG-%u", sequence++);
      status = net_test_tcp_roundtrip(socket, payload, (size_t)length, 3000u);
    }
    if (status != HAL_OK) {
      hal_tcp_socket_close(socket);
      socket = nullptr;
    }
    net_test_soak_count(&soak, status);
    hal_delay_ms(50u);
  }
  hal_tcp_socket_close(socket);
  hal_wireguard_end();
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "peer_up=%u connects=%u",
                 tunnel.peer_up ? 1u : 0u, connects);
  net_test_soak_finish(&soak, extra);
}

namespace {

bool load_trust_anchors(void) {
  hal_status_t status = hal_tls_trust_anchor_from_der_ex(
      kNetTestTlsCa, sizeof(kNetTestTlsCa), &s_anchor);
  if (status == HAL_OK) {
    status = hal_tls_trust_anchor_from_der_ex(
        kNetTestTlsOtherCa, sizeof(kNetTestTlsOtherCa), &s_other_anchor);
  }
  s_security = {};
  s_security.trust_anchors = &s_anchor.anchor;
  s_security.trust_anchor_count = 1u;
  s_security.get_time = hal_tls_default_time;
  s_security.get_entropy = hal_tls_default_entropy;
  net_test_report("tls_anchors", status == HAL_OK, status, "");
  return status == HAL_OK;
}

} // namespace

const char *net_test_ready_fields(void) {
  static char fields[256];
  (void)snprintf(fields, sizeof(fields),
                 "suite=services tls=%u tls_drop=%u tls_silent=%u http=%u "
                 "lan_tcp_echo=%u mqtt=%u mqtts=%u wg_peer=%s wg_udp_echo=%u "
                 "wg_tcp_echo=%u",
                 (unsigned)kTlsPort, (unsigned)kTlsDropPort,
                 (unsigned)kTlsSilentPort, (unsigned)kHttpPort,
                 (unsigned)kLanTcpEchoPort, (unsigned)kMqttPort,
                 (unsigned)kMqttsPort, JH_NET_TEST_WG_PEER_IP,
                 (unsigned)kWgUdpEchoPort, (unsigned)kWgTcpEchoPort);
  return fields;
}

void net_test_run_suite(void) {
  const hal_status_t mode_status = hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA);
  net_test_report("wifi_mode", mode_status == HAL_OK, mode_status, "");
  if (!net_test_check_join("join") || !load_trust_anchors()) {
    return;
  }
  // Before any wall clock exists. The libc clock survives a software reset
  // (the RTC keeps counting), so clear it as a power-on would.
  const timeval epoch = {};
  (void)settimeofday(&epoch, nullptr);
  TlsCase no_time = valid_case();
  check_tls("tls_no_time", no_time, HAL_ECONFIG, 30000u);
  check_wireguard_without_time();
  check_ntp_unreachable();
  if (!check_ntp_fallback()) {
    return;
  }
  check_libc_time();
  check_tls_suite();
  check_http_client("https_client", HAL_HTTP_CLIENT_TRANSPORT_TLS,
                    JH_NET_TEST_TLS_HOSTNAME, kTlsPort);
  check_http_client("http_client", HAL_HTTP_CLIENT_TRANSPORT_PLAINTEXT,
                    JH_NET_TEST_HOST, kHttpPort);
  check_mqtt_suite();
  check_wireguard_split();
  check_wireguard_cycles();
  check_wireguard_full();
  check_wireguard_hostname();
  check_wireguard_underlay_loss();
  check_tcpip_stack();
}

#endif /* JH_NET_TEST_SERVICES */
