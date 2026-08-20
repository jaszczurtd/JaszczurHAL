#pragma once

/**
 * @file stm32g474_power_port.h
 * @brief Clock and monotonic-time hooks used after STM32G474 low-power modes.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Restore the 170 MHz PLL clock tree after STOP0/STOP1 wake-up. */
void stm32g474_system_clock_restore_after_stop(void);

/** Add time elapsed while SysTick and the core clock were stopped. */
void stm32g474_monotonic_compensate_us(uint64_t elapsed_us);

/** Capture a retained Standby wake marker before RTC initialization. */
void stm32g474_power_capture_boot_wake(void);

#ifdef __cplusplus
}
#endif
