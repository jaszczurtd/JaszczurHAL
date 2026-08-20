#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_TIME

#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_target.h"
#include "hal/network/hal_net.h"
#include "hal/network/hal_udp.h"
#include "hal/network/jh_ntp_client.h"
#ifdef HAL_ENABLE_RTC
#include "hal/rtc/hal_rtc.h"
#endif
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "hal/time/hal_time.h"
#include "hal/time/jh_time_platform.h"

#include <stdlib.h>
#include <string.h>
#if !HAL_TARGET_IS_MOCK
#include <errno.h>
#include <sys/time.h>
#endif
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
  bool time_valid;
  hal_time_source_t time_source;
  uint64_t unix_base;
  uint32_t unix_base_micros;
  uint64_t unix_base_monotonic_us;
  hal_time_ntp_state_t ntp_state;
  hal_status_t last_ntp_status;
  uint64_t last_ntp_sync_unix;
#ifdef HAL_ENABLE_RTC
  hal_rtc_t rtc;
  uint32_t rtc_policy_flags;
  hal_status_t last_rtc_status;
#endif
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
  bool valid;
  hal_time_source_t source;
  uint64_t unix_base;
  uint32_t base_micros;
  uint64_t base_monotonic_us;
  hal_time_ntp_state_t ntp_state;
  hal_status_t last_ntp_status;
  uint64_t last_ntp_sync_unix;
#ifdef HAL_ENABLE_RTC
  hal_rtc_t rtc;
  hal_status_t last_rtc_status;
#endif
#if HAL_TARGET_IS_MOCK
  bool local_valid;
  struct tm local;
#endif
};

TimeState s_state = {};
hal_mutex_t s_state_mutex = nullptr;
#ifdef HAL_ENABLE_RTC
hal_mutex_t s_rtc_operation_mutex = nullptr;
#endif
bool s_service_active = false;

hal_mutex_t state_mutex() { return jh_hal_mutex_create_once(&s_state_mutex); }

#ifdef HAL_ENABLE_RTC
hal_mutex_t rtc_operation_mutex() {
  return jh_hal_mutex_create_once(&s_rtc_operation_mutex);
}
#endif

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

hal_status_t start_request(const char *server) {
  uint8_t address[HAL_NET_IPV4_ADDR_LEN] = {};
  hal_status_t status = hal_net_resolve_ipv4_ex(server, address);
  if (status != HAL_OK) {
    return status;
  }

  hal_udp_socket_t socket = nullptr;
  status = hal_udp_socket_open_ex(&socket);
  if (status != HAL_OK) {
    return status;
  }

  hal_net_endpoint_t local = {};
  local.family = HAL_NET_AF_INET;
  local.addr_len = HAL_NET_IPV4_ADDR_LEN;
  local.port = kNtpLocalPort;
  status = hal_udp_socket_bind_ex(socket, &local);

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
    return status != HAL_OK ? status : HAL_EIO;
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
  return HAL_OK;
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
  snapshot.valid = s_state.time_valid;
  snapshot.source = s_state.time_source;
  snapshot.unix_base = s_state.unix_base;
  snapshot.base_micros = s_state.unix_base_micros;
  snapshot.base_monotonic_us = s_state.unix_base_monotonic_us;
  snapshot.ntp_state = s_state.ntp_state;
  snapshot.last_ntp_status = s_state.last_ntp_status;
  snapshot.last_ntp_sync_unix = s_state.last_ntp_sync_unix;
#ifdef HAL_ENABLE_RTC
  snapshot.rtc = s_state.rtc;
  snapshot.last_rtc_status = s_state.last_rtc_status;
#endif
#if HAL_TARGET_IS_MOCK
  snapshot.local_valid = s_state.mock_local_valid;
  snapshot.local = s_state.mock_local;
#endif
  hal_mutex_unlock(mutex);
  return snapshot;
}

uint64_t elapsed_from_snapshot(const ClockSnapshot &snapshot,
                               uint64_t now_monotonic_us) {
  return now_monotonic_us >= snapshot.base_monotonic_us
             ? now_monotonic_us - snapshot.base_monotonic_us
             : 0u;
}

uint64_t unix_from_snapshot(const ClockSnapshot &snapshot,
                            uint64_t now_monotonic_us) {
  if (!snapshot.valid) {
    return 0u;
  }
  const uint64_t elapsed_micros =
      elapsed_from_snapshot(snapshot, now_monotonic_us);
  const uint64_t elapsed_seconds = elapsed_micros / UINT64_C(1000000);
  const uint64_t subsecond = static_cast<uint64_t>(snapshot.base_micros) +
                             elapsed_micros % UINT64_C(1000000);
  return snapshot.unix_base + elapsed_seconds + subsecond / UINT64_C(1000000);
}

uint32_t micros_from_snapshot(const ClockSnapshot &snapshot,
                              uint64_t now_monotonic_us) {
  const uint64_t elapsed_micros =
      elapsed_from_snapshot(snapshot, now_monotonic_us);
  return static_cast<uint32_t>((static_cast<uint64_t>(snapshot.base_micros) +
                                elapsed_micros % UINT64_C(1000000)) %
                               UINT64_C(1000000));
}

bool valid_time_source(hal_time_source_t source) {
  switch (source) {
  case HAL_TIME_SOURCE_MANUAL:
  case HAL_TIME_SOURCE_RTC:
  case HAL_TIME_SOURCE_NTP:
    return true;
  default:
    return false;
  }
}

void set_clock_locked(uint64_t unix_base, uint32_t base_micros,
                      uint64_t base_monotonic_us, hal_time_source_t source) {
  s_state.unix_base = unix_base;
  s_state.unix_base_micros = base_micros;
  s_state.unix_base_monotonic_us = base_monotonic_us;
  s_state.time_source = source;
  s_state.time_valid = true;
}

#ifdef HAL_ENABLE_RTC
void record_rtc_status(hal_rtc_t rtc, hal_status_t status) {
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  if (s_state.rtc == rtc) {
    s_state.last_rtc_status = status;
  }
  hal_mutex_unlock(mutex);
}

void persist_ntp_to_rtc(uint64_t unix_time) {
  hal_mutex_t operation_mutex = rtc_operation_mutex();
  hal_mutex_lock(operation_mutex);
  hal_rtc_t rtc = nullptr;
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  if ((s_state.rtc_policy_flags & HAL_TIME_RTC_WRITE_AFTER_NTP) != 0u) {
    rtc = s_state.rtc;
  }
  hal_mutex_unlock(mutex);
  if (rtc != nullptr) {
    record_rtc_status(rtc, hal_rtc_set_epoch_ex(rtc, unix_time));
  }
  hal_mutex_unlock(operation_mutex);
}
#endif

bool finish_success(const RequestSnapshot &request, uint64_t unix_base,
                    uint32_t base_micros, uint64_t base_monotonic_us) {
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  const bool current =
      s_state.ntp_pending && s_state.ntp_socket == request.socket;
  if (current) {
    set_clock_locked(unix_base, base_micros, base_monotonic_us,
                     HAL_TIME_SOURCE_NTP);
    s_state.ntp_state = HAL_TIME_NTP_SYNCHRONIZED;
    s_state.last_ntp_status = HAL_OK;
    s_state.last_ntp_sync_unix = unix_base;
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
    } else {
      s_state.ntp_state = HAL_TIME_NTP_FAILED;
      s_state.last_ntp_status = HAL_ETIMEOUT;
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
      const uint64_t base_monotonic_us = hal_micros64();
      if (finish_success(request, unix_base, base_micros, base_monotonic_us)) {
        jh_time_platform_clock_changed();
#ifdef HAL_ENABLE_RTC
        persist_ntp_to_rtc(unix_base);
#endif
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
    const hal_status_t retry_status = start_request(secondary);
    if (retry_status != HAL_OK) {
      hal_mutex_t mutex = state_mutex();
      hal_mutex_lock(mutex);
      s_state.ntp_state = HAL_TIME_NTP_FAILED;
      s_state.last_ntp_status = retry_status;
      hal_mutex_unlock(mutex);
    }
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

hal_status_t hal_time_set_unix_ex(uint64_t unix_time, uint32_t micros,
                                  hal_time_source_t source) {
  if (micros >= UINT32_C(1000000) || !valid_time_source(source)) {
    return HAL_EINVAL;
  }

  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  set_clock_locked(unix_time, micros, hal_micros64(), source);
  if (source == HAL_TIME_SOURCE_NTP) {
    s_state.ntp_state = HAL_TIME_NTP_SYNCHRONIZED;
    s_state.last_ntp_status = HAL_OK;
    s_state.last_ntp_sync_unix = unix_time;
  }
  hal_mutex_unlock(mutex);
  jh_time_platform_clock_changed();
#ifdef HAL_ENABLE_RTC
  if (source == HAL_TIME_SOURCE_NTP) {
    persist_ntp_to_rtc(unix_time);
  }
#endif
  return HAL_OK;
}

hal_status_t hal_time_get_status_ex(hal_time_status_t *out_status) {
  if (out_status == nullptr) {
    return HAL_EINVAL;
  }
  ntp_service();
  const ClockSnapshot snapshot = clock_snapshot();
  const uint64_t now_monotonic_us = hal_micros64();
  hal_time_status_t status = {};
  status.valid = snapshot.valid;
  status.source = snapshot.source;
  status.ntp_state = snapshot.ntp_state;
  status.last_ntp_status = snapshot.ntp_state == HAL_TIME_NTP_IDLE
                               ? HAL_NONE
                               : snapshot.last_ntp_status;
  status.last_ntp_sync_unix = snapshot.last_ntp_sync_unix;
#ifdef HAL_ENABLE_RTC
  status.rtc_attached = snapshot.rtc != nullptr;
  status.last_rtc_status =
      status.rtc_attached ? snapshot.last_rtc_status : HAL_NONE;
#else
  status.rtc_attached = false;
  status.last_rtc_status = HAL_NONE;
#endif
  if (snapshot.valid) {
    status.unix_time = unix_from_snapshot(snapshot, now_monotonic_us);
    status.micros = micros_from_snapshot(snapshot, now_monotonic_us);
  }
  *out_status = status;
  return HAL_OK;
}

#ifdef HAL_ENABLE_RTC
hal_status_t hal_time_attach_rtc_ex(hal_rtc_t rtc, uint32_t policy_flags) {
  constexpr uint32_t kSupportedPolicies =
      HAL_TIME_RTC_RESTORE_IF_VALID | HAL_TIME_RTC_WRITE_AFTER_NTP;
  if (rtc == nullptr || (policy_flags & ~kSupportedPolicies) != 0u) {
    return HAL_EINVAL;
  }

  hal_mutex_t operation_mutex = rtc_operation_mutex();
  hal_mutex_lock(operation_mutex);
  const bool restore_requested =
      (policy_flags & HAL_TIME_RTC_RESTORE_IF_VALID) != 0u;
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  if (s_state.rtc != nullptr) {
    hal_mutex_unlock(mutex);
    hal_mutex_unlock(operation_mutex);
    return HAL_EBUSY;
  }
  if (!restore_requested || s_state.time_valid) {
    s_state.rtc = rtc;
    s_state.rtc_policy_flags = policy_flags;
    s_state.last_rtc_status = restore_requested ? HAL_IGNORED : HAL_NONE;
    hal_mutex_unlock(mutex);
    hal_mutex_unlock(operation_mutex);
    return HAL_OK;
  }
  hal_mutex_unlock(mutex);

  bool integrity = false;
  const hal_status_t probe_status =
      hal_rtc_get_clock_integrity_ex(rtc, &integrity);
  if (probe_status != HAL_OK) {
    hal_mutex_unlock(operation_mutex);
    return probe_status;
  }

  hal_mutex_lock(mutex);
  s_state.rtc = rtc;
  s_state.rtc_policy_flags = policy_flags;
  if (s_state.time_valid) {
    s_state.last_rtc_status = HAL_IGNORED;
  } else if (!integrity) {
    s_state.last_rtc_status = HAL_EAGAIN;
  }
  const bool should_restore =
      restore_requested && !s_state.time_valid && integrity;
  hal_mutex_unlock(mutex);
  if (!should_restore) {
    hal_mutex_unlock(operation_mutex);
    return HAL_OK;
  }

  uint64_t epoch = 0u;
  hal_status_t status = hal_rtc_get_epoch_ex(rtc, &epoch);
  bool clock_changed = false;
  if (status == HAL_OK) {
    const uint64_t now_monotonic_us = hal_micros64();
    hal_mutex_lock(mutex);
    if (s_state.rtc != rtc) {
      status = HAL_ECANCELED;
    } else if (s_state.time_valid) {
      status = HAL_IGNORED;
    } else {
      set_clock_locked(epoch, 0u, now_monotonic_us, HAL_TIME_SOURCE_RTC);
      clock_changed = true;
    }
    hal_mutex_unlock(mutex);
  }
  if (clock_changed) {
    jh_time_platform_clock_changed();
  }
  record_rtc_status(rtc, status);
  hal_mutex_unlock(operation_mutex);
  return HAL_OK;
}

hal_status_t hal_time_detach_rtc_ex(void) {
  hal_mutex_t operation_mutex = rtc_operation_mutex();
  hal_mutex_lock(operation_mutex);
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  if (s_state.rtc == nullptr) {
    hal_mutex_unlock(mutex);
    hal_mutex_unlock(operation_mutex);
    return HAL_EUNINIT;
  }
  s_state.rtc = nullptr;
  s_state.rtc_policy_flags = 0u;
  s_state.last_rtc_status = HAL_NONE;
  hal_mutex_unlock(mutex);
  hal_mutex_unlock(operation_mutex);
  return HAL_OK;
}
#endif

hal_status_t hal_time_sync_ntp_ex(const char *primary_server,
                                  const char *secondary_server) {
  if (primary_server == nullptr || primary_server[0] == '\0') {
    hal_derr("hal_time_sync_ntp: primary_server is NULL/empty");
    return HAL_EINVAL;
  }
  if (strlen(primary_server) >= kServerNameSize ||
      (secondary_server != nullptr &&
       strlen(secondary_server) >= kServerNameSize)) {
    hal_derr("hal_time_sync_ntp: server name too long");
    return HAL_EINVAL;
  }

  service_enter();
  close_current_request();
  hal_mutex_t mutex = state_mutex();
  hal_mutex_lock(mutex);
  s_state.ntp_secondary_attempted = false;
  copy_server_name(s_state.ntp_secondary, secondary_server);
  s_state.ntp_state = HAL_TIME_NTP_IN_PROGRESS;
  s_state.last_ntp_status = HAL_EAGAIN;
#if HAL_TARGET_IS_MOCK
  copy_server_name(s_state.mock_primary, primary_server);
#endif
  hal_mutex_unlock(mutex);
  const hal_status_t status = start_request(primary_server);
  if (status != HAL_OK) {
    hal_mutex_lock(mutex);
    s_state.ntp_state = HAL_TIME_NTP_FAILED;
    s_state.last_ntp_status = status;
    hal_mutex_unlock(mutex);
  }
  service_leave();
  return status;
}

bool hal_time_sync_ntp(const char *primary_server,
                       const char *secondary_server) {
  return hal_status_to_bool(
      hal_time_sync_ntp_ex(primary_server, secondary_server));
}

uint64_t hal_time_unix(void) {
  ntp_service();
  const ClockSnapshot snapshot = clock_snapshot();
  return unix_from_snapshot(snapshot, hal_micros64());
}

bool hal_time_is_synced(uint64_t min_unix) {
  ntp_service();
  const ClockSnapshot snapshot = clock_snapshot();
  return snapshot.valid &&
         unix_from_snapshot(snapshot, hal_micros64()) >= min_unix;
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
  if (!snapshot.valid) {
    return false;
  }
  const time_t now =
      static_cast<time_t>(unix_from_snapshot(snapshot, hal_micros64()));
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
  hal_time_status_t status = {};
  const hal_status_t snapshot_status = hal_time_get_status_ex(&status);
  if (snapshot_status != HAL_OK || !status.valid) {
    *out_unix = 0u;
    *out_micros = 0u;
    return snapshot_status == HAL_OK ? HAL_EUNINIT : snapshot_status;
  }
  *out_unix = status.unix_time;
  *out_micros = status.micros;
  return HAL_OK;
}

#if !HAL_TARGET_IS_MOCK
int jh_time_libc_gettimeofday(struct timeval *time_value) {
  if (time_value == nullptr) {
    errno = EINVAL;
    return -1;
  }

  uint64_t unix_time = 0u;
  uint32_t micros = 0u;
  if (jh_time_runtime_snapshot(&unix_time, &micros) != HAL_OK) {
    time_value->tv_sec = 0;
    time_value->tv_usec = 0;
    errno = EAGAIN;
    return -1;
  }
  const time_t seconds = static_cast<time_t>(unix_time);
  if (seconds < 0 || static_cast<uint64_t>(seconds) != unix_time) {
    errno = EOVERFLOW;
    return -1;
  }
  time_value->tv_sec = seconds;
  time_value->tv_usec = static_cast<suseconds_t>(micros);
  return 0;
}

int jh_time_libc_settimeofday(const struct timeval *time_value) {
  if (time_value == nullptr || time_value->tv_sec < 0 ||
      time_value->tv_usec < 0 || time_value->tv_usec >= 1000000) {
    errno = EINVAL;
    return -1;
  }
  const hal_status_t status = hal_time_set_unix_ex(
      static_cast<uint64_t>(time_value->tv_sec),
      static_cast<uint32_t>(time_value->tv_usec), HAL_TIME_SOURCE_MANUAL);
  if (status != HAL_OK) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}
#endif

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
  (void)hal_time_set_unix_ex(unix_time, 0u, HAL_TIME_SOURCE_MANUAL);
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
