#include "hal_config.h"
#include "hal_target.h"

#ifdef HAL_ENABLE_GPS

/* Portable GPS transport facade. The NMEA engine and public data getters live
 * in impl/shared/frameworks/gps/hal_gps_core.cpp. */

#include "hal_gps.h"
#include "hal_serial.h"
#include "impl/shared/frameworks/gps/hal_gps_core.h"

#if HAL_TARGET_IS_MOCK
#define JH_GPS_TRANSPORT_MOCK 1
#elif defined(HAL_GPS_TRANSPORT_UART)
#define JH_GPS_TRANSPORT_UART 1
#elif defined(HAL_GPS_TRANSPORT_SWSERIAL)
#define JH_GPS_TRANSPORT_SWSERIAL 1
#elif defined(HAL_ENABLE_SWSERIAL)
#define JH_GPS_TRANSPORT_SWSERIAL 1
#elif defined(HAL_ENABLE_UART)
#define JH_GPS_TRANSPORT_UART 1
#else
#error "HAL_ENABLE_GPS needs HAL_ENABLE_SWSERIAL or HAL_ENABLE_UART"
#endif

#if defined(JH_GPS_TRANSPORT_SWSERIAL)

#include "hal_swserial.h"

static constexpr uint32_t GPS_AUTODETECT_CHARS = 500u;

static hal_swserial_t s_serial = nullptr;
static uint8_t s_rx_pin = 0u;
static uint8_t s_tx_pin = 0u;
static uint32_t s_baud = 0u;
static uint16_t s_config = HAL_GPS_DEFAULT_UART_CONFIG;
static bool s_autodetect_done = false;

static void gps_reinit_serial(uint16_t config) {
  if (s_serial) {
    hal_swserial_destroy(s_serial);
    s_serial = nullptr;
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
    s_serial = nullptr;
  }
}

void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud,
                  uint16_t config) {
  if (s_serial) {
    return;
  }

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
    s_serial = nullptr;
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

  if (!s_autodetect_done && hal_gps_chars_processed() >= GPS_AUTODETECT_CHARS) {
    if (hal_gps_passed_checksum() == 0u && hal_gps_failed_checksum() > 0u) {
      const uint16_t alternate =
          (s_config == HAL_UART_CFG_8N1) ? HAL_UART_CFG_7N1 : HAL_UART_CFG_8N1;
      hal_deb("gps: 0 passed / %lu failed with 0x%04X, switching to 0x%04X",
              (unsigned long)hal_gps_failed_checksum(), (unsigned)s_config,
              (unsigned)alternate);
      hal_gps_engine_reset();
      gps_reinit_serial(alternate);
    }
    s_autodetect_done = true;
  }
}

int hal_gps_serial_available(void) {
  return s_serial ? hal_swserial_available(s_serial) : -1;
}

#elif defined(JH_GPS_TRANSPORT_UART)

#include "hal_uart.h"

#ifndef HAL_GPS_UART_PORT
#define HAL_GPS_UART_PORT HAL_UART_PORT_1
#endif

static hal_uart_t s_uart = nullptr;

void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud,
                  uint16_t config) {
  if (s_uart) {
    return;
  }

  hal_gps_engine_reset();
  s_uart = hal_uart_create(HAL_GPS_UART_PORT, rx_pin, tx_pin);
  if (!s_uart) {
    hal_derr_limited("gps", "init failed: uart create returned NULL");
    return;
  }

  const hal_status_t status = hal_uart_begin(s_uart, baud, config);
  if (status != HAL_OK) {
    hal_derr_limited("gps", "init failed: uart begin: %s",
                     hal_status_to_string(status));
    hal_uart_destroy(s_uart);
    s_uart = nullptr;
  }
}

void hal_gps_update(void) {
  if (!s_uart) {
    hal_derr_limited("gps", "update failed: uart not initialized");
    return;
  }

  while (hal_uart_available(s_uart) > 0) {
    const int value = hal_uart_read(s_uart);
    if (value < 0) {
      break;
    }
    hal_gps_encode((char)value);
  }
}

int hal_gps_serial_available(void) {
  return s_uart ? hal_uart_available(s_uart) : -1;
}

#elif defined(JH_GPS_TRANSPORT_MOCK)

void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud,
                  uint16_t config) {
  (void)rx_pin;
  (void)tx_pin;
  (void)baud;
  (void)config;
  hal_gps_engine_reset();
}

void hal_gps_update(void) {}

int hal_gps_serial_available(void) { return 0; }

#endif

#endif /* HAL_ENABLE_GPS */
