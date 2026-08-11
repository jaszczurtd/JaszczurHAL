#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474

#include "../network/jh_ntp_client.h"
#include "hal/core/hal_config.h"
#include "hal/network/hal_net.h"
#include "hal/network/hal_udp.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"
#include "hal/time/hal_time.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#ifdef HAL_ENABLE_TIME

namespace {

constexpr uint32_t kNtpTimeoutMs = 5000u;
constexpr uint16_t kNtpLocalPort = 49152u;
constexpr uint64_t kNtpUnixEpochOffset = UINT64_C(2208988800);

hal_udp_socket_t s_ntp_socket;
hal_net_endpoint_t s_ntp_server{};
uint8_t s_ntp_request[JH_NTP_PACKET_SIZE]{};
char s_ntp_secondary[128]{};
uint32_t s_ntp_started;
bool s_ntp_pending;
bool s_ntp_secondary_attempted;
bool s_time_synced;
uint64_t s_unix_base;
uint32_t s_unix_base_micros;
uint32_t s_unix_base_millis;

void ntp_close() {
  if (s_ntp_socket != nullptr) {
    hal_udp_socket_close(s_ntp_socket);
    s_ntp_socket = nullptr;
  }
  s_ntp_pending = false;
}

bool ntp_start_request(const char *server) {
  uint8_t address[HAL_NET_IPV4_ADDR_LEN] = {};
  if (hal_net_resolve_ipv4_ex(server, address) != HAL_OK) {
    return false;
  }

  hal_udp_socket_t socket = nullptr;
  if (hal_udp_socket_open_ex(&socket) != HAL_OK) {
    return false;
  }

  hal_net_endpoint_t local{};
  local.family = HAL_NET_AF_INET;
  local.addr_len = HAL_NET_IPV4_ADDR_LEN;
  local.port = kNtpLocalPort;
  hal_status_t status = hal_udp_socket_bind_ex(socket, &local);

  uint64_t token =
      (static_cast<uint64_t>(hal_millis()) << 32u) | UINT64_C(0x4a484e54);
  if (token == 0u) {
    token = 1u;
  }
  if (status == HAL_OK) {
    status = jh_ntp_prepare_request(s_ntp_request, token);
  }

  memset(&s_ntp_server, 0, sizeof(s_ntp_server));
  s_ntp_server.family = HAL_NET_AF_INET;
  memcpy(s_ntp_server.addr, address, sizeof(address));
  s_ntp_server.addr_len = HAL_NET_IPV4_ADDR_LEN;
  s_ntp_server.port = JH_NTP_PORT;
  size_t sent = 0u;
  if (status == HAL_OK) {
    status = hal_udp_socket_sendto_ex(
        socket, s_ntp_request, sizeof(s_ntp_request), &s_ntp_server, &sent);
  }
  if (status != HAL_OK || sent != sizeof(s_ntp_request)) {
    hal_udp_socket_close(socket);
    return false;
  }

  s_ntp_socket = socket;
  s_ntp_started = hal_millis();
  s_ntp_pending = true;
  return true;
}

void ntp_service() {
  if (!s_ntp_pending || s_ntp_socket == nullptr) {
    return;
  }
  (void)hal_net_service();

  uint8_t response[JH_NTP_PACKET_SIZE] = {};
  hal_net_endpoint_t source{};
  size_t received = 0u;
  const hal_status_t receive_status = hal_udp_socket_recvfrom_ex(
      s_ntp_socket, response, sizeof(response), &source, 0u, &received);
  if (receive_status == HAL_OK && received > 0u) {
    uint32_t ntp_seconds = 0u;
    uint32_t ntp_fraction = 0u;
    const hal_status_t validation = jh_ntp_validate_response(
        s_ntp_request, &s_ntp_server, response, received, &source, &ntp_seconds,
        &ntp_fraction);
    if (validation == HAL_OK &&
        static_cast<uint64_t>(ntp_seconds) >= kNtpUnixEpochOffset) {
      s_unix_base = static_cast<uint64_t>(ntp_seconds) - kNtpUnixEpochOffset;
      s_unix_base_micros = static_cast<uint32_t>(
          (static_cast<uint64_t>(ntp_fraction) * UINT64_C(1000000)) >> 32u);
      s_unix_base_millis = hal_millis();
      s_time_synced = true;
#if HAL_TARGET_IS_RP
      struct timeval system_time {};
      system_time.tv_sec = static_cast<time_t>(s_unix_base);
      system_time.tv_usec = static_cast<suseconds_t>(s_unix_base_micros);
      (void)settimeofday(&system_time, nullptr);
#endif
      ntp_close();
      return;
    }
  }

  if ((uint32_t)(hal_millis() - s_ntp_started) < kNtpTimeoutMs) {
    return;
  }

  ntp_close();
  if (!s_ntp_secondary_attempted && s_ntp_secondary[0] != '\0') {
    s_ntp_secondary_attempted = true;
    (void)ntp_start_request(s_ntp_secondary);
  }
}

} // namespace

bool hal_time_set_timezone(const char *tz) {
  if (!tz || tz[0] == '\0') {
    hal_derr("hal_time_set_timezone: tz is NULL/empty");
    return false;
  }

  if (setenv("TZ", tz, 1) != 0) {
    hal_derr("hal_time_set_timezone: setenv failed");
    return false;
  }

  tzset();
  return true;
}

bool hal_time_sync_ntp(const char *primary_server,
                       const char *secondary_server) {
  if (!primary_server || primary_server[0] == '\0') {
    hal_derr("hal_time_sync_ntp: primary_server is NULL/empty");
    return false;
  }

  ntp_close();
  s_ntp_secondary_attempted = false;
  s_ntp_secondary[0] = '\0';
  if (secondary_server != nullptr && secondary_server[0] != '\0') {
    const size_t length = strlen(secondary_server);
    if (length >= sizeof(s_ntp_secondary)) {
      hal_derr("hal_time_sync_ntp: secondary server name too long");
      return false;
    }
    memcpy(s_ntp_secondary, secondary_server, length + 1u);
  }
  return ntp_start_request(primary_server);
}

uint64_t hal_time_unix(void) {
  ntp_service();
  if (!s_time_synced) {
    return 0u;
  }
  const uint64_t elapsed_micros =
      static_cast<uint64_t>((uint32_t)(hal_millis() - s_unix_base_millis)) *
      UINT64_C(1000);
  return s_unix_base +
         (static_cast<uint64_t>(s_unix_base_micros) + elapsed_micros) /
             UINT64_C(1000000);
}

bool hal_time_is_synced(uint64_t min_unix) {
  return hal_time_unix() >= min_unix;
}

bool hal_time_get_local(struct tm *out_tm) {
  if (!out_tm) {
    hal_derr("hal_time_get_local: out_tm is NULL");
    return false;
  }

  time_t now = (time_t)hal_time_unix();
  if (!s_time_synced) {
    return false;
  }
  return localtime_r(&now, out_tm) != NULL;
}

bool hal_time_format_local(char *out, size_t out_size, const char *format) {
  if (!out || out_size == 0u) {
    hal_derr("hal_time_format_local: output buffer invalid");
    return false;
  }
  if (!format || format[0] == '\0') {
    hal_derr("hal_time_format_local: format is NULL/empty");
    return false;
  }

  struct tm tm_local;
  if (!hal_time_get_local(&tm_local)) {
    return false;
  }

  return strftime(out, out_size, format, &tm_local) > 0u;
}

#if HAL_TARGET_IS_STM32G474
extern "C" int jh_stm32g474_runtime_gettimeofday(struct timeval *time_value) {
  if (time_value == nullptr) {
    return -1;
  }
  const uint64_t now = hal_time_unix();
  if (!s_time_synced) {
    time_value->tv_sec = 0;
    time_value->tv_usec = 0;
    errno = EAGAIN;
    return -1;
  }
  const uint64_t elapsed_micros =
      static_cast<uint64_t>((uint32_t)(hal_millis() - s_unix_base_millis)) *
      UINT64_C(1000);
  const uint64_t subsecond =
      static_cast<uint64_t>(s_unix_base_micros) + elapsed_micros;
  time_value->tv_sec = (time_t)now;
  time_value->tv_usec = (suseconds_t)(subsecond % UINT64_C(1000000));
  return 0;
}
#endif

#endif /* HAL_ENABLE_TIME */

#endif // HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474
