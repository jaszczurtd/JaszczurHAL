#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
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
    /* extended fix data */
    double   altitude_m;
    double   course_deg;
    double   hdop, vdop, pdop;
    double   horizontal_accuracy_m;
    uint32_t satellites_used;
    uint8_t  satellites_in_view;
    uint8_t  fix_quality;
    uint8_t  fix_mode;
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

double   hal_gps_altitude_m(void)              { return s_mock_gps.altitude_m;            }
double   hal_gps_course_deg(void)              { return s_mock_gps.course_deg;            }
double   hal_gps_hdop(void)                    { return s_mock_gps.hdop;                  }
double   hal_gps_vdop(void)                    { return s_mock_gps.vdop;                  }
double   hal_gps_pdop(void)                    { return s_mock_gps.pdop;                  }
double   hal_gps_horizontal_accuracy_m(void)   { return s_mock_gps.horizontal_accuracy_m; }
uint32_t hal_gps_satellites_used(void)         { return s_mock_gps.satellites_used;       }
uint8_t  hal_gps_satellites_in_view(void)      { return s_mock_gps.satellites_in_view;    }
uint8_t  hal_gps_fix_quality(void)             { return s_mock_gps.fix_quality;           }
uint8_t  hal_gps_fix_mode(void)                { return s_mock_gps.fix_mode;              }

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

void hal_mock_gps_set_altitude_m(double altitude_m) {
    s_mock_gps.altitude_m = altitude_m;
}

void hal_mock_gps_set_course_deg(double course_deg) {
    s_mock_gps.course_deg = course_deg;
}

void hal_mock_gps_set_dop(double hdop, double vdop, double pdop) {
    s_mock_gps.hdop = hdop;
    s_mock_gps.vdop = vdop;
    s_mock_gps.pdop = pdop;
}

void hal_mock_gps_set_satellites(uint32_t used, uint8_t in_view) {
    s_mock_gps.satellites_used    = used;
    s_mock_gps.satellites_in_view = in_view;
}

void hal_mock_gps_set_fix(uint8_t quality, uint8_t mode) {
    s_mock_gps.fix_quality = quality;
    s_mock_gps.fix_mode    = mode;
}

void hal_mock_gps_set_horizontal_accuracy_m(double accuracy_m) {
    s_mock_gps.horizontal_accuracy_m = accuracy_m;
}

void hal_mock_gps_reset(void) {
    memset(&s_mock_gps, 0, sizeof(s_mock_gps));
}

uint32_t hal_gps_chars_processed(void)    { return 0; }
uint32_t hal_gps_passed_checksum(void)    { return 0; }
uint32_t hal_gps_failed_checksum(void)    { return 0; }
uint32_t hal_gps_sentences_with_fix(void) { return 0; }
int      hal_gps_serial_available(void)   { return 0; }
#endif  // HAL_TARGET_IS_MOCK
