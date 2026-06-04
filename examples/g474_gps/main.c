/**
 * @file main.c
 * @brief STM32G474 (Nucleo-G474RE) GPS reader - live NMEA over USART1.
 *
 * The STM32 counterpart of the RP2040 `07_gps` sketch: it reads a real GPS
 * receiver through the hardware UART and prints the decoded fix. Parsing is the
 * shared portable engine (impl/shared/gps_nmea_parser + hal_gps_core).
 *
 * Wiring: GPS TX -> PA10 (USART1 RX), GPS RX -> PA9 (USART1 TX, optional),
 *         common GND, GPS VCC to 3V3. Console on USART2 / ST-Link VCP @ 115200.
 *
 * Note: hal_gps_update() must be called often (it polls the UART), so the loop
 * keeps polling and only throttles the *reporting* to ~1 Hz.
 */

#include <hal/hal_serial.h>
#include <hal/hal_system.h>
#include <hal/hal_gps.h>

/* JaszczurHAL pin ids (port*16 + pin): PA10 = USART1 RX, PA9 = USART1 TX. */
#define GPS_RX_PIN 10u
#define GPS_TX_PIN 9u
#define GPS_BAUD   9600u

static void print_i32(int32_t v) {
    char buf[12];
    int i = (int)sizeof(buf) - 1;
    buf[i] = '\0';
    bool neg = v < 0;
    uint32_t u = neg ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    do { buf[--i] = (char)('0' + (u % 10u)); u /= 10u; } while (u && i > 0);
    if (neg && i > 0) buf[--i] = '-';
    hal_serial_print(&buf[i]);
}

static void field(const char *label, int32_t value) {
    hal_serial_print(label);
    print_i32(value);
    hal_serial_println("");
}

int main(void) {
    hal_serial_begin(115200);
    hal_serial_println("");
    hal_serial_println("=== JaszczurHAL G474 GPS reader (USART1 @ 9600) ===");
    hal_serial_println("Wiring: GPS TX -> PA10, GND -> GND, VCC -> 3V3");

    hal_gps_init(GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD, HAL_UART_CFG_8N1);

    uint32_t last_report = 0;
    for (;;) {
        hal_gps_update();              /* poll the UART into the parser */

        const uint32_t now = hal_millis();
        if (now - last_report < 1000u) {
            continue;                  /* keep polling; only report at ~1 Hz */
        }
        last_report = now;

        if (!hal_gps_location_is_valid()) {
            hal_serial_print("waiting for fix: chars=");
            print_i32((int32_t)hal_gps_chars_processed());
            hal_serial_print(" ok=");
            print_i32((int32_t)hal_gps_passed_checksum());
            hal_serial_print(" fail=");
            print_i32((int32_t)hal_gps_failed_checksum());
            hal_serial_println("");
            continue;
        }

        field("  lat (1e7 deg)   : ", (int32_t)(hal_gps_latitude()  * 1e7));
        field("  lon (1e7 deg)   : ", (int32_t)(hal_gps_longitude() * 1e7));
        field("  altitude (cm)   : ", (int32_t)(hal_gps_altitude_m() * 100.0));
        field("  speed (x100kmph): ", (int32_t)(hal_gps_speed_kmph() * 100.0));
        field("  course (x100)   : ", (int32_t)(hal_gps_course_deg() * 100.0));
        field("  sats used       : ", (int32_t)hal_gps_satellites_used());
        field("  sats in view    : ", (int32_t)hal_gps_satellites_in_view());
        field("  fix quality     : ", (int32_t)hal_gps_fix_quality());
        field("  fix mode        : ", (int32_t)hal_gps_fix_mode());
        field("  hdop (x100)     : ", (int32_t)(hal_gps_hdop() * 100.0));
        field("  h-accuracy (cm) : ", (int32_t)(hal_gps_horizontal_accuracy_m() * 100.0));
        field("  age (ms)        : ", (int32_t)hal_gps_location_age());
        hal_serial_println("");
    }
}
