#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"
#ifdef HAL_ENABLE_GPS

/* Shared GPS engine: wraps the portable gps_nmea_parser with a mutex and
 * hal_millis()-based position and UTC age, and implements every
 * transport-independent hal_gps_* getter plus hal_gps_encode(). The portable
 * public facade supplies compile-time UART/SoftwareSerial transport
 * selection. */

#include "gps_nmea_parser.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/gps/hal_gps.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h" /* hal_millis() */
#include "hal_gps_core.h"

/* hal_millis() of the parse that last bumped a parser commit sequence. */
typedef struct {
  uint32_t seq;
  uint32_t ms;
} gps_commit_stamp_t;

static gps_nmea_t s_p;
static hal_mutex_t s_mutex = nullptr;
static gps_commit_stamp_t s_loc_stamp = {}; /* location commits */
static gps_commit_stamp_t s_utc_stamp = {}; /* UTC commits      */

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
  bool utc_available;
  hal_gps_utc_t utc;
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

/* Caller holds s_mutex. */
static void gps_reset_parser_locked(void) {
  gps_nmea_init(&s_p);
  s_loc_stamp = {};
  s_utc_stamp = {};
}

static void gps_stamp_commit(gps_commit_stamp_t *stamp, uint32_t seq) {
  if (seq != stamp->seq) {
    stamp->seq = seq;
    stamp->ms = hal_millis();
  }
}

void hal_gps_engine_reset(void) {
  gps_ensure_mutex();
  hal_mutex_lock(s_mutex);
  gps_reset_parser_locked();
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
  gps_stamp_commit(&s_loc_stamp, s_p.loc_commit_seq);
  gps_stamp_commit(&s_utc_stamp, s_p.utc_commit_seq);
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
  uint32_t v =
      GPS_SOURCE(s_mock.age_ms,
                 s_p.loc_valid ? (hal_millis() - s_loc_stamp.ms) : UINT32_MAX);
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

/* Caller holds s_mutex. */
static bool gps_parsed_utc_locked(hal_gps_utc_t *out_utc) {
  if (!s_p.utc_valid) {
    return false;
  }
  out_utc->year = s_p.utc.year;
  out_utc->month = s_p.utc.month;
  out_utc->day = s_p.utc.day;
  out_utc->hour = s_p.utc.hour;
  out_utc->minute = s_p.utc.minute;
  out_utc->second = s_p.utc.second;
  out_utc->age_ms = hal_millis() - s_utc_stamp.ms;
  return true;
}

#if HAL_TARGET_IS_MOCK
/* Caller holds s_mutex. */
static bool gps_injected_utc_locked(hal_gps_utc_t *out_utc) {
  if (!s_mock.utc_available) {
    return false;
  }
  *out_utc = s_mock.utc;
  return true;
}
#endif

hal_status_t hal_gps_get_utc(hal_gps_utc_t *out_utc) {
  if (out_utc == nullptr) {
    return HAL_EINVAL;
  }
  gps_ensure_mutex();
  hal_mutex_lock(s_mutex);
  const bool available = GPS_SOURCE(gps_injected_utc_locked(out_utc),
                                    gps_parsed_utc_locked(out_utc));
  hal_mutex_unlock(s_mutex);
  return available ? HAL_OK : HAL_EAGAIN;
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

void hal_gps_engine_mock_set_utc(const hal_gps_utc_t *utc) {
  GPS_MOCK_SET(s_mock.utc_available = utc != nullptr;
               s_mock.utc = utc != nullptr ? *utc : hal_gps_utc_t{});
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
  gps_reset_parser_locked();
  s_mock = {};
  s_mock_injection_active = true;
  hal_mutex_unlock(s_mutex);
}

/* Test-only: force the singleton mutex through a real destroy so
 * Helgrind/DRD can observe the teardown path. Firmware never calls this -
 * the mutex is a process-lifetime singleton by design. */
void hal_mock_gps_engine_full_reset(void) {
  if (s_mutex != nullptr) {
    hal_mutex_destroy(s_mutex);
    s_mutex = nullptr;
  }
}
#endif

#endif /* HAL_ENABLE_GPS */
