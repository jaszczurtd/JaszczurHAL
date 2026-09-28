#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"

#ifdef HAL_ENABLE_GPS

/* Portable GPS transport facade. The NMEA engine and public data getters live
 * in hal/gps/hal_gps_core.cpp. */

#include "hal/core/hal_compiler.h"
#include "hal/gps/hal_gps.h"
#include "hal/gps/hal_gps_core.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"

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

#include "hal/serial/hal_swserial.h"

static constexpr uint32_t GPS_AUTODETECT_CHARS = 500u;

static hal_swserial_t s_serial = nullptr;
static uint8_t s_rx_pin = 0u;
static uint8_t s_tx_pin = 0u;
static uint32_t s_baud = 0u;
static uint16_t s_config = HAL_GPS_DEFAULT_UART_CONFIG;
static bool s_autodetect_done = false;
static bool s_initialized = false;
static bool s_paused = false;

static hal_status_t gps_reinit_serial(uint16_t config) {
  if (s_serial) {
    hal_swserial_destroy(s_serial);
    s_serial = nullptr;
  }

  s_config = config;
  hal_status_t status = hal_swserial_create_ex(s_rx_pin, s_tx_pin, &s_serial);
  if (status != HAL_OK) {
    hal_derr_limited("gps", "reinit failed: swserial create: %s",
                     hal_status_to_string(status));
    return status;
  }

  status = hal_swserial_begin(s_serial, s_baud, s_config);
  if (status != HAL_OK) {
    hal_derr_limited("gps", "reinit failed: swserial begin: %s",
                     hal_status_to_string(status));
    hal_swserial_destroy(s_serial);
    s_serial = nullptr;
  }
  return status;
}

static hal_status_t gps_reconfigure_serial(uint16_t config) {
  if (s_serial == nullptr) {
    return HAL_EUNINIT;
  }
  const hal_status_t status = hal_swserial_begin(s_serial, s_baud, config);
  if (status == HAL_OK) {
    s_config = config;
  } else {
    hal_derr_limited("gps", "reconfigure failed: swserial begin: %s",
                     hal_status_to_string(status));
  }
  return status;
}

static void gps_transport_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud,
                               uint16_t config) {
  if (s_initialized) {
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
    return;
  }
  s_initialized = true;
  s_paused = false;
}

static hal_status_t gps_transport_pause(void) {
  if (!s_initialized || s_paused) {
    return HAL_OK;
  }
  if (s_serial == nullptr) {
    return HAL_ESTATE;
  }
  hal_swserial_destroy(s_serial);
  s_serial = nullptr;
  s_paused = true;
  return HAL_OK;
}

static hal_status_t gps_transport_resume(void) {
  if (!s_initialized || !s_paused) {
    return HAL_OK;
  }
  const hal_status_t status = gps_reinit_serial(s_config);
  if (status == HAL_OK) {
    s_paused = false;
  }
  return status;
}

static void gps_transport_update(void) {
  if (s_paused) {
    return;
  }
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
      (void)gps_reconfigure_serial(alternate);
    }
    s_autodetect_done = true;
  }
}

static int gps_transport_available(void) {
  return s_serial ? hal_swserial_available(s_serial) : (s_paused ? 0 : -1);
}

#elif defined(JH_GPS_TRANSPORT_UART)

#include "hal/serial/hal_uart.h"

#ifndef HAL_GPS_UART_PORT
#define HAL_GPS_UART_PORT HAL_UART_PORT_1
#endif

static hal_uart_t s_uart = nullptr;
static uint8_t s_rx_pin = 0u;
static uint8_t s_tx_pin = 0u;
static uint32_t s_baud = 0u;
static uint16_t s_config = HAL_GPS_DEFAULT_UART_CONFIG;
static bool s_initialized = false;
static bool s_paused = false;

static hal_status_t gps_start_uart(void) {
  s_uart = hal_uart_create(HAL_GPS_UART_PORT, s_rx_pin, s_tx_pin);
  if (!s_uart) {
    return HAL_ENOMEM;
  }
  const hal_status_t status = hal_uart_begin(s_uart, s_baud, s_config);
  if (status != HAL_OK) {
    hal_uart_destroy(s_uart);
    s_uart = nullptr;
  }
  return status;
}

static void gps_transport_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud,
                               uint16_t config) {
  if (s_initialized) {
    return;
  }

  s_rx_pin = rx_pin;
  s_tx_pin = tx_pin;
  s_baud = baud;
  s_config = config;
  hal_gps_engine_reset();
  const hal_status_t status = gps_start_uart();
  if (status != HAL_OK) {
    hal_derr_limited("gps", "init failed: uart begin: %s",
                     hal_status_to_string(status));
    return;
  }
  s_initialized = true;
  s_paused = false;
}

static hal_status_t gps_transport_pause(void) {
  if (!s_initialized || s_paused) {
    return HAL_OK;
  }
  if (s_uart == nullptr) {
    return HAL_ESTATE;
  }
  hal_uart_destroy(s_uart);
  s_uart = nullptr;
  s_paused = true;
  return HAL_OK;
}

static hal_status_t gps_transport_resume(void) {
  if (!s_initialized || !s_paused) {
    return HAL_OK;
  }
  const hal_status_t status = gps_start_uart();
  if (status == HAL_OK) {
    s_paused = false;
  }
  return status;
}

static void gps_transport_update(void) {
  if (s_paused) {
    return;
  }
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

static int gps_transport_available(void) {
  return s_uart ? hal_uart_available(s_uart) : (s_paused ? 0 : -1);
}

#elif defined(JH_GPS_TRANSPORT_MOCK)

static bool s_initialized = false;
static bool s_paused = false;

static void gps_transport_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud,
                               uint16_t config) {
  (void)rx_pin;
  (void)tx_pin;
  (void)baud;
  (void)config;
  hal_gps_engine_reset();
  s_initialized = true;
  s_paused = false;
}

static hal_status_t gps_transport_pause(void) {
  if (s_initialized) {
    s_paused = true;
  }
  return HAL_OK;
}

static hal_status_t gps_transport_resume(void) {
  if (!s_initialized || !s_paused) {
    return HAL_OK;
  }
  s_paused = false;
  return HAL_OK;
}

static void gps_transport_update(void) {}

static int gps_transport_available(void) { return 0; }

#endif

/* No GPS call waits for another. One token guards the transport handle; a
 * call that finds it taken skips the transport. A pause or resume request left
 * that way is applied by the token holder before it lets go, so a pause from
 * one core never waits for hal_gps_update() on the other. */
static uint32_t s_owner = 0u;
static uint32_t s_want_paused = 0u;
static uint32_t s_requests = 0u;

/* Token holder only. A request that failed to apply is retried this often. */
static constexpr uint32_t GPS_REQUEST_RETRY_MS = 1000u;
static uint32_t s_applied_requests = 0u;
static bool s_retry = false;
static uint32_t s_retry_ms = 0u;

static bool gps_try_own(void) {
  uint32_t free_token = 0u;
  return HAL_ATOMIC_COMPARE_EXCHANGE(&s_owner, &free_token, 1u,
                                     HAL_ATOMIC_SEQ_CST, HAL_ATOMIC_SEQ_CST);
}

static hal_status_t gps_apply_request(void) {
  s_applied_requests = HAL_ATOMIC_LOAD(&s_requests, HAL_ATOMIC_SEQ_CST);
  const hal_status_t status =
      HAL_ATOMIC_LOAD(&s_want_paused, HAL_ATOMIC_SEQ_CST) != 0u
          ? gps_transport_pause()
          : gps_transport_resume();
  s_retry = status != HAL_OK;
  s_retry_ms = hal_millis();
  return status;
}

// Lets go of the token, first applying requests that arrived meanwhile.
static void gps_release(void) {
  for (;;) {
    if (HAL_ATOMIC_LOAD(&s_requests, HAL_ATOMIC_SEQ_CST) !=
            s_applied_requests ||
        (s_retry &&
         hal_millis_deadline_expired(s_retry_ms, GPS_REQUEST_RETRY_MS))) {
      (void)gps_apply_request();
    }
    const uint32_t applied = s_applied_requests;
    HAL_ATOMIC_STORE(&s_owner, 0u, HAL_ATOMIC_SEQ_CST);
    if (HAL_ATOMIC_LOAD(&s_requests, HAL_ATOMIC_SEQ_CST) == applied ||
        !gps_try_own()) {
      return;
    }
  }
}

static hal_status_t gps_request(bool paused) {
  HAL_ATOMIC_STORE(&s_want_paused, paused ? 1u : 0u, HAL_ATOMIC_SEQ_CST);
  HAL_ATOMIC_ADD_FETCH(&s_requests, 1u, HAL_ATOMIC_SEQ_CST);
  if (!gps_try_own()) {
    return HAL_OK;
  }
  const hal_status_t status = gps_apply_request();
  gps_release();
  return status;
}

void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud,
                  uint16_t config) {
  if (!gps_try_own()) {
    hal_derr_limited("gps", "init skipped: transport in use");
    return;
  }
  HAL_ATOMIC_STORE(&s_want_paused, 0u, HAL_ATOMIC_SEQ_CST);
  s_applied_requests = HAL_ATOMIC_LOAD(&s_requests, HAL_ATOMIC_SEQ_CST);
  s_retry = false;
  gps_transport_init(rx_pin, tx_pin, baud, config);
  gps_release();
}

hal_status_t hal_gps_pause(void) { return gps_request(true); }

hal_status_t hal_gps_resume(void) { return gps_request(false); }

void hal_gps_update(void) {
  if (gps_try_own()) {
    gps_transport_update();
    gps_release();
  }
}

int hal_gps_serial_available(void) {
  if (!gps_try_own()) {
    return 0;
  }
  const int available = gps_transport_available();
  gps_release();
  return available;
}

#endif /* HAL_ENABLE_GPS */
