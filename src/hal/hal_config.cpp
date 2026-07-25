#include "hal_config.h"

#ifndef HAL_DISABLE_ASSERTS
#if HAL_TARGET_IS_RP
extern "C" void hal_rp2040_serial_write_assert_fail(const char *text);
#elif HAL_TARGET_IS_STM32G474 && defined(JH_STM32G474_HW)
#include "hal/impl/stm32g474/port/g474_debug_uart.h"
#else
#include <stdio.h>
#include <stdlib.h>
#endif
#endif

/**
 * @file hal_config.cpp
 * @brief Runtime HAL configuration implementation.
 *
 * Provides hal_setup() / hal_get_config() so the application can
 * override pool-size limits at startup.  When hal_setup() is never
 * called the compile-time #define defaults are used.
 */

#ifndef HAL_DISABLE_ASSERTS
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

static hal_config_t s_config = {
    HAL_PWM_FREQ_MAX_CHANNELS, HAL_CAN_MAX_INSTANCES,
    HAL_UART_MAX_INSTANCES,    HAL_SWSERIAL_MAX_INSTANCES,
    MOCK_CAN_MAX_INST,         MOCK_CAN_BUF_SIZE,
    MOCK_MAX_ALARMS,
};

/* Helper: cap value to compile-time max */
static int cap(int val, int compile_max) {
  if (val <= 0)
    return compile_max; /* 0 or negative -> use default */
  return val < compile_max ? val : compile_max;
}

hal_config_t hal_config_defaults(void) {
  hal_config_t d;
  d.pwm_freq_max_channels = HAL_PWM_FREQ_MAX_CHANNELS;
  d.can_max_instances = HAL_CAN_MAX_INSTANCES;
  d.uart_max_instances = HAL_UART_MAX_INSTANCES;
  d.swserial_max_instances = HAL_SWSERIAL_MAX_INSTANCES;
  d.mock_can_max_inst = MOCK_CAN_MAX_INST;
  d.mock_can_buf_size = MOCK_CAN_BUF_SIZE;
  d.mock_max_alarms = MOCK_MAX_ALARMS;
  return d;
}

void hal_setup(const hal_config_t *cfg) {
  if (!cfg)
    return;

  s_config.pwm_freq_max_channels =
      cap(cfg->pwm_freq_max_channels, HAL_PWM_FREQ_MAX_CHANNELS);
  s_config.can_max_instances =
      cap(cfg->can_max_instances, HAL_CAN_MAX_INSTANCES);
  s_config.uart_max_instances =
      cap(cfg->uart_max_instances, HAL_UART_MAX_INSTANCES);
  s_config.swserial_max_instances =
      cap(cfg->swserial_max_instances, HAL_SWSERIAL_MAX_INSTANCES);
  s_config.mock_can_max_inst = cap(cfg->mock_can_max_inst, MOCK_CAN_MAX_INST);
  s_config.mock_can_buf_size = cap(cfg->mock_can_buf_size, MOCK_CAN_BUF_SIZE);
  s_config.mock_max_alarms = cap(cfg->mock_max_alarms, MOCK_MAX_ALARMS);
}

const hal_config_t *hal_get_config(void) { return &s_config; }
