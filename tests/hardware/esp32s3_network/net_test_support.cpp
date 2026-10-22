#include "net_test_support.h"

#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#include <esp_system.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

char g_net_test_local_ip[20];

namespace {

char s_line[320];
char s_serial_line[32];
size_t s_serial_length = 0u;
unsigned s_passed = 0u;
unsigned s_failed = 0u;

} // namespace

void net_test_report(const char *check, bool pass, hal_status_t status,
                     const char *extra) {
  (void)snprintf(s_line, sizeof(s_line), "%scheck=%s pass=%u status=%s%s%s",
                 kNetTestPrefix, check, pass ? 1u : 0u,
                 hal_status_to_string(status), extra[0] != '\0' ? " " : "",
                 extra);
  hal_serial_println(s_line);
  if (pass) {
    ++s_passed;
  } else {
    ++s_failed;
  }
}

void net_test_announce(const char *text) {
  (void)snprintf(s_line, sizeof(s_line), "%s%s", kNetTestPrefix, text);
  hal_serial_println(s_line);
}

unsigned net_test_failed(void) { return s_failed; }

void net_test_report_result(void) {
  (void)snprintf(s_line, sizeof(s_line),
                 "%scheck=result pass=%u passed=%u failed=%u", kNetTestPrefix,
                 s_failed == 0u ? 1u : 0u, s_passed, s_failed);
  hal_serial_println(s_line);
}

const char *net_test_read_line(void) {
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

bool net_test_wait_for_line(const char *command, uint32_t timeout_ms) {
  const uint32_t start = hal_millis();
  while (hal_millis() - start < timeout_ms) {
    const char *line = net_test_read_line();
    if (line != nullptr && strcmp(line, command) == 0) {
      return true;
    }
    hal_delay_ms(5u);
  }
  return false;
}

bool net_test_endpoint(const char *address, uint16_t port,
                       hal_net_endpoint_t *out) {
  size_t count = 0u;
  if (hal_net_resolve_ex(address, HAL_NET_AF_INET, out, 1u, &count) != HAL_OK ||
      count != 1u) {
    return false;
  }
  out->port = port;
  return true;
}

hal_net_endpoint_t net_test_any_ipv4(uint16_t port) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.port = port;
  return endpoint;
}

hal_status_t net_test_join(const char *ssid, const char *password,
                           hal_wifi_state_t *out_state) {
  (void)hal_wifi_set_timeout_ms_ex(kNetTestJoinTimeoutMs);
  const hal_status_t status = hal_wifi_begin_station_ex(ssid, password, false);
  *out_state = HAL_WIFI_STATE_FAILED;
  (void)hal_wifi_get_state_ex(out_state);
  return status;
}

bool net_test_check_join(const char *check) {
  hal_wifi_state_t state = HAL_WIFI_STATE_FAILED;
  const hal_status_t status =
      net_test_join(JH_NET_TEST_SSID, JH_NET_TEST_PASSWORD, &state);
  g_net_test_local_ip[0] = '\0';
  (void)hal_wifi_get_local_ip_ex(g_net_test_local_ip,
                                 sizeof(g_net_test_local_ip));
  const bool pass = status == HAL_OK && state == HAL_WIFI_STATE_CONNECTED &&
                    g_net_test_local_ip[0] != '\0' &&
                    strcmp(g_net_test_local_ip, "0.0.0.0") != 0;
  char extra[64];
  (void)snprintf(extra, sizeof(extra), "state=%s ip=%s",
                 hal_wifi_state_to_string(state), g_net_test_local_ip);
  net_test_report(check, pass, status, extra);
  return pass;
}

hal_status_t net_test_connect(const char *address, uint16_t port,
                              hal_tcp_socket_t *out_socket) {
  hal_net_endpoint_t remote = {};
  if (!net_test_endpoint(address, port, &remote)) {
    return HAL_EINVAL;
  }
  hal_status_t status = hal_tcp_socket_open_ex(out_socket);
  if (status == HAL_OK) {
    status = hal_tcp_socket_connect_ex(*out_socket, &remote, kNetTestWaitMs);
  }
  return status;
}

hal_status_t net_test_tcp_roundtrip(hal_tcp_socket_t socket, const char *data,
                                    size_t length, uint32_t timeout_ms) {
  size_t offset = 0u;
  hal_status_t status = HAL_OK;
  const uint32_t start = hal_millis();
  while (status == HAL_OK && offset < length) {
    size_t sent = 0u;
    status =
        hal_tcp_socket_send_ex(socket, data + offset, length - offset, &sent);
    if (status == HAL_EAGAIN && hal_millis() - start < timeout_ms) {
      status = HAL_OK;
      hal_delay_ms(1u);
    }
    offset += sent;
  }
  char buffer[64];
  size_t total = 0u;
  while (status == HAL_OK && total < length) {
    if (hal_millis() - start >= timeout_ms) {
      return HAL_ETIMEOUT;
    }
    size_t chunk =
        length - total < sizeof(buffer) ? length - total : sizeof(buffer);
    size_t received = 0u;
    status = hal_tcp_socket_recv_ex(socket, buffer, chunk, 100u, &received);
    if (status == HAL_ETIMEOUT) {
      status = HAL_OK;
      continue;
    }
    if (status == HAL_OK && received == 0u) {
      return HAL_EIO;
    }
    if (status == HAL_OK && memcmp(buffer, data + total, received) != 0) {
      return HAL_EPROTO;
    }
    total += received;
  }
  return status;
}

namespace {

constexpr uint32_t kSoakReportMs = 30000u;
constexpr uint32_t kSoakHeapDriftBytes = 8192u;

} // namespace

void net_test_soak_begin(NetTestSoak *soak) {
  *soak = {};
  soak->started_ms = hal_millis();
  soak->last_report_ms = soak->started_ms;
  soak->heap_start = hal_get_free_heap();
}

void net_test_soak_count(NetTestSoak *soak, hal_status_t status) {
  if (status == HAL_OK) {
    ++soak->ok;
  } else {
    ++soak->errors;
  }
  if (hal_millis() - soak->last_report_ms >= kSoakReportMs) {
    soak->last_report_ms = hal_millis();
    char line[160];
    (void)snprintf(line, sizeof(line),
                   "soak t=%" PRIu32 " ok=%u errors=%u heap=%" PRIu32
                   " heap_min=%" PRIu32,
                   (hal_millis() - soak->started_ms) / 1000u, soak->ok,
                   soak->errors, hal_get_free_heap(),
                   static_cast<uint32_t>(esp_get_minimum_free_heap_size()));
    net_test_announce(line);
  }
}

void net_test_soak_finish(const NetTestSoak *soak, const char *extra) {
  const uint32_t heap_end = hal_get_free_heap();
  const unsigned total = soak->ok + soak->errors;
  char line[200];
  (void)snprintf(line, sizeof(line),
                 "seconds=%" PRIu32 " ok=%u errors=%u heap_start=%" PRIu32
                 " heap_end=%" PRIu32 " heap_min=%" PRIu32 "%s%s",
                 (hal_millis() - soak->started_ms) / 1000u, soak->ok,
                 soak->errors, soak->heap_start, heap_end,
                 static_cast<uint32_t>(esp_get_minimum_free_heap_size()),
                 extra[0] != '\0' ? " " : "", extra);
  net_test_report("soak",
                  total > 0u && soak->errors * 200u <= total &&
                      heap_end + kSoakHeapDriftBytes >= soak->heap_start,
                  soak->errors == 0u ? HAL_OK : HAL_EIO, line);
}
