#if !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32)

#include "../../hal_config.h"
#include "../../hal_time.h"
#include "../../hal_serial.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

uint32_t hal_time_from_components(int year, int month, int day,
                                  int hour, int minute, int second) {
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return 0u;
    }

    static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
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

    static const uint16_t days_before_month[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    days += (uint32_t)days_before_month[month - 1];
    if (month > 2 && leap) {
        days += 1u;
    }
    days += (uint32_t)(day - 1);

    return days * 86400u + (uint32_t)hour * 3600u + (uint32_t)minute * 60u + (uint32_t)second;
}

#ifndef HAL_DISABLE_TIME

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

bool hal_time_sync_ntp(const char *primary_server, const char *secondary_server) {
    (void)secondary_server;
    if (!primary_server || primary_server[0] == '\0') {
        hal_derr("hal_time_sync_ntp: primary_server is NULL/empty");
        return false;
    }

    hal_derr_limited("time", "NTP sync is not implemented for STM32G474 skeleton backend");
    return false;
}

uint64_t hal_time_unix(void) {
    return (uint64_t)time(NULL);
}

bool hal_time_is_synced(uint64_t min_unix) {
    return hal_time_unix() >= min_unix;
}

bool hal_time_get_local(struct tm *out_tm) {
    if (!out_tm) {
        hal_derr("hal_time_get_local: out_tm is NULL");
        return false;
    }

    time_t now = time(NULL);
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

#endif /* HAL_DISABLE_TIME */

#endif /* !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32) */
