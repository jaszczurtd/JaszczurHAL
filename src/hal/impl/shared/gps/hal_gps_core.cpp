#include "../../../hal_target.h"
#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474

#include "../../../hal_config.h"
#ifdef HAL_ENABLE_GPS

/* Shared GPS engine: wraps the portable gps_nmea_parser with a mutex and
 * hal_millis()-based position age, and implements every transport-independent
 * hal_gps_* getter plus hal_gps_encode(). The per-backend files supply only
 * the serial transport. */

#include "../../../hal_gps.h"
#include "../../../hal_sync.h"
#include "../../../hal_system.h"   /* hal_millis() */
#include "hal_gps_core.h"
#include "gps_nmea_parser.h"

static gps_nmea_t  s_p;
static hal_mutex_t s_mutex = nullptr;
static uint32_t    s_last_seq = 0;     /* last seen parser commit sequence   */
static uint32_t    s_commit_ms = 0;    /* hal_millis() at last location fix   */

static void gps_ensure_mutex(void) {
    if (s_mutex == nullptr) {
        hal_critical_section_enter();
        if (s_mutex == nullptr) {
            s_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

void hal_gps_engine_reset(void) {
    gps_ensure_mutex();
    hal_mutex_lock(s_mutex);
    gps_nmea_init(&s_p);
    s_last_seq = 0;
    s_commit_ms = 0;
    hal_mutex_unlock(s_mutex);
}

void hal_gps_encode(char c) {
    gps_ensure_mutex();
    hal_mutex_lock(s_mutex);
    gps_nmea_encode(&s_p, c);
    if (s_p.loc_commit_seq != s_last_seq) {   /* a new location was committed */
        s_last_seq = s_p.loc_commit_seq;
        s_commit_ms = hal_millis();
    }
    hal_mutex_unlock(s_mutex);
}

/* Lock, evaluate @p expr against s_p, unlock, return it. */
#define GPS_GET(type, expr) \
    gps_ensure_mutex(); hal_mutex_lock(s_mutex); \
    type _v = (expr); hal_mutex_unlock(s_mutex); return _v

bool hal_gps_location_is_valid(void)   { GPS_GET(bool, s_p.loc_valid); }
bool hal_gps_location_is_updated(void) { GPS_GET(bool, s_p.loc_updated); }

uint32_t hal_gps_location_age(void) {
    gps_ensure_mutex();
    hal_mutex_lock(s_mutex);
    uint32_t v = s_p.loc_valid ? (hal_millis() - s_commit_ms) : UINT32_MAX;
    hal_mutex_unlock(s_mutex);
    return v;
}

double hal_gps_latitude(void) {
    gps_ensure_mutex();
    hal_mutex_lock(s_mutex);
    s_p.loc_updated = false;            /* mirror TinyGPS++: reading clears it */
    double v = gps_nmea_latitude(&s_p);
    hal_mutex_unlock(s_mutex);
    return v;
}

double hal_gps_longitude(void) {
    gps_ensure_mutex();
    hal_mutex_lock(s_mutex);
    s_p.loc_updated = false;
    double v = gps_nmea_longitude(&s_p);
    hal_mutex_unlock(s_mutex);
    return v;
}

double hal_gps_speed_kmph(void)  { GPS_GET(double, gps_nmea_speed_kmph(&s_p)); }
double hal_gps_altitude_m(void)  { GPS_GET(double, gps_nmea_altitude_m(&s_p)); }
double hal_gps_course_deg(void)  { GPS_GET(double, gps_nmea_course_deg(&s_p)); }
double hal_gps_hdop(void)        { GPS_GET(double, gps_nmea_hdop(&s_p)); }
double hal_gps_vdop(void)        { GPS_GET(double, gps_nmea_vdop(&s_p)); }
double hal_gps_pdop(void)        { GPS_GET(double, gps_nmea_pdop(&s_p)); }
double hal_gps_horizontal_accuracy_m(void) { GPS_GET(double, gps_nmea_horizontal_accuracy_m(&s_p)); }
uint32_t hal_gps_satellites_used(void)   { GPS_GET(uint32_t, s_p.sats_used); }
uint8_t  hal_gps_satellites_in_view(void){ GPS_GET(uint8_t, gps_nmea_satellites_in_view(&s_p)); }
uint8_t  hal_gps_fix_quality(void)       { GPS_GET(uint8_t, s_p.fix_quality); }
uint8_t  hal_gps_fix_mode(void)          { GPS_GET(uint8_t, s_p.fix_mode); }

int hal_gps_date_year(void)   { GPS_GET(int, gps_nmea_year(&s_p)); }
int hal_gps_date_month(void)  { GPS_GET(int, gps_nmea_month(&s_p)); }
int hal_gps_date_day(void)    { GPS_GET(int, gps_nmea_day(&s_p)); }
int hal_gps_time_hour(void)   { GPS_GET(int, gps_nmea_hour(&s_p)); }
int hal_gps_time_minute(void) { GPS_GET(int, gps_nmea_minute(&s_p)); }
int hal_gps_time_second(void) { GPS_GET(int, gps_nmea_second(&s_p)); }

uint32_t hal_gps_chars_processed(void)    { GPS_GET(uint32_t, s_p.chars_processed); }
uint32_t hal_gps_passed_checksum(void)    { GPS_GET(uint32_t, s_p.passed_checksum); }
uint32_t hal_gps_failed_checksum(void)    { GPS_GET(uint32_t, s_p.failed_checksum); }
uint32_t hal_gps_sentences_with_fix(void) { GPS_GET(uint32_t, s_p.sentences_with_fix); }

#endif /* HAL_ENABLE_GPS */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
