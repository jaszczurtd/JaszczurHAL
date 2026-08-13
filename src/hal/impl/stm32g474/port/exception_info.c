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

#define JH_EXC_MAGIC 0x4A484633u /* 'J','H','F','3' (record layout v3) */
#define JH_MAIN_RAM_START 0x20000000u
#define JH_MAIN_RAM_END 0x20018000u
#define JH_CCM_RAM_START 0x10000000u
#define JH_CCM_RAM_END 0x10008000u

#ifdef HAL_ENABLE_STACK_GUARD
extern char JH_StackLimit;
#endif

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
 * Reads a valid stacked frame and the SCB fault-status registers, persists
 * them, and resets. Marked `used` so -ffunction-sections + --gc-sections
 * cannot drop it (only the naked asm references it).
 */
static bool range_is_readable(uintptr_t address, size_t bytes) {
  if (bytes > UINTPTR_MAX - address) {
    return false;
  }
  const uintptr_t end = address + bytes;
  return (address >= JH_MAIN_RAM_START && end <= JH_MAIN_RAM_END) ||
         (address >= JH_CCM_RAM_START && end <= JH_CCM_RAM_END);
}

static const uint32_t *
basic_exception_frame(uint32_t *raw_frame, uint32_t exc_return, uint32_t cfsr) {
  const uint32_t stacking_errors = SCB_CFSR_MMFSR_MSTKERR |
                                   SCB_CFSR_MMFSR_MLSPERR |
                                   SCB_CFSR_BFSR_STKERR | SCB_CFSR_BFSR_LSPERR;
  if (raw_frame == NULL || (cfsr & stacking_errors) != 0u) {
    return NULL;
  }

  uintptr_t address = (uintptr_t)raw_frame;
  if ((exc_return & (1u << 4)) == 0u) {
    /* Extended Cortex-M4F frame: S0-S15, FPSCR and one reserved word precede
     * the basic R0-xPSR frame. */
    address += 18u * sizeof(uint32_t);
  }
  if ((address & (sizeof(uint32_t) - 1u)) != 0u ||
      !range_is_readable(address, 8u * sizeof(uint32_t))) {
    return NULL;
  }
  return (const uint32_t *)address;
}

__attribute__((used, no_stack_protector)) void
jh_fault_handler_c(uint32_t *frame, uint32_t exc_return, uint32_t kind) {
  /* Invalidate first. A reset during capture must never expose a partial
   * record as valid on the next boot. */
  s_exc.magic = 0u;
  s_exc.kind = kind;
  s_exc.cfsr = SCB_CFSR;
  s_exc.hfsr = SCB_HFSR;
  s_exc.mmfar = SCB_MMFAR;
  s_exc.bfar = SCB_BFAR;
  s_exc.shcsr = SCB_SHCSR;
  s_exc.exc_return = exc_return;
  s_exc.raw_sp = (uint32_t)(uintptr_t)frame;

  s_exc.r0 = 0u;
  s_exc.r1 = 0u;
  s_exc.r2 = 0u;
  s_exc.r3 = 0u;
  s_exc.r12 = 0u;
  s_exc.lr = 0u;
  s_exc.pc = 0u;
  s_exc.xpsr = 0u;
  const uint32_t *const basic =
      basic_exception_frame(frame, exc_return, s_exc.cfsr);
  if (basic != NULL) {
    s_exc.r0 = basic[0];
    s_exc.r1 = basic[1];
    s_exc.r2 = basic[2];
    s_exc.r3 = basic[3];
    s_exc.r12 = basic[4];
    s_exc.lr = basic[5];
    s_exc.pc = basic[6];
    s_exc.xpsr = basic[7];
  }

  /* Publish validity last, then reset. Only the separately bounded overflow
   * reporter may touch UART from this terminal path. */
  __asm volatile("" ::: "memory");
  s_exc.magic = JH_EXC_MAGIC;
  __asm volatile("dsb" ::: "memory");
#ifdef HAL_ENABLE_STACK_GUARD
  if (jh_stm32_fault_hits_stack_guard(&s_exc)) {
    jh_stm32_stack_fault_reset(&s_exc);
  }
#endif
  SCB_AIRCR = (SCB_AIRCR & 0x700u) | SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
  __asm volatile("dsb" ::: "memory");
  for (;;) {
  }
}

/*
 * Naked fault entry points. They disable interrupts first, then "tst lr,#4"
 * selects MSP vs PSP as the frame pointer (EXC_RETURN bit 2), switches MSP to
 * the dedicated CCMRAM emergency stack, and tail-calls the C handler with
 * (frame, exc_return, kind).
 */
#define JH_FAULT_ENTRY(name, kind)                                             \
  __attribute__((naked, used)) void name(void) {                               \
    __asm volatile("cpsid i               \n"                                  \
                   "tst lr, #4            \n"                                  \
                   "ite eq                \n"                                  \
                   "mrseq r0, msp         \n"                                  \
                   "mrsne r0, psp         \n"                                  \
                   "mov r1, lr            \n"                                  \
                   "mov r2, %0            \n"                                  \
                   "ldr r3, =jh_stm32_fault_emergency_stack\n"                 \
                   "ldr r12, =%1          \n"                                  \
                   "add r3, r3, r12       \n"                                  \
                   "msr msp, r3           \n"                                  \
                   "isb                   \n"                                  \
                   "b jh_fault_handler_c  \n" ::"i"(kind),                     \
                   "i"(JH_STM32_FAULT_STACK_BYTES)                             \
                   : "r0", "r1", "r2", "r3", "r12");                           \
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

  const jh_exception_info_t copy = s_exc;
  /* Consume before exposing the copy. A reset while the caller processes it
   * must not replay the same record. */
  s_exc.magic = 0u;
  __asm volatile("dsb" ::: "memory");
  if (out != NULL) {
    *out = copy;
  }
  return true;
}

__attribute__((no_stack_protector)) void exception_info_discard_last(void) {
  s_exc.magic = 0u;
  __asm volatile("dsb" ::: "memory");
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
  return exception_info_report_record(&rec);
}

bool exception_info_report_record(const jh_exception_info_t *record) {
  if (record == NULL || record->kind == JH_FAULT_NONE) {
    return false;
  }
  dump_record(record);
  return true;
}

#endif /* JH_STM32G474_HW */
