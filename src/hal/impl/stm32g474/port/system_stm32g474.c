/**
 * @file system_stm32g474.c
 * @brief SystemInit + SysTick time base for the STM32G474 bring-up.
 *
 * The STM32G4 boots on HSI16 (16 MHz); this first step keeps that default
 * clock (no PLL) and only:
 *   - relocates the vector table to flash base (VTOR),
 *   - enables the FPU (CPACR),
 *   - enables the dedicated fault handlers so CFSR/HFSR are meaningful,
 *   - starts SysTick at 1 kHz in non-FreeRTOS builds.
 *
 * This replaces the host-stub time source (`g_millis += ms`) with hardware
 * time, which is the single highest-leverage change: every timeout/timer in
 * the library depends on hal_millis().
 *
 * Only built for the ARM hardware target (JH_STM32G474_HW).
 */

#ifdef JH_STM32G474_HW

#include "stm32g474_regs.h"
#include <stdint.h>

#ifdef HAL_ENABLE_FREERTOS
#include <FreeRTOS.h>
#include <task.h>
#endif

/* Milliseconds since boot, advanced by the SysTick interrupt. */
#ifndef HAL_ENABLE_FREERTOS
volatile uint32_t g_systick_ms = 0u;

void SysTick_Handler(void) { g_systick_ms++; }
#else
static int stm32g474_system_in_isr_local(void) {
  uint32_t ipsr;
  __asm volatile("MRS %0, ipsr" : "=r"(ipsr));
  return (ipsr & 0x1FFu) != 0u;
}

static TickType_t stm32g474_freertos_tick_count(void) {
  if (stm32g474_system_in_isr_local()) {
    return xTaskGetTickCountFromISR();
  }
  return xTaskGetTickCount();
}
#endif

void SystemInit(void) {
  /* Vector table lives at the start of flash. */
  SCB_VTOR = 0x08000000u;

  /* Enable the FPU (full access to CP10/CP11), then sync. */
  SCB_CPACR |= SCB_CPACR_FPU_FULL;
  __asm volatile("dsb");
  __asm volatile("isb");

  /* Keep a hardware cycle counter available for FreeRTOS fallback delays.
   * SysTick belongs to the scheduler in that mode, so pre-scheduler and
   * critical-section waits must not depend on the tick interrupt. */
  COREDEBUG_DEMCR |= COREDEBUG_DEMCR_TRCENA;
  DWT_CYCCNT = 0u;
  DWT_CTRL |= DWT_CTRL_CYCCNTENA;

  /* Enable precise fault handlers so the exception_info module can read
   * MemManage/Bus/Usage fault status instead of everything escalating to
   * a generic HardFault. */
  SCB_SHCSR |=
      SCB_SHCSR_MEMFAULTENA | SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_USGFAULTENA;

#ifndef HAL_ENABLE_FREERTOS
  /* SysTick @ 1 kHz from the 16 MHz core clock. */
  SYSTICK_LOAD = (JH_G474_CORE_CLOCK_HZ / 1000u) - 1u;
  SYSTICK_VAL = 0u;
  SYSTICK_CTRL =
      SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;
#endif

  /* Make the interrupt state explicit so reset/debug entry modes
   * cannot leave the first delay permanently asleep. */
  __asm volatile("cpsie i" ::: "memory");
}

/* ── Time source consumed by the stm32g474_system driver under HW build ──── */

uint32_t stm32g474_systick_millis(void) {
#ifdef HAL_ENABLE_FREERTOS
  return (uint32_t)stm32g474_freertos_tick_count();
#else
  return g_systick_ms;
#endif
}

/* Sub-millisecond fraction in microseconds, derived from the SysTick
 * down-counter (LOAD..0 over 1 ms). */
uint32_t stm32g474_systick_micros(void) {
#ifdef HAL_ENABLE_FREERTOS
  return (uint32_t)stm32g474_freertos_tick_count() * 1000u;
#else
  uint32_t ms1, ms2;
  uint32_t val;
  const uint32_t load = SYSTICK_LOAD + 1u;

  /* Read consistently across a possible tick rollover. */
  do {
    ms1 = g_systick_ms;
    val = SYSTICK_VAL;
    ms2 = g_systick_ms;
  } while (ms1 != ms2);

  /* Elapsed core cycles within the current ms / cycles-per-us. */
  const uint32_t elapsed_cycles = load - val;
  const uint32_t us_in_ms = elapsed_cycles / (JH_G474_CORE_CLOCK_HZ / 1000000u);
  return (ms1 * 1000u) + us_in_ms;
#endif
}

#endif /* JH_STM32G474_HW */
