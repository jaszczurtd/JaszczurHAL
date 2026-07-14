#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#ifdef HAL_ENABLE_GPS

/* RP2040 GPS backend: serial transport only. All NMEA parsing and the
 * hal_gps_* getters live in the shared engine
 * (impl/shared/frameworks/gps/hal_gps_core.cpp); this file just pumps received
 * bytes into it.
 *
 * The transport is not fixed to SoftwareSerial - the receiver can be read over
 * either a hardware UART (hal_uart) or SoftwareSerial (hal_swserial). Selection
 * is compile-time:
 *   - force with HAL_GPS_TRANSPORT_UART / HAL_GPS_TRANSPORT_SWSERIAL, otherwise
 *   - default to SoftwareSerial when enabled (backward compatible), else UART.
 * The 8N1<->7N1 auto-detect applies to the SoftwareSerial path only. */

#include "../../hal_gps.h"
#include "../../hal_serial.h"
#include "../shared/frameworks/gps/hal_gps_core.h"

#if defined(HAL_GPS_TRANSPORT_UART)
#define GPS_RP2040_UART 1
#elif defined(HAL_GPS_TRANSPORT_SWSERIAL)
#define GPS_RP2040_SWSERIAL 1
#elif defined(HAL_ENABLE_SWSERIAL)
#define GPS_RP2040_SWSERIAL 1 /* default: SoftwareSerial */
#elif defined(HAL_ENABLE_UART)
#define GPS_RP2040_UART 1
#else
#error "HAL_ENABLE_GPS needs HAL_ENABLE_SWSERIAL or HAL_ENABLE_UART"
#endif

/* ───────────────────────────── SoftwareSerial ───────────────────────────── */
#ifdef GPS_RP2040_SWSERIAL
#include "../../hal_swserial.h"

static hal_swserial_t s_serial = NULL;

#define GPS_AUTODETECT_CHARS 500
static uint8_t s_rx_pin;
static uint8_t s_tx_pin;
static uint32_t s_baud;
static uint16_t s_config;
static bool s_autodetect_done = false;

static void gps_reinit_serial(uint16_t config) {
  if (s_serial) {
    hal_swserial_destroy(s_serial);
    s_serial = NULL;
  }
  s_config = config;
  hal_status_t status = hal_swserial_create_ex(s_rx_pin, s_tx_pin, &s_serial);
  if (status != HAL_OK) {
    hal_derr_limited("gps", "reinit failed: swserial create: %s",
                     hal_status_to_string(status));
    return;
  }
  status = hal_swserial_begin(s_serial, s_baud, s_config);
  if (status != HAL_OK) {
    hal_derr_limited("gps", "reinit failed: swserial begin: %s",
                     hal_status_to_string(status));
    hal_swserial_destroy(s_serial);
    s_serial = NULL;
  }
}

void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud,
                  uint16_t config) {
  if (s_serial)
    return;
  s_rx_pin = rx_pin;
  s_tx_pin = tx_pin;
  s_baud = baud;
  s_config = config;
  s_autodetect_done = false;
  hal_gps_engine_reset();

  hal_status_t status = hal_swserial_create_ex(rx_pin, tx_pin, &s_serial);
  if (status != HAL_OK) {
    hal_derr_limited("gps", "init failed: swserial create: %s",
                     hal_status_to_string(status));
    return;
  }
  status = hal_swserial_begin(s_serial, baud, config);
  if (status != HAL_OK) {
    hal_derr_limited("gps", "init failed: swserial begin: %s",
                     hal_status_to_string(status));
    hal_swserial_destroy(s_serial);
    s_serial = NULL;
  }
}

void hal_gps_update(void) {
  if (!s_serial) {
    hal_derr_limited("gps", "update failed: swserial not initialized");
    return;
  }
  while (hal_swserial_available(s_serial) > 0) {
    uint8_t value = 0u;
    const hal_status_t status = hal_swserial_read_ex(s_serial, &value);
    if (status == HAL_OK) {
      hal_gps_encode((char)value);
      continue;
    }
    if (status != HAL_EAGAIN) {
      hal_derr_limited("gps", "swserial read failed: %s",
                       hal_status_to_string(status));
    }
    break;
  }

  /* Auto-detect: if after GPS_AUTODETECT_CHARS bytes every sentence failed
     checksum, toggle between 8N1 and 7N1 and retry once. */
  if (!s_autodetect_done && hal_gps_chars_processed() >= GPS_AUTODETECT_CHARS) {
    if (hal_gps_passed_checksum() == 0 && hal_gps_failed_checksum() > 0) {
      uint16_t alt =
          (s_config == HAL_UART_CFG_8N1) ? HAL_UART_CFG_7N1 : HAL_UART_CFG_8N1;
      hal_deb("gps: 0 passed / %lu failed with 0x%04X, switching to 0x%04X",
              (unsigned long)hal_gps_failed_checksum(), (unsigned)s_config,
              (unsigned)alt);
      hal_gps_engine_reset();
      gps_reinit_serial(alt);
    }
    s_autodetect_done = true;
  }
}

int hal_gps_serial_available(void) {
  return s_serial ? hal_swserial_available(s_serial) : -1;
}
#endif /* GPS_RP2040_SWSERIAL */

/* ──────────────────────────────── UART ──────────────────────────────────── */
#ifdef GPS_RP2040_UART
#include "../../hal_uart.h"

#ifndef HAL_GPS_UART_PORT
#define HAL_GPS_UART_PORT HAL_UART_PORT_1
#endif

static hal_uart_t s_uart = NULL;

void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud,
                  uint16_t config) {
  if (s_uart)
    return;
  hal_gps_engine_reset();
  s_uart = hal_uart_create(HAL_GPS_UART_PORT, rx_pin, tx_pin);
  if (!s_uart) {
    hal_derr_limited("gps", "init failed: uart create returned NULL");
    return;
  }
  hal_uart_begin(s_uart, baud, config);
}

void hal_gps_update(void) {
  if (!s_uart) {
    hal_derr_limited("gps", "update failed: uart not initialized");
    return;
  }
  while (hal_uart_available(s_uart) > 0) {
    int b = hal_uart_read(s_uart);
    if (b < 0)
      break;
    hal_gps_encode((char)b);
  }
}

int hal_gps_serial_available(void) {
  return s_uart ? hal_uart_available(s_uart) : -1;
}
#endif /* GPS_RP2040_UART */

#endif /* HAL_ENABLE_GPS */
#endif // HAL_TARGET_IS_RP2040
