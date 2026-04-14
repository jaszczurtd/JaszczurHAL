#include "../../hal_time.h"
#include "../../hal_serial.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

static uint64_t s_unix = 0;
static struct tm s_local_tm = {};
static bool s_local_valid = false;
static char s_tz[64] = "";
static char s_ntp_primary[64] = "";
static char s_ntp_secondary[64] = "";

uint32_t hal_time_from_components(int year, int month, int day,
                                  int hour, int minute, int second) {
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return 0u;
    }

    static const uint8_t days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
    int max_day = days_in_month[month - 1] + ((month == 2 && leap) ? 1 : 0);
    if (day > max_day) {
        return 0u;
    }

    uint32_t days = 0u;
    for (int y = 1970; y < year; y++) {
        bool y_leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        days += y_leap ? 366u : 365u;
    }

    static const uint16_t days_before_month[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    days += (uint32_t)days_before_month[month - 1];
    if (month > 2 && leap) {
        days += 1u;
    }
    days += (uint32_t)(day - 1);

    return days * 86400u + (uint32_t)hour * 3600u + (uint32_t)minute * 60u + (uint32_t)second;
}

bool hal_time_set_timezone(const char *tz) {
    if (!tz || tz[0] == '\0') {
        hal_derr("hal_time_set_timezone: tz is NULL/empty");
        return false;
    }
    snprintf(s_tz, sizeof(s_tz), "%s", tz);
    return true;
}

bool hal_time_sync_ntp(const char *primary_server, const char *secondary_server) {
    if (!primary_server || primary_server[0] == '\0') {
        hal_derr("hal_time_sync_ntp: primary_server is NULL/empty");
        return false;
    }
    snprintf(s_ntp_primary, sizeof(s_ntp_primary), "%s", primary_server);
    snprintf(s_ntp_secondary, sizeof(s_ntp_secondary), "%s", secondary_server ? secondary_server : "");
    return true;
}

uint64_t hal_time_unix(void) {
    return s_unix;
}

bool hal_time_is_synced(uint64_t min_unix) {
    return s_unix >= min_unix;
}

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

void hal_mock_time_set_unix(uint64_t unix_time) {
    s_unix = unix_time;
}

void hal_mock_time_set_local(const struct tm *tm_local) {
    if (!tm_local) {
        s_local_valid = false;
        memset(&s_local_tm, 0, sizeof(s_local_tm));
        return;
    }
    s_local_tm = *tm_local;
    s_local_valid = true;
}

const char *hal_mock_time_get_timezone(void) {
    return s_tz;
}

const char *hal_mock_time_get_ntp_primary(void) {
    return s_ntp_primary;
}

const char *hal_mock_time_get_ntp_secondary(void) {
    return s_ntp_secondary;
}
