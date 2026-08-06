#include <hal/hal_app.h>
#include <hal/hal_gps.h>
#include <hal/hal_swserial.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <stdio.h>
#include <tools_c.h>

#if !HAL_TARGET_IS_RP
#error "The software-serial GPS variant is supported only on RP targets"
#endif

#if !defined(EXAMPLE_SERIAL_GPS_USE_SWSERIAL)
#error "Define EXAMPLE_SERIAL_GPS_USE_SWSERIAL for this variant"
#endif

#define GPS_RX_PIN 5u
#define GPS_TX_PIN 4u
#define ECHO_RX_PIN 9u
#define ECHO_TX_PIN 8u
#define SERIAL_BAUD 9600u
#define REPORT_PERIOD_MS 1000u
#define ECHO_TX_PERIOD_MS 2000u

static hal_swserial_t s_echo_serial = NULL;
static uint32_t s_last_report_ms = 0u;
static uint32_t s_last_echo_tx_ms = 0u;
static uint32_t s_echo_line = 0u;

static void init_swserial_echo(void) {
  hal_status_t status =
      hal_swserial_create_ex(ECHO_RX_PIN, ECHO_TX_PIN, &s_echo_serial);
  if (status != HAL_OK) {
    derr("Software-serial echo unavailable: %s", hal_status_to_string(status));
    return;
  }

  status = hal_swserial_begin(s_echo_serial, SERIAL_BAUD, HAL_UART_CFG_8N1);
  if (status != HAL_OK) {
    derr("Software-serial echo init failed: %s", hal_status_to_string(status));
    hal_swserial_destroy(s_echo_serial);
    s_echo_serial = NULL;
    return;
  }

  status = hal_swserial_println_ex(
      s_echo_serial, "JaszczurHAL software-serial echo ready", NULL);
  if (status != HAL_OK) {
    derr("Software-serial initial write failed: %s",
         hal_status_to_string(status));
  }
}

static void service_swserial_echo(uint32_t now) {
  if (s_echo_serial == NULL) {
    return;
  }

  while (hal_swserial_available(s_echo_serial) > 0) {
    uint8_t value = 0u;
    hal_status_t status = hal_swserial_read_ex(s_echo_serial, &value);
    if (status == HAL_EAGAIN) {
      break;
    }
    if (status != HAL_OK) {
      derr("Software-serial read failed: %s", hal_status_to_string(status));
      break;
    }

    size_t written = 0u;
    status = hal_swserial_write_ex(s_echo_serial, &value, 1u, &written);
    if (status != HAL_OK || written != 1u) {
      derr("Software-serial write failed: %s", hal_status_to_string(status));
      break;
    }
  }

  if ((uint32_t)(now - s_last_echo_tx_ms) < ECHO_TX_PERIOD_MS) {
    return;
  }
  s_last_echo_tx_ms = now;

  char line[56] = {0};
  snprintf(line, sizeof(line), "Software-serial heartbeat %lu",
           (unsigned long)s_echo_line++);
  const hal_status_t status =
      hal_swserial_println_ex(s_echo_serial, line, NULL);
  if (status != HAL_OK) {
    derr("Software-serial heartbeat failed: %s", hal_status_to_string(status));
  }
}

static void report_gps(uint32_t now) {
  if ((uint32_t)(now - s_last_report_ms) < REPORT_PERIOD_MS) {
    return;
  }
  s_last_report_ms = now;

  if (hal_gps_location_is_valid()) {
    deb("GPS/SWSERIAL: %.6f, %.6f speed=%.1f km/h sats=%lu", hal_gps_latitude(),
        hal_gps_longitude(), hal_gps_speed_kmph(),
        (unsigned long)hal_gps_satellites_used());
  } else {
    deb("GPS/SWSERIAL: waiting for fix, chars=%lu valid=%lu failed=%lu",
        (unsigned long)hal_gps_chars_processed(),
        (unsigned long)hal_gps_passed_checksum(),
        (unsigned long)hal_gps_failed_checksum());
  }
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL software serial and GPS ===");

  hal_gps_init(GPS_RX_PIN, GPS_TX_PIN, SERIAL_BAUD, HAL_UART_CFG_8N1);
  init_swserial_echo();
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  hal_gps_update();
  service_swserial_echo(now);
  report_gps(now);
  hal_delay_ms(10u);
}
