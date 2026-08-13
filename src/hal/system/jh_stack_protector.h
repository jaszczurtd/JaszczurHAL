#pragma once

/**
 * @file jh_stack_protector.h
 * @brief Internal compiler stack-protector runtime contract.
 */

#include "hal/core/hal_compiler.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(HAL_ENABLE_STACK_PROTECTOR)

/** Process-wide canary read by compiler-instrumented function prologues. */
extern uintptr_t __stack_chk_guard;

/** Compiler ABI entry called after a function detects a damaged canary. */
HAL_NORETURN HAL_NO_STACK_PROTECTOR void __stack_chk_fail(void);

/** Target terminal path: retain stack-overflow diagnostics and reset. */
HAL_NORETURN HAL_NO_STACK_PROTECTOR void jh_stack_overflow_reset(void);

/** Same terminal path with a best-effort call-site address for retention. */
HAL_NORETURN HAL_NO_STACK_PROTECTOR void
jh_stack_overflow_reset_with_context(uintptr_t pc, uintptr_t lr);

#endif

#ifdef __cplusplus
}
#endif
