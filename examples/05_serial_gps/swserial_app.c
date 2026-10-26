/*
 * Read GPS data and run a separate loopback/echo test using software serial.
 * This RP-only variant selects software serial for the GPS module as well.
 */

#include <hal/core/hal_config.h>

#if defined(EXAMPLE_SERIAL_GPS_USE_SWSERIAL)

#include "gps_report.h"

#include <hal/core/hal_app.h>
#include <hal/gps/hal_gps.h>
#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_swserial.h>
#include <hal/system/hal_system.h>
#include <stdio.h>

/* Software serial drives the same lines as the base build: the GPS on the
 * gpsUart pins and the test port on the testUart pins of device_tree.json. */
#define ECHO_BAUD 9600u
#define ECHO_TX_PERIOD_MS 2000u

static hal_swserial_t s_echo_serial = NULL;
static uint32_t s_last_echo_tx_ms = 0u;
static uint32_t s_echo_line = 0u;

static void init_swserial_echo(void) {
  hal_status_t status = hal_swserial_create_ex(
      JH_HW_BUS_TEST_UART_PIN_RX, JH_HW_BUS_TEST_UART_PIN_TX, &s_echo_serial);
  if (status != HAL_OK) {
    derr("Software-serial echo unavailable: %s", hal_status_to_string(status));
    return;
  }

  status = hal_swserial_begin(s_echo_serial, ECHO_BAUD, HAL_UART_CFG_8N1);
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

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL software serial and GPS ===");

  hal_gps_init(JH_HW_BUS_GPS_UART_PIN_RX, JH_HW_BUS_GPS_UART_PIN_TX,
               JH_HW_BUS_GPS_UART_BAUD_RATE, JH_HW_BUS_GPS_UART_FRAME_CONFIG);
  init_swserial_echo();
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  hal_gps_update();
  service_swserial_echo(now);
  gps_report(now, "GPS/SWSERIAL");
  hal_delay_ms(10u);
}

#endif /* defined(EXAMPLE_SERIAL_GPS_USE_SWSERIAL) */
