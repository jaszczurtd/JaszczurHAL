#include "../../hal_gps.h"
#include "hal_mock.h"
#include <string.h>

static struct {
    bool     valid;
    bool     updated;
    uint32_t age;
    double   lat;
    double   lng;
    double   speed_kmph;
    int      year, month, day;
    int      hour, minute, second;
} s_mock_gps;

void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud, uint16_t config) {
    (void)rx_pin; (void)tx_pin; (void)baud; (void)config;
    memset(&s_mock_gps, 0, sizeof(s_mock_gps));
}

void hal_gps_encode(char c) {
    (void)c;
}

void hal_gps_update(void) {
    /* no-op in mock - use hal_mock_gps_set_* inject helpers */
}

bool     hal_gps_location_is_valid(void)   { return s_mock_gps.valid;      }
bool     hal_gps_location_is_updated(void) { return s_mock_gps.updated;    }
uint32_t hal_gps_location_age(void)        { return s_mock_gps.age;        }
double   hal_gps_latitude(void)            { return s_mock_gps.lat;        }
double   hal_gps_longitude(void)           { return s_mock_gps.lng;        }
double   hal_gps_speed_kmph(void)          { return s_mock_gps.speed_kmph; }
int      hal_gps_date_year(void)           { return s_mock_gps.year;       }
int      hal_gps_date_month(void)          { return s_mock_gps.month;      }
int      hal_gps_date_day(void)            { return s_mock_gps.day;        }
int      hal_gps_time_hour(void)           { return s_mock_gps.hour;       }
int      hal_gps_time_minute(void)         { return s_mock_gps.minute;     }
int      hal_gps_time_second(void)         { return s_mock_gps.second;     }

/* ── Mock injection helpers ──────────────────────────────────────────────── */

void hal_mock_gps_set_location(double lat, double lng) {
    s_mock_gps.lat = lat;
    s_mock_gps.lng = lng;
}

void hal_mock_gps_set_valid(bool valid) {
    s_mock_gps.valid = valid;
}

void hal_mock_gps_set_updated(bool updated) {
    s_mock_gps.updated = updated;
}

void hal_mock_gps_set_age(uint32_t age_ms) {
    s_mock_gps.age = age_ms;
}

void hal_mock_gps_set_speed(double kmph) {
    s_mock_gps.speed_kmph = kmph;
}

void hal_mock_gps_set_date(int year, int month, int day) {
    s_mock_gps.year  = year;
    s_mock_gps.month = month;
    s_mock_gps.day   = day;
}

void hal_mock_gps_set_time(int hour, int minute, int second) {
    s_mock_gps.hour   = hour;
    s_mock_gps.minute = minute;
    s_mock_gps.second = second;
}

void hal_mock_gps_reset(void) {
    memset(&s_mock_gps, 0, sizeof(s_mock_gps));
}

uint32_t hal_gps_chars_processed(void)    { return 0; }
uint32_t hal_gps_passed_checksum(void)    { return 0; }
uint32_t hal_gps_failed_checksum(void)    { return 0; }
uint32_t hal_gps_sentences_with_fix(void) { return 0; }
int      hal_gps_serial_available(void)   { return 0; }
