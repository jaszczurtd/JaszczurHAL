/**
 * @file app.cpp
 * @brief ESP32-S3 network hardware test: WiFi lifecycle, resolver, UDP/TCP
 *        and the HTTP and WebSocket servers against a host fixture.
 *
 * The firmware announces its ports, waits for `GO` on the debug serial port,
 * runs every check once and prints one `JH_ESP32_NET check=...` line per check
 * and a final `check=result` line. verify_network.py serves the host side.
 * The status LED blinks blue while the firmware waits for `GO`, stays blue
 * while the checks run, then turns green on PASS or red on FAIL.
 */
#include <hal/gpio/hal_rgb_led.h>
#include <hal/network/hal_net.h>
#include <hal/network/hal_tcp.h>
#include <hal/network/hal_udp.h>
#include <hal/network/hal_wifi.h>
#include <hal/network/http/hal_http_server.h>
#include <hal/network/websocket/hal_websocket.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#if !__has_include("net_test_config.h")
#error                                                                         \
    "Create net_test_config.h with JH_NET_TEST_SSID, JH_NET_TEST_PASSWORD and JH_NET_TEST_HOST (see README.md)"
#endif
#include "net_test_config.h"

#ifndef JH_NET_TEST_DNS_NAME
#define JH_NET_TEST_DNS_NAME "example.com"
#endif

namespace {

constexpr const char *kPrefix = "JH_ESP32_NET ";
constexpr uint16_t kUdpEchoPort = 18190u;
constexpr uint16_t kTcpEchoPort = 18191u;
constexpr uint16_t kTcpSinkPort = 18192u;
constexpr uint16_t kTcpClosePort = 18193u;
constexpr uint16_t kTcpClosedPort = 18194u;
constexpr uint16_t kHttpPort = 18195u;
constexpr uint16_t kWebSocketPort = 18196u;
constexpr uint16_t kLocalUdpPort = 18197u;
constexpr uint32_t kJoinTimeoutMs = 20000u;
constexpr uint32_t kWaitMs = 3000u;

char s_line[320];
char s_serial_line[32];
size_t s_serial_length = 0u;
unsigned s_passed = 0u;
unsigned s_failed = 0u;
char s_local_ip[20];
uint8_t s_chunk[4096];
unsigned s_http_hits = 0u;
unsigned s_ws_messages = 0u;
bool s_led_ready = false;

void set_led(hal_rgb_led_color_t color) {
  if (s_led_ready) {
    (void)hal_rgb_led_set_color(color);
  }
}

void report(const char *check, bool pass, hal_status_t status,
            const char *extra) {
  (void)snprintf(s_line, sizeof(s_line), "%scheck=%s pass=%u status=%s%s%s",
                 kPrefix, check, pass ? 1u : 0u, hal_status_to_string(status),
                 extra[0] != '\0' ? " " : "", extra);
  hal_serial_println(s_line);
  if (pass) {
    ++s_passed;
  } else {
    ++s_failed;
  }
}

// Returns the completed line, or nullptr while a line is still arriving.
const char *read_serial_line(void) {
  while (hal_serial_available() > 0) {
    const int value = hal_serial_read();
    if (value < 0) {
      break;
    }
    if (value == '\n' || value == '\r') {
      if (s_serial_length > 0u) {
        s_serial_line[s_serial_length] = '\0';
        s_serial_length = 0u;
        return s_serial_line;
      }
      continue;
    }
    if (s_serial_length + 1u < sizeof(s_serial_line)) {
      s_serial_line[s_serial_length++] = static_cast<char>(value);
    } else {
      s_serial_length = 0u;
    }
  }
  return nullptr;
}

bool parse_endpoint(const char *address, uint16_t port,
                    hal_net_endpoint_t *out) {
  size_t count = 0u;
  if (hal_net_resolve_ex(address, HAL_NET_AF_INET, out, 1u, &count) != HAL_OK ||
      count != 1u) {
    return false;
  }
  out->port = port;
  return true;
}

hal_net_endpoint_t any_ipv4(uint16_t port) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.port = port;
  return endpoint;
}

hal_status_t join(const char *ssid, const char *password,
                  hal_wifi_state_t *out_state) {
  (void)hal_wifi_set_timeout_ms_ex(kJoinTimeoutMs);
  const hal_status_t status = hal_wifi_begin_station_ex(ssid, password, false);
  *out_state = HAL_WIFI_STATE_FAILED;
  (void)hal_wifi_get_state_ex(out_state);
  return status;
}

hal_status_t connect_host(uint16_t port, hal_tcp_socket_t *out_socket) {
  hal_net_endpoint_t remote = {};
  if (!parse_endpoint(JH_NET_TEST_HOST, port, &remote)) {
    return HAL_EINVAL;
  }
  hal_status_t status = hal_tcp_socket_open_ex(out_socket);
  if (status == HAL_OK) {
    status = hal_tcp_socket_connect_ex(*out_socket, &remote, kWaitMs);
  }
  return status;
}

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
  report("scan", status == HAL_OK && count > 0 && seen, status, extra);
}

void check_failed_join(const char *check, const char *ssid,
                       const char *password) {
  hal_wifi_state_t state = HAL_WIFI_STATE_FAILED;
  const hal_status_t status = join(ssid, password, &state);
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "state=%s",
                 hal_wifi_state_to_string(state));
  report(check, status != HAL_OK && state != HAL_WIFI_STATE_CONNECTED, status,
         extra);
  (void)hal_wifi_disconnect_ex(false);
}

bool check_join(const char *check) {
  hal_wifi_state_t state = HAL_WIFI_STATE_FAILED;
  const hal_status_t status =
      join(JH_NET_TEST_SSID, JH_NET_TEST_PASSWORD, &state);
  s_local_ip[0] = '\0';
  (void)hal_wifi_get_local_ip_ex(s_local_ip, sizeof(s_local_ip));
  const bool pass = status == HAL_OK && state == HAL_WIFI_STATE_CONNECTED &&
                    s_local_ip[0] != '\0' && strcmp(s_local_ip, "0.0.0.0") != 0;
  char extra[64];
  (void)snprintf(extra, sizeof(extra), "state=%s ip=%s",
                 hal_wifi_state_to_string(state), s_local_ip);
  report(check, pass, status, extra);
  return pass;
}

void check_dns(void) {
  hal_net_endpoint_t results[4] = {};
  size_t count = 0u;
  const hal_status_t status = hal_net_resolve_ex(
      JH_NET_TEST_DNS_NAME, HAL_NET_AF_UNSPEC, results, 4u, &count);
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "name=%s count=%u", JH_NET_TEST_DNS_NAME,
                 (unsigned)count);
  report("dns", status == HAL_OK && count >= 1u, status, extra);
}

void check_ping(void) {
  int result = -1;
  const hal_status_t status =
      hal_wifi_ping_status_ex(JH_NET_TEST_HOST, 2000u, &result);
  char extra[24];
  (void)snprintf(extra, sizeof(extra), "result=%d", result);
  report("ping", status == HAL_OK && result >= 0, status, extra);
}

void check_udp(void) {
  hal_udp_socket_t socket = nullptr;
  hal_status_t status = hal_udp_socket_open_ex(&socket);
  const hal_net_endpoint_t local = any_ipv4(kLocalUdpPort);
  if (status == HAL_OK) {
    status = hal_udp_socket_bind_ex(socket, &local);
  }
  hal_net_endpoint_t remote = {};
  if (status == HAL_OK &&
      !parse_endpoint(JH_NET_TEST_HOST, kUdpEchoPort, &remote)) {
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
                                        &sender, kWaitMs, &received);
  }
  report("udp_echo",
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
  report("udp_timeout",
         timeout_status == HAL_ETIMEOUT && waited >= 80u && waited < 1000u,
         timeout_status, extra);
  hal_udp_socket_close(socket);
}

void check_tcp_echo(void) {
  hal_tcp_socket_t socket = nullptr;
  hal_status_t status = connect_host(kTcpEchoPort, &socket);
  size_t sent = 0u;
  if (status == HAL_OK) {
    status = hal_tcp_socket_send_ex(socket, "JH-ESP-TCP", 10u, &sent);
  }
  char buffer[32] = {};
  size_t total = 0u;
  const uint32_t start = hal_millis();
  while (status == HAL_OK && total < 10u && hal_millis() - start < kWaitMs) {
    size_t received = 0u;
    status = hal_tcp_socket_recv_ex(
        socket, buffer + total, sizeof(buffer) - 1u - total, 100u, &received);
    if (status == HAL_ETIMEOUT) {
      status = HAL_OK;
    }
    total += received;
  }
  report("tcp_echo",
         status == HAL_OK && total == 10u &&
             memcmp(buffer, "JH-ESP-TCP", 10u) == 0,
         status, "");
  hal_tcp_socket_close(socket);
}

void check_tcp_refused(void) {
  hal_tcp_socket_t socket = nullptr;
  const hal_status_t status = connect_host(kTcpClosedPort, &socket);
  report("tcp_refused", status == HAL_EIO, status, "");
  hal_tcp_socket_close(socket);
}

void check_tcp_peer_close(void) {
  hal_tcp_socket_t socket = nullptr;
  hal_status_t status = connect_host(kTcpClosePort, &socket);
  char byte = 0;
  size_t received = 1u;
  if (status == HAL_OK) {
    status = hal_tcp_socket_recv_ex(socket, &byte, 1u, kWaitMs, &received);
  }
  report("tcp_peer_close", status == HAL_OK && received == 0u, status, "");
  hal_tcp_socket_close(socket);
}

// The host accepts but never reads: every send returns at once, with a
// partial count, until the buffers are full and the socket reports EAGAIN.
void check_tcp_backpressure(void) {
  hal_tcp_socket_t socket = nullptr;
  hal_status_t status = connect_host(kTcpSinkPort, &socket);
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
  report("tcp_backpressure",
         status == HAL_EAGAIN && total > 0u && slowest_ms < 500u && !can_send,
         status, extra);
  hal_tcp_socket_close(socket);
}

// A disconnect invalidates open handles; after a new join sockets work again.
void check_stale_handle_after_reconnect(void) {
  hal_tcp_socket_t socket = nullptr;
  const hal_status_t connect_status = connect_host(kTcpEchoPort, &socket);
  const hal_status_t disconnect_status = hal_wifi_disconnect_ex(false);
  size_t sent = 0u;
  const hal_status_t stale_status =
      hal_tcp_socket_send_ex(socket, "X", 1u, &sent);
  hal_tcp_socket_close(socket);
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "disconnect=%s",
                 hal_status_to_string(disconnect_status));
  report("stale_handle", connect_status == HAL_OK && stale_status != HAL_OK,
         stale_status, extra);
  if (check_join("rejoin")) {
    check_tcp_echo();
  }
}

// Turning WiFi off with an open socket and starting again.
void check_wifi_off_and_reinit(void) {
  hal_udp_socket_t socket = nullptr;
  (void)hal_udp_socket_open_ex(&socket);
  const hal_status_t off_status = hal_wifi_set_mode_ex(HAL_WIFI_MODE_OFF);
  hal_net_endpoint_t remote = {};
  (void)parse_endpoint(JH_NET_TEST_HOST, kUdpEchoPort, &remote);
  size_t sent = 0u;
  const hal_status_t stale_status =
      hal_udp_socket_sendto_ex(socket, "X", 1u, &remote, &sent);
  hal_udp_socket_close(socket);
  report("wifi_off", off_status == HAL_OK && stale_status != HAL_OK, off_status,
         "");
  const hal_status_t on_status = hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA);
  report("wifi_on", on_status == HAL_OK, on_status, "");
  if (check_join("reinit_join")) {
    check_dns();
    check_udp();
  }
}

hal_status_t http_route(const hal_http_request_t *request,
                        hal_http_response_t *response, void *user) {
  (void)request;
  (void)user;
  ++s_http_hits;
  const hal_status_t status =
      hal_http_response_set_content_type(response, "text/plain");
  return status == HAL_OK
             ? hal_http_response_write_str(response, "JH-ESP-HTTP-OK")
             : status;
}

void websocket_message(hal_websocket_client_t client,
                       hal_websocket_message_type_t type, const uint8_t *data,
                       size_t length, void *user) {
  (void)user;
  if (type == HAL_WEBSOCKET_MESSAGE_TEXT && length == 13u &&
      memcmp(data, "JH-ESP-WS-REQ", 13u) == 0) {
    ++s_ws_messages;
    (void)hal_websocket_send_text(client, "JH-ESP-WS-OK");
  }
}

// The host fetches a page and exchanges a WebSocket message, then sends DONE.
void check_servers(void) {
  hal_net_endpoint_t http_local = {};
  hal_net_endpoint_t ws_local = {};
  hal_status_t status = HAL_OK;
  if (!parse_endpoint(s_local_ip, kHttpPort, &http_local) ||
      !parse_endpoint(s_local_ip, kWebSocketPort, &ws_local)) {
    status = HAL_EINVAL;
  }
  if (status == HAL_OK) {
    status = hal_http_server_route(HAL_HTTP_METHOD_GET, "/jh-net", http_route,
                                   nullptr);
  }
  if (status == HAL_OK) {
    status = hal_http_server_start_ex(&http_local);
  }
  hal_websocket_callbacks_t callbacks = {};
  callbacks.on_message = websocket_message;
  if (status == HAL_OK) {
    status = hal_websocket_server_set_callbacks(&callbacks, nullptr);
  }
  if (status == HAL_OK) {
    status = hal_websocket_server_start_ex(&ws_local, "/ws");
  }
  report("servers_start", status == HAL_OK, status, "");
  (void)snprintf(s_line, sizeof(s_line), "%scheck=serve ip=%s http=%u ws=%u",
                 kPrefix, s_local_ip, (unsigned)kHttpPort,
                 (unsigned)kWebSocketPort);
  hal_serial_println(s_line);

  bool done = false;
  const uint32_t start = hal_millis();
  while (status == HAL_OK && !done && hal_millis() - start < 60000u) {
    hal_http_server_poll();
    hal_websocket_server_poll();
    const char *line = read_serial_line();
    done = line != nullptr && strcmp(line, "DONE") == 0;
    hal_delay_ms(2u);
  }
  hal_websocket_server_stop();
  hal_http_server_stop();
  char extra[48];
  (void)snprintf(extra, sizeof(extra), "http_hits=%u ws_messages=%u",
                 s_http_hits, s_ws_messages);
  report("servers_probe", done && s_http_hits >= 1u && s_ws_messages >= 1u,
         status, extra);
}

void run_checks(void) {
  const hal_status_t mode_status = hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA);
  report("wifi_mode", mode_status == HAL_OK, mode_status, "");
  check_scan();
  check_failed_join("auth_failure", JH_NET_TEST_SSID, "jh-wrong-password-0");
  check_failed_join("no_network", "jh-absent-network-7f3c", "jh-password-0");
  if (check_join("join")) {
    check_dns();
    check_ping();
    check_udp();
    check_tcp_echo();
    check_tcp_refused();
    check_tcp_peer_close();
    check_tcp_backpressure();
    check_stale_handle_after_reconnect();
    check_wifi_off_and_reinit();
    check_servers();
  }
  (void)snprintf(s_line, sizeof(s_line),
                 "%scheck=result pass=%u passed=%u failed=%u", kPrefix,
                 s_failed == 0u ? 1u : 0u, s_passed, s_failed);
  hal_serial_println(s_line);
}

} // namespace

extern "C" void app_start(void) {
  hal_serial_begin(115200u);
  hal_serial_set_flush(true);
  s_led_ready = hal_rgb_led_init_board_status() == HAL_OK;
  hal_rgb_led_set_brightness(24u);
  set_led(HAL_RGB_LED_BLUE);
}

extern "C" void app_task0(void) {
  static bool s_started = false;
  static bool s_finished = false;
  static uint32_t s_last_ms = 0u;
  static bool s_blink = false;
  if (s_finished) {
    hal_delay_ms(100u);
    return;
  }
  if (!s_started) {
    const char *line = read_serial_line();
    if (line != nullptr && strcmp(line, "GO") == 0) {
      s_started = true;
      run_checks();
      set_led(s_failed == 0u ? HAL_RGB_LED_GREEN : HAL_RGB_LED_RED);
      s_finished = true;
      return;
    }
    if (hal_millis() - s_last_ms >= 1000u) {
      s_last_ms = hal_millis();
      s_blink = !s_blink;
      set_led(s_blink ? HAL_RGB_LED_BLUE : HAL_RGB_LED_NONE);
      (void)snprintf(s_line, sizeof(s_line),
                     "%sready=1 host=%s udp_echo=%u tcp_echo=%u tcp_sink=%u "
                     "tcp_close=%u tcp_closed=%u",
                     kPrefix, JH_NET_TEST_HOST, (unsigned)kUdpEchoPort,
                     (unsigned)kTcpEchoPort, (unsigned)kTcpSinkPort,
                     (unsigned)kTcpClosePort, (unsigned)kTcpClosedPort);
      hal_serial_println(s_line);
    }
  }
  hal_delay_ms(10u);
}
