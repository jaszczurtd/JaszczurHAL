#include <hal/hal_app.h>
#include <hal/hal_littlefs.h>
#include <hal/hal_system.h>
#include <tools_c.h>

static const char *MARKER_PATH = "/hal_marker.txt";
static uint32_t last_report_ms = 0;

void app_start(void) {
  debugInit();

  if (!hal_littlefs_begin()) {
    derr("LittleFS mount failed, formatting");
    if (!hal_littlefs_format() || !hal_littlefs_begin()) {
      derr("LittleFS unavailable");
      return;
    }
  }

  deb("LittleFS mounted: total=%lu used=%lu",
      (unsigned long)hal_littlefs_total_bytes(),
      (unsigned long)hal_littlefs_used_bytes());

  if (hal_littlefs_exists(MARKER_PATH)) {
    deb("LittleFS marker exists, removing");
    hal_littlefs_remove(MARKER_PATH);
  } else {
    deb("LittleFS marker not present");
  }
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if (now - last_report_ms < 5000u) {
    return;
  }
  last_report_ms = now;

  if (!hal_littlefs_is_mounted()) {
    derr("LittleFS not mounted");
    return;
  }

  deb("LittleFS: total=%lu used=%lu marker=%s",
      (unsigned long)hal_littlefs_total_bytes(),
      (unsigned long)hal_littlefs_used_bytes(),
      hal_littlefs_exists(MARKER_PATH) ? "yes" : "no");
}
