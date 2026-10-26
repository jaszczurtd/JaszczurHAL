/*
 * GPS status line shared by the hardware UART and software serial variants.
 */

#include "gps_report.h"

#include <hal/gps/hal_gps.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#define REPORT_PERIOD_MS 1000u
/* A time older than this no longer names the current second. */
#define UTC_MAX_AGE_MS 2000u

static uint32_t s_last_report_ms = 0u;

static void report_utc(const char *label) {
  hal_gps_utc_t utc;
  if (hal_gps_get_utc(&utc) != HAL_OK) {
    deb("%s: no UTC time yet", label);
    return;
  }
  deb("%s: UTC %04u-%02u-%02u %02u:%02u:%02u, %lu ms old%s", label,
      (unsigned)utc.year, (unsigned)utc.month, (unsigned)utc.day,
      (unsigned)utc.hour, (unsigned)utc.minute, (unsigned)utc.second,
      (unsigned long)utc.age_ms, utc.age_ms > UTC_MAX_AGE_MS ? " (stale)" : "");
}

void gps_report(uint32_t now, const char *label) {
  if (!hal_millis_interval_elapsed(now, &s_last_report_ms, REPORT_PERIOD_MS)) {
    return;
  }

  if (hal_gps_location_is_valid()) {
    deb("%s: %.6f, %.6f speed=%.1f km/h sats=%lu", label, hal_gps_latitude(),
        hal_gps_longitude(), hal_gps_speed_kmph(),
        (unsigned long)hal_gps_satellites_used());
  } else {
    deb("%s: waiting for fix, chars=%lu valid=%lu failed=%lu", label,
        (unsigned long)hal_gps_chars_processed(),
        (unsigned long)hal_gps_passed_checksum(),
        (unsigned long)hal_gps_failed_checksum());
  }
  report_utc(label);
}
