#include "hal/hal_config.h"
#include "hal/hal_target.h"
#ifdef HAL_ENABLE_GPS

/* Shared GPS engine: wraps the portable gps_nmea_parser with a mutex and
 * hal_millis()-based position age, and implements every transport-independent
 * hal_gps_* getter plus hal_gps_encode(). The portable public facade supplies
 * compile-time UART/SoftwareSerial transport selection. */

#include "gps_nmea_parser.h"
#include "hal/hal_gps.h"
#include "hal/hal_sync.h"
#include "hal/hal_system.h" /* hal_millis() */
#include "hal/impl/shared/hal_mutex_once.h"
#include "hal_gps_core.h"

static gps_nmea_t s_p;
static hal_mutex_t s_mutex = nullptr;
static uint32_t s_last_seq = 0;  /* last seen parser commit sequence   */
static uint32_t s_commit_ms = 0; /* hal_millis() at last location fix   */

#if HAL_TARGET_IS_MOCK
typedef struct {
  bool valid;
  bool updated;
  uint32_t age_ms;
  double lat;
  double lng;
  double speed_kmph;
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  double altitude_m;
  double course_deg;
  double hdop;
  double vdop;
  double pdop;
  double horizontal_accuracy_m;
  uint32_t satellites_used;
  uint8_t satellites_in_view;
  uint8_t fix_quality;
  uint8_t fix_mode;
} gps_mock_state_t;

static gps_mock_state_t s_mock = {};
static bool s_mock_injection_active = false;

#define GPS_SOURCE(mock_expr, parser_expr)                                     \
  (s_mock_injection_active ? (mock_expr) : (parser_expr))
#else
#define GPS_SOURCE(mock_expr, parser_expr) (parser_expr)
#endif

static void gps_ensure_mutex(void) { (void)jh_hal_mutex_create_once(&s_mutex); }

void hal_gps_engine_reset(void) {
  gps_ensure_mutex();
  hal_mutex_lock(s_mutex);
  gps_nmea_init(&s_p);
  s_last_seq = 0;
  s_commit_ms = 0;
#if HAL_TARGET_IS_MOCK
  s_mock = {};
  s_mock_injection_active = false;
#endif
  hal_mutex_unlock(s_mutex);
}

void hal_gps_encode(char c) {
  gps_ensure_mutex();
  hal_mutex_lock(s_mutex);
  gps_nmea_encode(&s_p, c);
  if (s_p.loc_commit_seq != s_last_seq) { /* a new location was committed */
    s_last_seq = s_p.loc_commit_seq;
    s_commit_ms = hal_millis();
  }
  hal_mutex_unlock(s_mutex);
}

/* Lock, evaluate @p expr against s_p, unlock, return it. */
#define GPS_GET(type, expr)                                                    \
  gps_ensure_mutex();                                                          \
  hal_mutex_lock(s_mutex);                                                     \
  type _v = (expr);                                                            \
  hal_mutex_unlock(s_mutex);                                                   \
  return _v

bool hal_gps_location_is_valid(void) {
  GPS_GET(bool, GPS_SOURCE(s_mock.valid, s_p.loc_valid));
}
bool hal_gps_location_is_updated(void) {
  GPS_GET(bool, GPS_SOURCE(s_mock.updated, s_p.loc_updated));
}

uint32_t hal_gps_location_age(void) {
  gps_ensure_mutex();
  hal_mutex_lock(s_mutex);
  uint32_t v = GPS_SOURCE(
      s_mock.age_ms, s_p.loc_valid ? (hal_millis() - s_commit_ms) : UINT32_MAX);
  hal_mutex_unlock(s_mutex);
  return v;
}

double hal_gps_latitude(void) {
  gps_ensure_mutex();
  hal_mutex_lock(s_mutex);
#if HAL_TARGET_IS_MOCK
  if (!s_mock_injection_active)
#endif
    s_p.loc_updated = false; /* mirror TinyGPS++: reading clears it */
  double v = GPS_SOURCE(s_mock.lat, gps_nmea_latitude(&s_p));
  hal_mutex_unlock(s_mutex);
  return v;
}

double hal_gps_longitude(void) {
  gps_ensure_mutex();
  hal_mutex_lock(s_mutex);
#if HAL_TARGET_IS_MOCK
  if (!s_mock_injection_active)
#endif
    s_p.loc_updated = false;
  double v = GPS_SOURCE(s_mock.lng, gps_nmea_longitude(&s_p));
  hal_mutex_unlock(s_mutex);
  return v;
}

double hal_gps_speed_kmph(void) {
  GPS_GET(double, GPS_SOURCE(s_mock.speed_kmph, gps_nmea_speed_kmph(&s_p)));
}
double hal_gps_altitude_m(void) {
  GPS_GET(double, GPS_SOURCE(s_mock.altitude_m, gps_nmea_altitude_m(&s_p)));
}
double hal_gps_course_deg(void) {
  GPS_GET(double, GPS_SOURCE(s_mock.course_deg, gps_nmea_course_deg(&s_p)));
}
double hal_gps_hdop(void) {
  GPS_GET(double, GPS_SOURCE(s_mock.hdop, gps_nmea_hdop(&s_p)));
}
double hal_gps_vdop(void) {
  GPS_GET(double, GPS_SOURCE(s_mock.vdop, gps_nmea_vdop(&s_p)));
}
double hal_gps_pdop(void) {
  GPS_GET(double, GPS_SOURCE(s_mock.pdop, gps_nmea_pdop(&s_p)));
}
double hal_gps_horizontal_accuracy_m(void) {
  GPS_GET(double, GPS_SOURCE(s_mock.horizontal_accuracy_m,
                             gps_nmea_horizontal_accuracy_m(&s_p)));
}
uint32_t hal_gps_satellites_used(void) {
  GPS_GET(uint32_t, GPS_SOURCE(s_mock.satellites_used, s_p.sats_used));
}
uint8_t hal_gps_satellites_in_view(void) {
  GPS_GET(uint8_t, GPS_SOURCE(s_mock.satellites_in_view,
                              gps_nmea_satellites_in_view(&s_p)));
}
uint8_t hal_gps_fix_quality(void) {
  GPS_GET(uint8_t, GPS_SOURCE(s_mock.fix_quality, s_p.fix_quality));
}
uint8_t hal_gps_fix_mode(void) {
  GPS_GET(uint8_t, GPS_SOURCE(s_mock.fix_mode, s_p.fix_mode));
}

int hal_gps_date_year(void) {
  GPS_GET(int, GPS_SOURCE(s_mock.year, gps_nmea_year(&s_p)));
}
int hal_gps_date_month(void) {
  GPS_GET(int, GPS_SOURCE(s_mock.month, gps_nmea_month(&s_p)));
}
int hal_gps_date_day(void) {
  GPS_GET(int, GPS_SOURCE(s_mock.day, gps_nmea_day(&s_p)));
}
int hal_gps_time_hour(void) {
  GPS_GET(int, GPS_SOURCE(s_mock.hour, gps_nmea_hour(&s_p)));
}
int hal_gps_time_minute(void) {
  GPS_GET(int, GPS_SOURCE(s_mock.minute, gps_nmea_minute(&s_p)));
}
int hal_gps_time_second(void) {
  GPS_GET(int, GPS_SOURCE(s_mock.second, gps_nmea_second(&s_p)));
}

uint32_t hal_gps_chars_processed(void) {
  GPS_GET(uint32_t, GPS_SOURCE(0u, s_p.chars_processed));
}
uint32_t hal_gps_passed_checksum(void) {
  GPS_GET(uint32_t, GPS_SOURCE(0u, s_p.passed_checksum));
}
uint32_t hal_gps_failed_checksum(void) {
  GPS_GET(uint32_t, GPS_SOURCE(0u, s_p.failed_checksum));
}
uint32_t hal_gps_sentences_with_fix(void) {
  GPS_GET(uint32_t, GPS_SOURCE(0u, s_p.sentences_with_fix));
}

#if HAL_TARGET_IS_MOCK
static void gps_mock_begin_injection_locked(void) {
  if (!s_mock_injection_active) {
    s_mock = {};
    s_mock_injection_active = true;
  }
}

#define GPS_MOCK_SET(statement)                                                \
  gps_ensure_mutex();                                                          \
  hal_mutex_lock(s_mutex);                                                     \
  gps_mock_begin_injection_locked();                                           \
  statement;                                                                   \
  hal_mutex_unlock(s_mutex)

void hal_gps_engine_mock_set_location(double lat, double lng) {
  GPS_MOCK_SET(s_mock.lat = lat; s_mock.lng = lng);
}

void hal_gps_engine_mock_set_valid(bool valid) {
  GPS_MOCK_SET(s_mock.valid = valid);
}

void hal_gps_engine_mock_set_updated(bool updated) {
  GPS_MOCK_SET(s_mock.updated = updated);
}

void hal_gps_engine_mock_set_age(uint32_t age_ms) {
  GPS_MOCK_SET(s_mock.age_ms = age_ms);
}

void hal_gps_engine_mock_set_speed(double kmph) {
  GPS_MOCK_SET(s_mock.speed_kmph = kmph);
}

void hal_gps_engine_mock_set_date(int year, int month, int day) {
  GPS_MOCK_SET(s_mock.year = year; s_mock.month = month; s_mock.day = day);
}

void hal_gps_engine_mock_set_time(int hour, int minute, int second) {
  GPS_MOCK_SET(s_mock.hour = hour; s_mock.minute = minute;
               s_mock.second = second);
}

void hal_gps_engine_mock_set_altitude(double altitude_m) {
  GPS_MOCK_SET(s_mock.altitude_m = altitude_m);
}

void hal_gps_engine_mock_set_course(double course_deg) {
  GPS_MOCK_SET(s_mock.course_deg = course_deg);
}

void hal_gps_engine_mock_set_dop(double hdop, double vdop, double pdop) {
  GPS_MOCK_SET(s_mock.hdop = hdop; s_mock.vdop = vdop; s_mock.pdop = pdop);
}

void hal_gps_engine_mock_set_satellites(uint32_t used, uint8_t in_view) {
  GPS_MOCK_SET(s_mock.satellites_used = used;
               s_mock.satellites_in_view = in_view);
}

void hal_gps_engine_mock_set_fix(uint8_t quality, uint8_t mode) {
  GPS_MOCK_SET(s_mock.fix_quality = quality; s_mock.fix_mode = mode);
}

void hal_gps_engine_mock_set_horizontal_accuracy(double accuracy_m) {
  GPS_MOCK_SET(s_mock.horizontal_accuracy_m = accuracy_m);
}

void hal_gps_engine_mock_reset(void) {
  gps_ensure_mutex();
  hal_mutex_lock(s_mutex);
  gps_nmea_init(&s_p);
  s_last_seq = 0u;
  s_commit_ms = 0u;
  s_mock = {};
  s_mock_injection_active = true;
  hal_mutex_unlock(s_mutex);
}
#endif

#endif /* HAL_ENABLE_GPS */
