/**
 * @file stm32g474_fault.cpp
 * @brief STM32G474 SoC-specific crash / fault diagnostics (stub backend).
 *
 * See @c stm32g474_fault.h for the rationale and the planned real impl.
 */

#include "stm32g474_fault.h"

void stm32g474_fault_init(void) {
    /* no-op */
}

hal_reset_reason_t stm32g474_fault_get_reset_reason(void) {
    return HAL_RESET_REASON_UNKNOWN;
}

bool stm32g474_fault_get_last_fault(hal_fault_info_t *out) {
    (void)out;
    return false;
}

void stm32g474_fault_clear_last_fault(void) {
    /* no-op */
}

bool stm32g474_fault_brownout_suspected(void) {
    return false;
}

void stm32g474_fault_alive_mark(void) {
    /* no-op */
}

bool stm32g474_fault_stack_guard_init(void) {
    return false;
}

void stm32g474_fault_stack_guard_check(void) {
    /* no-op */
}
