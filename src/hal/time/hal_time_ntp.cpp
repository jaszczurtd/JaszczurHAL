#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_TIME

#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_target.h"
#include "hal/network/hal_net.h"
#include "hal/network/hal_udp.h"
#include "hal/network/jh_ntp_client.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "hal/time/hal_time.h"
#include "hal/time/jh_time_platform.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

constexpr uint32_t kNtpTimeoutMs = 5000u;
constexpr uint16_t kNtpLocalPort = 49152u;
constexpr uint64_t kNtpUnixEpochOffset = UINT64_C(2208988800);
constexpr size_t kServerNameSize = 128u;

struct TimeState {
  hal_udp_socket_t ntp_socket;
  hal_net_endpoint_t ntp_server;
  uint8_t ntp_request[JH_NTP_PACKET_SIZE];
  char ntp_secondary[kServerNameSize];
  uint32_t ntp_started;
  bool ntp_pending;
  bool ntp_secondary_attempted;
  bool time_synced;
  uint64_t unix_base;
  uint32_t unix_base_micros;
  uint32_t unix_base_millis;
#if HAL_TARGET_IS_MOCK
  char mock_timezone[kServerNameSize];
  char mock_primary[kServerNameSize];
  bool mock_local_valid;
  struct tm mock_local;
#endif
};

struct RequestSnapshot {
  hal_udp_socket_t socket;
  hal_net_endpoint_t server;
  uint8_t request[JH_NTP_PACKET_SIZE];
  uint32_t started;
  bool pending;
};

struct ClockSnapshot {
  bool synced;
  uint64_t unix_base;
  uint32_t base_micros;
  uint32_t base_millis;
#if HAL_TARGET_IS_MOCK
  bool local_valid;
  struct tm local;
#endif
};

TimeState s_state = {};
hal_mutex_t s_state_mutex = nullptr;
bool s_service_active = false;

hal_mutex_t state_mutex() { return jh_hal_mutex_create_once(&s_state_mutex); }

bool service_try_enter() {
  bool expected = false;
  return __atomic_compare_exchange_n(&s_service_active, &expected, true, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

void service_enter() {
  while (!service_try_enter()) {
    hal_delay_us(50u);
  }
}

void service_leave() {
  __atomic_store_n(&s_service_active, false, __ATOMIC_RELEASE);
}

void copy_server_name(char out[kServerNameSize], const char *server) {
  if (server == nullptr) {
    out[0] = '\0';
    return;
  }
  size_t length = strlen(server);
  if (length >= kServerNameSize) {
    length = kServerNameSize - 1u;
  }
  memcpy(out, server, length);
  out[length] = '\0';
}

hal_udp_socket_t detach_socket() {
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  hal_udp_socket_t socket = s_state.ntp_socket;
  s_state.ntp_socket = nullptr;
  s_state.ntp_pending = false;
  hal_mutex_unlock(mutex);
  return socket;
}

void close_current_request() {
  hal_udp_socket_t socket = detach_socket();
  if (socket != nullptr) {
    hal_udp_socket_close(socket);
  }
}

bool start_request(const char *server) {
  uint8_t address[HAL_NET_IPV4_ADDR_LEN] = {};
  if (hal_net_resolve_ipv4_ex(server, address) != HAL_OK) {
    return false;
  }

  hal_udp_socket_t socket = nullptr;
  if (hal_udp_socket_open_ex(&socket) != HAL_OK) {
    return false;
  }

  hal_net_endpoint_t local = {};
  local.family = HAL_NET_AF_INET;
  local.addr_len = HAL_NET_IPV4_ADDR_LEN;
  local.port = kNtpLocalPort;
  hal_status_t status = hal_udp_socket_bind_ex(socket, &local);

  uint8_t request[JH_NTP_PACKET_SIZE] = {};
  uint64_t token =
      (static_cast<uint64_t>(hal_millis()) << 32u) | UINT64_C(0x4a484e54);
  if (token == 0u) {
    token = 1u;
  }
  if (status == HAL_OK) {
    status = jh_ntp_prepare_request(request, token);
  }

  hal_net_endpoint_t remote = {};
  remote.family = HAL_NET_AF_INET;
  memcpy(remote.addr, address, sizeof(address));
  remote.addr_len = HAL_NET_IPV4_ADDR_LEN;
  remote.port = JH_NTP_PORT;
  size_t sent = 0u;
  if (status == HAL_OK) {
    status = hal_udp_socket_sendto_ex(socket, request, sizeof(request), &remote,
                                      &sent);
  }
  if (status != HAL_OK || sent != sizeof(request)) {
    hal_udp_socket_close(socket);
    return false;
  }

  const uint32_t started = hal_millis();
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  s_state.ntp_socket = socket;
  s_state.ntp_server = remote;
  memcpy(s_state.ntp_request, request, sizeof(request));
  s_state.ntp_started = started;
  s_state.ntp_pending = true;
  hal_mutex_unlock(mutex);
  return true;
}

RequestSnapshot request_snapshot() {
  RequestSnapshot snapshot = {};
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  snapshot.socket = s_state.ntp_socket;
  snapshot.server = s_state.ntp_server;
  memcpy(snapshot.request, s_state.ntp_request, sizeof(snapshot.request));
  snapshot.started = s_state.ntp_started;
  snapshot.pending = s_state.ntp_pending;
  hal_mutex_unlock(mutex);
  return snapshot;
}

ClockSnapshot clock_snapshot() {
  ClockSnapshot snapshot = {};
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  snapshot.synced = s_state.time_synced;
  snapshot.unix_base = s_state.unix_base;
  snapshot.base_micros = s_state.unix_base_micros;
  snapshot.base_millis = s_state.unix_base_millis;
#if HAL_TARGET_IS_MOCK
  snapshot.local_valid = s_state.mock_local_valid;
  snapshot.local = s_state.mock_local;
#endif
  hal_mutex_unlock(mutex);
  return snapshot;
}

uint64_t unix_from_snapshot(const ClockSnapshot &snapshot,
                            uint32_t now_millis) {
  if (!snapshot.synced) {
    return 0u;
  }
  const uint64_t elapsed_micros = static_cast<uint64_t>(static_cast<uint32_t>(
                                      now_millis - snapshot.base_millis)) *
                                  UINT64_C(1000);
  return snapshot.unix_base +
         (static_cast<uint64_t>(snapshot.base_micros) + elapsed_micros) /
             UINT64_C(1000000);
}

bool finish_success(const RequestSnapshot &request, uint64_t unix_base,
                    uint32_t base_micros, uint32_t base_millis) {
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  const bool current =
      s_state.ntp_pending && s_state.ntp_socket == request.socket;
  if (current) {
    s_state.unix_base = unix_base;
    s_state.unix_base_micros = base_micros;
    s_state.unix_base_millis = base_millis;
    s_state.time_synced = true;
    s_state.ntp_socket = nullptr;
    s_state.ntp_pending = false;
  }
  hal_mutex_unlock(mutex);
  return current;
}

bool finish_timeout(const RequestSnapshot &request,
                    char secondary[kServerNameSize]) {
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  const bool current =
      s_state.ntp_pending && s_state.ntp_socket == request.socket;
  bool retry = false;
  if (current) {
    s_state.ntp_socket = nullptr;
    s_state.ntp_pending = false;
    retry =
        !s_state.ntp_secondary_attempted && s_state.ntp_secondary[0] != '\0';
    if (retry) {
      s_state.ntp_secondary_attempted = true;
      memcpy(secondary, s_state.ntp_secondary, kServerNameSize);
    }
  }
  hal_mutex_unlock(mutex);
  return retry;
}

void ntp_service() {
  if (!service_try_enter()) {
    return;
  }
  const RequestSnapshot request = request_snapshot();
  if (!request.pending || request.socket == nullptr) {
    service_leave();
    return;
  }

  (void)hal_net_service();
  uint8_t response[JH_NTP_PACKET_SIZE] = {};
  hal_net_endpoint_t source = {};
  size_t received = 0u;
  const hal_status_t receive_status = hal_udp_socket_recvfrom_ex(
      request.socket, response, sizeof(response), &source, 0u, &received);
  if (receive_status == HAL_OK && received > 0u) {
    uint32_t ntp_seconds = 0u;
    uint32_t ntp_fraction = 0u;
    const hal_status_t validation = jh_ntp_validate_response(
        request.request, &request.server, response, received, &source,
        &ntp_seconds, &ntp_fraction);
    if (validation == HAL_OK &&
        static_cast<uint64_t>(ntp_seconds) >= kNtpUnixEpochOffset) {
      const uint64_t unix_base =
          static_cast<uint64_t>(ntp_seconds) - kNtpUnixEpochOffset;
      const uint32_t base_micros = static_cast<uint32_t>(
          (static_cast<uint64_t>(ntp_fraction) * UINT64_C(1000000)) >> 32u);
      const uint32_t base_millis = hal_millis();
      if (finish_success(request, unix_base, base_micros, base_millis)) {
        jh_time_platform_apply_unix(unix_base, base_micros);
        hal_udp_socket_close(request.socket);
      }
      service_leave();
      return;
    }
  }

  if (static_cast<uint32_t>(hal_millis() - request.started) < kNtpTimeoutMs) {
    service_leave();
    return;
  }

  char secondary[kServerNameSize] = {};
  const bool retry = finish_timeout(request, secondary);
  hal_udp_socket_close(request.socket);
  if (retry) {
    (void)start_request(secondary);
  }
  service_leave();
}

bool set_process_timezone(const char *tz) {
#if HAL_TARGET_IS_MOCK && defined(_WIN32)
  if (_putenv_s("TZ", tz) != 0) {
    return false;
  }
  _tzset();
#else
  if (setenv("TZ", tz, 1) != 0) {
    return false;
  }
  tzset();
#endif
  return true;
}

bool local_time(time_t now, struct tm *out_tm) {
#if HAL_TARGET_IS_MOCK && defined(_WIN32)
  return localtime_s(out_tm, &now) == 0;
#else
  return localtime_r(&now, out_tm) != nullptr;
#endif
}

} // namespace

bool hal_time_set_timezone(const char *tz) {
  if (tz == nullptr || tz[0] == '\0') {
    hal_derr("hal_time_set_timezone: tz is NULL/empty");
    return false;
  }
  if (!set_process_timezone(tz)) {
    hal_derr("hal_time_set_timezone: environment update failed");
    return false;
  }
#if HAL_TARGET_IS_MOCK
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  copy_server_name(s_state.mock_timezone, tz);
  hal_mutex_unlock(mutex);
#endif
  return true;
}

bool hal_time_sync_ntp(const char *primary_server,
                       const char *secondary_server) {
  if (primary_server == nullptr || primary_server[0] == '\0') {
    hal_derr("hal_time_sync_ntp: primary_server is NULL/empty");
    return false;
  }
  if (secondary_server != nullptr &&
      strlen(secondary_server) >= kServerNameSize) {
    hal_derr("hal_time_sync_ntp: secondary server name too long");
    return false;
  }

  service_enter();
  close_current_request();
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  s_state.ntp_secondary_attempted = false;
  copy_server_name(s_state.ntp_secondary, secondary_server);
#if HAL_TARGET_IS_MOCK
  copy_server_name(s_state.mock_primary, primary_server);
#endif
  hal_mutex_unlock(mutex);
  const bool started = start_request(primary_server);
  service_leave();
  return started;
}

uint64_t hal_time_unix(void) {
  ntp_service();
  const ClockSnapshot snapshot = clock_snapshot();
  return unix_from_snapshot(snapshot, hal_millis());
}

bool hal_time_is_synced(uint64_t min_unix) {
  return hal_time_unix() >= min_unix;
}

bool hal_time_get_local(struct tm *out_tm) {
  if (out_tm == nullptr) {
    hal_derr("hal_time_get_local: out_tm is NULL");
    return false;
  }
  ntp_service();
  const ClockSnapshot snapshot = clock_snapshot();
#if HAL_TARGET_IS_MOCK
  if (snapshot.local_valid) {
    *out_tm = snapshot.local;
    return true;
  }
#endif
  if (!snapshot.synced) {
    return false;
  }
  const time_t now =
      static_cast<time_t>(unix_from_snapshot(snapshot, hal_millis()));
  return local_time(now, out_tm);
}

bool hal_time_format_local(char *out, size_t out_size, const char *format) {
  if (out == nullptr || out_size == 0u) {
    hal_derr("hal_time_format_local: output buffer invalid");
    return false;
  }
  if (format == nullptr || format[0] == '\0') {
    hal_derr("hal_time_format_local: format is NULL/empty");
    return false;
  }
  struct tm tm_local = {};
  return hal_time_get_local(&tm_local) &&
         strftime(out, out_size, format, &tm_local) > 0u;
}

hal_status_t jh_time_runtime_snapshot(uint64_t *out_unix,
                                      uint32_t *out_micros) {
  if (out_unix == nullptr || out_micros == nullptr) {
    return HAL_EINVAL;
  }
  ntp_service();
  const ClockSnapshot snapshot = clock_snapshot();
  if (!snapshot.synced) {
    *out_unix = 0u;
    *out_micros = 0u;
    return HAL_EUNINIT;
  }
  const uint32_t now_millis = hal_millis();
  const uint64_t elapsed_micros = static_cast<uint64_t>(static_cast<uint32_t>(
                                      now_millis - snapshot.base_millis)) *
                                  UINT64_C(1000);
  const uint64_t subsecond =
      static_cast<uint64_t>(snapshot.base_micros) + elapsed_micros;
  *out_unix = unix_from_snapshot(snapshot, now_millis);
  *out_micros = static_cast<uint32_t>(subsecond % UINT64_C(1000000));
  return HAL_OK;
}

#if HAL_TARGET_IS_MOCK
void hal_mock_time_reset(void) {
  service_enter();
  hal_udp_socket_t socket = nullptr;
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  socket = s_state.ntp_socket;
  memset(&s_state, 0, sizeof(s_state));
  hal_mutex_unlock(mutex);
  if (socket != nullptr) {
    hal_udp_socket_close(socket);
  }
  service_leave();
}

void hal_mock_time_set_unix(uint64_t unix_time) {
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  s_state.unix_base = unix_time;
  s_state.unix_base_micros = 0u;
  s_state.unix_base_millis = hal_millis();
  s_state.time_synced = true;
  hal_mutex_unlock(mutex);
}

void hal_mock_time_set_local(const struct tm *tm_local) {
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  s_state.mock_local_valid = tm_local != nullptr;
  if (tm_local != nullptr) {
    s_state.mock_local = *tm_local;
  } else {
    memset(&s_state.mock_local, 0, sizeof(s_state.mock_local));
  }
  hal_mutex_unlock(mutex);
}

const char *hal_mock_time_get_timezone(void) { return s_state.mock_timezone; }

const char *hal_mock_time_get_ntp_primary(void) { return s_state.mock_primary; }

const char *hal_mock_time_get_ntp_secondary(void) {
  return s_state.ntp_secondary;
}
#endif

#endif /* HAL_ENABLE_TIME */
