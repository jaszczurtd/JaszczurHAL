#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/gps/hal_gps.h>
#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_uart.h>
#include <hal/system/hal_system.h>
#include <stdio.h>

#if HAL_TARGET_IS_RP
#define GPS_RX_PIN 1u
#define GPS_TX_PIN 0u
#define ECHO_RX_PIN 5u
#define ECHO_TX_PIN 4u
#else
#define GPS_RX_PIN 10u
#define GPS_TX_PIN 9u
#endif

#define GPS_BAUD 9600u
#define ECHO_BAUD 115200u
#define REPORT_PERIOD_MS 1000u
#define ECHO_TX_PERIOD_MS 2000u

static uint32_t s_last_report_ms = 0u;
#if HAL_TARGET_IS_RP
static hal_uart_t s_echo_uart = NULL;
static uint32_t s_last_echo_tx_ms = 0u;
static uint32_t s_echo_line = 0u;

static void init_uart_echo(void) {
  s_echo_uart = hal_uart_create(HAL_UART_PORT_2, ECHO_RX_PIN, ECHO_TX_PIN);
  if (s_echo_uart == NULL) {
    derr("UART echo unavailable");
    return;
  }

  const hal_status_t status =
      hal_uart_begin(s_echo_uart, ECHO_BAUD, HAL_UART_CFG_8N1);
  if (status != HAL_OK) {
    derr("UART echo init failed: %s", hal_status_to_string(status));
    hal_uart_destroy(s_echo_uart);
    s_echo_uart = NULL;
    return;
  }

  const hal_status_t write_status =
      hal_uart_println_ex(s_echo_uart, "JaszczurHAL UART echo ready", NULL);
  if (write_status != HAL_OK) {
    derr("UART echo initial write failed: %s",
         hal_status_to_string(write_status));
  }
}

static void service_uart_echo(uint32_t now) {
  if (s_echo_uart == NULL) {
    return;
  }

  while (hal_uart_available(s_echo_uart) > 0) {
    uint8_t value = 0u;
    hal_status_t status = hal_uart_read_ex(s_echo_uart, &value);
    if (status == HAL_EAGAIN) {
      break;
    }
    if (status != HAL_OK) {
      derr("UART echo read failed: %s", hal_status_to_string(status));
      break;
    }

    size_t written = 0u;
    status = hal_uart_write_ex(s_echo_uart, &value, 1u, &written);
    if (status != HAL_OK || written != 1u) {
      derr("UART echo write failed: %s", hal_status_to_string(status));
      break;
    }
  }

  if ((uint32_t)(now - s_last_echo_tx_ms) < ECHO_TX_PERIOD_MS) {
    return;
  }
  s_last_echo_tx_ms = now;

  char line[48] = {0};
  snprintf(line, sizeof(line), "UART echo heartbeat %lu",
           (unsigned long)s_echo_line++);
  const hal_status_t status = hal_uart_println_ex(s_echo_uart, line, NULL);
  if (status != HAL_OK) {
    derr("UART echo heartbeat failed: %s", hal_status_to_string(status));
  }
}
#endif

static void report_gps(uint32_t now) {
  if ((uint32_t)(now - s_last_report_ms) < REPORT_PERIOD_MS) {
    return;
  }
  s_last_report_ms = now;

  if (hal_gps_location_is_valid()) {
    deb("GPS: %.6f, %.6f speed=%.1f km/h sats=%lu", hal_gps_latitude(),
        hal_gps_longitude(), hal_gps_speed_kmph(),
        (unsigned long)hal_gps_satellites_used());
  } else {
    deb("GPS: waiting for fix, chars=%lu valid=%lu failed=%lu",
        (unsigned long)hal_gps_chars_processed(),
        (unsigned long)hal_gps_passed_checksum(),
        (unsigned long)hal_gps_failed_checksum());
  }
}

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL hardware UART and GPS ===");

  hal_gps_init(GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD, HAL_UART_CFG_8N1);
#if HAL_TARGET_IS_RP
  init_uart_echo();
#else
  deb("STM32G474: GPS uses USART1; USART2 remains reserved for debug output");
#endif
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  hal_gps_update();
#if HAL_TARGET_IS_RP
  service_uart_echo(now);
#endif
  report_gps(now);
  hal_delay_ms(10u);
}
