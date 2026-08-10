#include "../../hal_target.h"
#if HAL_TARGET_IS_RP

#include "../../hal_config.h"
#include "../../hal_usb.h"
#include "../shared/debug/jh_serial_port.h"

#include <limits.h>

static volatile bool s_serial_flush_enabled = false;

void jh_serial_port_begin(uint32_t baud) {
  (void)baud;
  (void)hal_usb_init();
  (void)hal_usb_task();
}

void jh_serial_port_set_flush(bool enabled) {
  __atomic_store_n(&s_serial_flush_enabled, enabled, __ATOMIC_RELEASE);
}

void jh_serial_port_message_begin(jh_serial_port_message_t kind) { (void)kind; }

void jh_serial_port_write(const char *data, size_t len) {
  if (data == NULL || len == 0u) {
    return;
  }

  size_t written = 0u;
  (void)hal_usb_cdc_write((const uint8_t *)data, len,
                          HAL_USB_CDC_WRITE_TIMEOUT_MS, &written);
}

size_t jh_serial_port_finish_line(char line_ending[2]) {
  line_ending[0] = '\r';
  line_ending[1] = '\n';
  jh_serial_port_write(line_ending, 2u);
  return 2u;
}

void jh_serial_port_flush(void) {
  if (__atomic_load_n(&s_serial_flush_enabled, __ATOMIC_ACQUIRE)) {
    (void)hal_usb_cdc_flush(HAL_USB_CDC_WRITE_TIMEOUT_MS);
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
