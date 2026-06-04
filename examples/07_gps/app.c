#include <hal/hal_app.h>
#include <hal/hal_gps.h>
#include <hal/hal_serial.h>
#include <hal/hal_system.h>

static const uint8_t GPS_RX_PIN = 5;
static const uint8_t GPS_TX_PIN = 4;
static const uint32_t GPS_BAUD = 9600;
static uint32_t last_report_ms = 0;

void app_start(void) {
    hal_debug_init(115200, NULL);
    hal_gps_init(GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD, HAL_UART_CFG_8N1);
}

void app_task0(void) {
    hal_gps_update();

    const uint32_t now = hal_millis();
    if (now - last_report_ms < 1000u) {
        return;
    }
    last_report_ms = now;

    if (!hal_gps_location_is_valid()) {
        hal_deb("GPS: waiting for fix, chars=%lu, ok=%lu, fail=%lu",
                (unsigned long)hal_gps_chars_processed(),
                (unsigned long)hal_gps_passed_checksum(),
                (unsigned long)hal_gps_failed_checksum());
        return;
    }

    hal_deb("GPS: lat=%.6f lon=%.6f speed=%.2f km/h age=%lu ms",
            hal_gps_latitude(),
            hal_gps_longitude(),
            hal_gps_speed_kmph(),
            (unsigned long)hal_gps_location_age());
}
