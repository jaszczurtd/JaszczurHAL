/**
 * @file jh_stack_protector.c
 * @brief GCC/Clang stack-canary runtime for bare-metal firmware.
 */

#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_STACK_PROTECTOR)

#include "hal/system/jh_stack_protector.h"

#if !defined(JH_STACK_PROTECTOR_STRONG_COMPILE_CONTRACT)
#error                                                                         \
    "HAL_ENABLE_STACK_PROTECTOR requires the supported CMake stack-protector contract"
#endif

/* A data initializer makes the guard valid immediately after normal C runtime
 * data relocation, before preinit/init arrays and application constructors.
 * It detects accidental corruption; it is deliberately not presented as a
 * secret against an attacker who can read the firmware image. */
uintptr_t __stack_chk_guard = (uintptr_t)0xA5F03C69u;

#if HAL_RP_ARCH_ARM || defined(JH_STM32G474_HW)
/* A normal C prologue may push registers on the already-corrupted foreground
 * stack and fault again before the target can select its emergency stack. */
HAL_NORETURN HAL_NO_STACK_PROTECTOR __attribute__((naked, used)) void
__stack_chk_fail(void) {
  __asm volatile("cpsid i                                      \n"
                 "mov r0, lr                                  \n"
                 "movs r1, #0                                 \n"
                 "ldr r2, =jh_stack_overflow_reset_with_context\n"
                 "bx r2                                       \n");
}
#elif HAL_RP_ARCH_RISCV
HAL_NORETURN HAL_NO_STACK_PROTECTOR __attribute__((naked, used)) void
__stack_chk_fail(void) {
  __asm volatile("csrci mstatus, 8                             \n"
                 "mv a0, ra                                   \n"
                 "li a1, 0                                    \n"
                 "tail jh_stack_overflow_reset_with_context   \n");
}
#else
HAL_NORETURN HAL_NO_STACK_PROTECTOR void __stack_chk_fail(void) {
  const uintptr_t caller =
      (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
  jh_stack_overflow_reset_with_context(caller, 0u);
}
#endif

#endif
