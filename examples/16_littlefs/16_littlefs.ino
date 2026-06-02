#include <JaszczurHAL.h>

static const char *MARKER_PATH = "/hal_marker.txt";
static uint32_t last_report_ms = 0;

void setup() {
  hal_debug_init(115200);

  if (!hal_littlefs_begin()) {
    hal_derr("LittleFS mount failed");
    return;
  }

  hal_deb("LittleFS mounted: total=%lu used=%lu",
          (unsigned long)hal_littlefs_total_bytes(),
          (unsigned long)hal_littlefs_used_bytes());

  if (hal_littlefs_exists(MARKER_PATH)) {
    hal_deb("LittleFS marker exists, removing");
    hal_littlefs_remove(MARKER_PATH);
  } else {
    hal_deb("LittleFS marker not present");
  }
}

void loop() {
  const uint32_t now = hal_millis();
  if (now - last_report_ms < 5000u) {
    return;
  }
  last_report_ms = now;

  if (!hal_littlefs_is_mounted()) {
    hal_derr("LittleFS not mounted");
    return;
  }

  hal_deb("LittleFS: total=%lu used=%lu marker=%s",
          (unsigned long)hal_littlefs_total_bytes(),
          (unsigned long)hal_littlefs_used_bytes(),
          hal_littlefs_exists(MARKER_PATH) ? "yes" : "no");
}
