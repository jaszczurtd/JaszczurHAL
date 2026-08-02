#include "hal_config.h"

/**
 * @file hal_assert.cpp
 * @brief Target-aware HAL assertion failure implementation.
 */

#ifndef HAL_DISABLE_ASSERTS
#if HAL_TARGET_IS_RP
extern "C" void hal_rp2040_serial_write_assert_fail(const char *text);
#elif HAL_TARGET_IS_STM32G474 && defined(JH_STM32G474_HW)
#include "hal/impl/stm32g474/port/g474_debug_uart.h"
#else
#include <stdio.h>
#include <stdlib.h>
#endif

extern "C" void hal_assert_fail(const char *msg) {
  const char *text = msg ? msg : "(null)";

#if HAL_TARGET_IS_RP
  hal_rp2040_serial_write_assert_fail(text);
  for (;;) {
  }
#elif HAL_TARGET_IS_STM32G474 && defined(JH_STM32G474_HW)
  g474_debug_uart_init();
  g474_debug_uart_puts("HAL ASSERT FAIL: ");
  g474_debug_uart_puts(text);
  g474_debug_uart_puts("\r\n");
  for (;;) {
  }
#else
  fprintf(stderr, "HAL ASSERT FAIL: %s\n", text);
  abort();
#endif
}
#endif
