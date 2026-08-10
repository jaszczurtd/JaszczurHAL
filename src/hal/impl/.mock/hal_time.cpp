#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_serial.h"
#include "../../hal_time.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

static uint64_t s_unix = 0;
static struct tm s_local_tm = {};
static bool s_local_valid = false;
static char s_tz[64] = "";
static char s_ntp_primary[64] = "";
static char s_ntp_secondary[64] = "";

bool hal_time_set_timezone(const char *tz) {
  if (!tz || tz[0] == '\0') {
    hal_derr("hal_time_set_timezone: tz is NULL/empty");
    return false;
  }
  snprintf(s_tz, sizeof(s_tz), "%s", tz);
  return true;
}

bool hal_time_sync_ntp(const char *primary_server,
                       const char *secondary_server) {
  if (!primary_server || primary_server[0] == '\0') {
    hal_derr("hal_time_sync_ntp: primary_server is NULL/empty");
    return false;
  }
  snprintf(s_ntp_primary, sizeof(s_ntp_primary), "%s", primary_server);
  snprintf(s_ntp_secondary, sizeof(s_ntp_secondary), "%s",
           secondary_server ? secondary_server : "");
  return true;
}

uint64_t hal_time_unix(void) { return s_unix; }

bool hal_time_is_synced(uint64_t min_unix) { return s_unix >= min_unix; }

bool hal_time_get_local(struct tm *out_tm) {
  if (!out_tm) {
    hal_derr("hal_time_get_local: out_tm is NULL");
    return false;
  }
  if (!s_local_valid) {
    hal_derr("hal_time_get_local: local time not set");
    return false;
  }
  *out_tm = s_local_tm;
  return true;
}

bool hal_time_format_local(char *out, size_t out_size, const char *format) {
  if (!out || out_size == 0) {
    hal_derr("hal_time_format_local: output buffer invalid");
    return false;
  }
  if (!format || format[0] == '\0') {
    hal_derr("hal_time_format_local: format is NULL/empty");
    return false;
  }
  if (!s_local_valid) {
    hal_derr("hal_time_format_local: local time not set");
    return false;
  }
  return strftime(out, out_size, format, &s_local_tm) > 0;
}

void hal_mock_time_reset(void) {
  s_unix = 0;
  memset(&s_local_tm, 0, sizeof(s_local_tm));
  s_local_valid = false;
  s_tz[0] = '\0';
  s_ntp_primary[0] = '\0';
  s_ntp_secondary[0] = '\0';
}

void hal_mock_time_set_unix(uint64_t unix_time) { s_unix = unix_time; }

void hal_mock_time_set_local(const struct tm *tm_local) {
  if (!tm_local) {
    s_local_valid = false;
    memset(&s_local_tm, 0, sizeof(s_local_tm));
    return;
  }
  s_local_tm = *tm_local;
  s_local_valid = true;
}

const char *hal_mock_time_get_timezone(void) { return s_tz; }

const char *hal_mock_time_get_ntp_primary(void) { return s_ntp_primary; }

const char *hal_mock_time_get_ntp_secondary(void) { return s_ntp_secondary; }
#endif // HAL_TARGET_IS_MOCK
