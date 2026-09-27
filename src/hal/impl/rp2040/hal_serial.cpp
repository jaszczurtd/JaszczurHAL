#include "hal/core/hal_compiler.h"
#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"
#include "hal/debug/jh_serial_port.h"
#include "hal/usb/hal_usb.h"

#include <limits.h>

static volatile bool s_serial_flush_enabled = false;

/* A host that keeps DTR asserted but stops reading (Linux port closed with
 * HUPCL cleared) leaves the CDC FIFO full forever. Debug output must not pay
 * the bounded write timeout for every fragment then, or the application
 * watchdog starves. After the first timeout with the host still asserted,
 * writes drop instead of blocking until the host drains data (HAL_OK) or
 * releases DTR (HAL_EAGAIN). */
static volatile bool s_tx_congested = false;

static uint32_t jh_tx_timeout_ms(void) {
  return HAL_ATOMIC_LOAD(&s_tx_congested, HAL_ATOMIC_ACQUIRE)
             ? 0u
             : HAL_USB_CDC_WRITE_TIMEOUT_MS;
}

static void jh_tx_note_result(hal_status_t status) {
  if (status == HAL_ETIMEOUT) {
    HAL_ATOMIC_STORE(&s_tx_congested, true, HAL_ATOMIC_RELEASE);
  } else if (status == HAL_OK || status == HAL_EAGAIN) {
    HAL_ATOMIC_STORE(&s_tx_congested, false, HAL_ATOMIC_RELEASE);
  }
}

void jh_serial_port_begin(uint32_t baud) {
  (void)baud;
  (void)hal_usb_init();
  (void)hal_usb_task();
}

void jh_serial_port_set_flush(bool enabled) {
  HAL_ATOMIC_STORE(&s_serial_flush_enabled, enabled, HAL_ATOMIC_RELEASE);
}

void jh_serial_port_message_begin(jh_serial_port_message_t kind) { (void)kind; }

void jh_serial_port_write(const char *data, size_t len) {
  if (data == NULL || len == 0u) {
    return;
  }

  size_t written = 0u;
  jh_tx_note_result(hal_usb_cdc_write((const uint8_t *)data, len,
                                      jh_tx_timeout_ms(), &written));
}

size_t jh_serial_port_finish_line(char line_ending[2]) {
  line_ending[0] = '\r';
  line_ending[1] = '\n';
  jh_serial_port_write(line_ending, 2u);
  return 2u;
}

void jh_serial_port_flush(void) {
  if (HAL_ATOMIC_LOAD(&s_serial_flush_enabled, HAL_ATOMIC_ACQUIRE)) {
    (void)hal_usb_cdc_flush(jh_tx_timeout_ms());
  }
}

int jh_serial_port_available(void) {
  size_t available = 0u;
  (void)hal_usb_cdc_available(&available);
  return available > (size_t)INT_MAX ? INT_MAX : (int)available;
}

int jh_serial_port_read(void) {
  uint8_t value = 0u;
  size_t read = 0u;
  return hal_usb_cdc_read(&value, 1u, &read) == HAL_OK && read == 1u
             ? (int)value
             : -1;
}

#endif // HAL_TARGET_IS_RP
