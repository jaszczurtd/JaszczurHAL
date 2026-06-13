/**
 * @file exception_info.c
 * @brief Cortex-M4 fault handlers + retained crash record for STM32G474.
 *
 * Only built for the ARM hardware target (JH_STM32G474_HW).
 */

#ifdef JH_STM32G474_HW

#include <stddef.h>

#include "exception_info.h"
#include "g474_debug_uart.h"
#include "stm32g474_regs.h"

#define JH_EXC_MAGIC 0x4A484659u /* 'J','H','F','Y' */

/* Retained across reset: placed in .noinit so startup does not clear it. */
__attribute__((section(".noinit"))) static jh_exception_info_t s_exc;

/* A separate flag, also retained, distinguishes "valid record" from random
 * power-on RAM contents (magic alone is a strong-enough check in practice). */

/**
 * @brief Common C fault handler.
 * @param frame       Pointer to the 8-word stacked exception frame.
 * @param exc_return  EXC_RETURN value (the LR on handler entry).
 * @param kind        Which fault fired.
 *
 * Reads the stacked frame and SCB fault-status registers, persists them,
 * dumps a summary over UART and resets. Marked `used` so -ffunction-sections
 * + --gc-sections cannot drop it (only the naked asm references it).
 */
__attribute__((used)) void
jh_fault_handler_c(uint32_t *frame, uint32_t exc_return, uint32_t kind) {
  s_exc.magic = JH_EXC_MAGIC;
  s_exc.kind = kind;
  s_exc.r0 = frame[0];
  s_exc.r1 = frame[1];
  s_exc.r2 = frame[2];
  s_exc.r3 = frame[3];
  s_exc.r12 = frame[4];
  s_exc.lr = frame[5];
  s_exc.pc = frame[6];
  s_exc.xpsr = frame[7];
  s_exc.cfsr = SCB_CFSR;
  s_exc.hfsr = SCB_HFSR;
  s_exc.mmfar = SCB_MMFAR;
  s_exc.bfar = SCB_BFAR;
  s_exc.shcsr = SCB_SHCSR;
  s_exc.exc_return = exc_return;

  /* Best-effort live dump (UART may already be up). */
  g474_debug_uart_puts("\r\n*** FAULT ***\r\n");
  g474_debug_uart_puts("kind=");
  g474_debug_uart_put_u32(kind);
  g474_debug_uart_puts(" PC=");
  g474_debug_uart_put_hex32(s_exc.pc);
  g474_debug_uart_puts(" LR=");
  g474_debug_uart_put_hex32(s_exc.lr);
  g474_debug_uart_puts(" xPSR=");
  g474_debug_uart_put_hex32(s_exc.xpsr);
  g474_debug_uart_puts("\r\nCFSR=");
  g474_debug_uart_put_hex32(s_exc.cfsr);
  g474_debug_uart_puts(" HFSR=");
  g474_debug_uart_put_hex32(s_exc.hfsr);
  g474_debug_uart_puts(" MMFAR=");
  g474_debug_uart_put_hex32(s_exc.mmfar);
  g474_debug_uart_puts(" BFAR=");
  g474_debug_uart_put_hex32(s_exc.bfar);
  g474_debug_uart_puts("\r\nResetting...\r\n");

  /* Request a system reset; record persists in .noinit. */
  SCB_AIRCR = SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
  __asm volatile("dsb");
  for (;;) {
  }
}

/*
 * Naked fault entry points. The "tst lr,#4" selects MSP vs PSP as the frame
 * pointer (EXC_RETURN bit 2), then we tail-call the C handler with
 * (frame, exc_return, kind). Standard Cortex-M fault-handling idiom.
 */
#define JH_FAULT_ENTRY(name, kind)                                             \
  __attribute__((naked)) void name(void) {                                     \
    __asm volatile("tst lr, #4            \n"                                  \
                   "ite eq                \n"                                  \
                   "mrseq r0, msp         \n"                                  \
                   "mrsne r0, psp         \n"                                  \
                   "mov r1, lr            \n"                                  \
                   "mov r2, %0            \n"                                  \
                   "b jh_fault_handler_c  \n" ::"i"(kind)                      \
                   : "r0", "r1", "r2");                                        \
  }

JH_FAULT_ENTRY(HardFault_Handler, JH_FAULT_HARD)
JH_FAULT_ENTRY(MemManage_Handler, JH_FAULT_MEMMANAGE)
JH_FAULT_ENTRY(BusFault_Handler, JH_FAULT_BUS)
JH_FAULT_ENTRY(UsageFault_Handler, JH_FAULT_USAGE)

const jh_exception_info_t *exception_info_last(void) { return &s_exc; }

static bool has_valid_record(const jh_exception_info_t *e) {
  return (e->magic == JH_EXC_MAGIC) && (e->kind != JH_FAULT_NONE);
}

bool exception_info_take_last(jh_exception_info_t *out) {
  if (!has_valid_record(&s_exc)) {
    return false;
  }

  if (out != NULL) {
    *out = s_exc;
  }

  /* Consume the record so it is handled only once. */
  s_exc.magic = 0u;
  s_exc.kind = JH_FAULT_NONE;
  return true;
}

static void dump_record(const jh_exception_info_t *e) {
  static const char *const names[] = {"NONE", "HARDFAULT", "MEMMANAGE",
                                      "BUSFAULT", "USAGEFAULT"};
  const char *name = (e->kind <= JH_FAULT_USAGE) ? names[e->kind] : "?";

  g474_debug_uart_puts("Last reset was a FAULT: ");
  g474_debug_uart_puts(name);
  g474_debug_uart_puts("\r\n  PC   =");
  g474_debug_uart_put_hex32(e->pc);
  g474_debug_uart_puts("\r\n  LR   =");
  g474_debug_uart_put_hex32(e->lr);
  g474_debug_uart_puts("\r\n  xPSR =");
  g474_debug_uart_put_hex32(e->xpsr);
  g474_debug_uart_puts("\r\n  CFSR =");
  g474_debug_uart_put_hex32(e->cfsr);
  g474_debug_uart_puts("\r\n  HFSR =");
  g474_debug_uart_put_hex32(e->hfsr);
  g474_debug_uart_puts("\r\n  MMFAR=");
  g474_debug_uart_put_hex32(e->mmfar);
  g474_debug_uart_puts("\r\n  BFAR =");
  g474_debug_uart_put_hex32(e->bfar);
  g474_debug_uart_puts("\r\n");
}

bool exception_info_report_last(void) {
  jh_exception_info_t rec;
  if (!exception_info_take_last(&rec)) {
    return false;
  }
  dump_record(&rec);
  return true;
}

#endif /* JH_STM32G474_HW */
