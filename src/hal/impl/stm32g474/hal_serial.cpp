#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/debug/jh_serial_port.h"

#include <stdio.h>

#ifdef JH_STM32G474_HW
/* Real hardware uses USART2 through the ST-Link virtual COM port. */
#include "drivers/stm32g474/stm32g474_fault.h"
#include "port/g474_debug_uart.h"
#endif

void jh_serial_port_begin(uint32_t baud) {
  (void)baud;
#ifdef JH_STM32G474_HW
  g474_debug_uart_init();
  stm32g474_fault_init();
  (void)stm32g474_fault_report_last();
#endif
}

void jh_serial_port_set_flush(bool enabled) { (void)enabled; }

void jh_serial_port_message_begin(jh_serial_port_message_t kind) { (void)kind; }

void jh_serial_port_write(const char *data, size_t len) {
  if (data == NULL || len == 0u) {
    return;
  }

#ifdef JH_STM32G474_HW
  for (size_t i = 0u; i < len; ++i) {
    g474_debug_uart_putc(data[i]);
  }
#else
  (void)fwrite(data, 1u, len, stdout);
#endif
}

size_t jh_serial_port_finish_line(char line_ending[2]) {
#ifdef JH_STM32G474_HW
  line_ending[0] = '\r';
  line_ending[1] = '\n';
  jh_serial_port_write(line_ending, 2u);
  return 2u;
#else
  line_ending[0] = '\n';
  jh_serial_port_write(line_ending, 1u);
  return 1u;
#endif
}

void jh_serial_port_flush(void) {}

int jh_serial_port_available(void) { return 0; }

int jh_serial_port_read(void) { return -1; }

#endif // HAL_TARGET_IS_STM32G474
