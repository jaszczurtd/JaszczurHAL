/**
 * @file system_stm32g474.c
 * @brief SystemInit, PLL clock tree, and monotonic time for STM32G474.
 *
 * The STM32G4 boots on HSI16 (16 MHz). Startup brings up the PLL tree of
 * stm32g474_clock.h (170 MHz from HSI16, or 160 MHz from HSE on boards that
 * select it) with the core and both APB buses undivided. It also:
 *   - relocates the vector table to flash base (VTOR),
 *   - enables the FPU (CPACR),
 *   - enables the dedicated fault handlers so CFSR/HFSR are meaningful,
 *   - starts SysTick at 1 kHz in non-FreeRTOS builds.
 *
 * Only built for the ARM hardware target (JH_STM32G474_HW).
 */

#ifdef JH_STM32G474_HW

#include "stm32g474_clock_tree.h"
#include "stm32g474_power_port.h"
#include "stm32g474_regs.h"
#include "stm32g474_time.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef HAL_ENABLE_STACK_GUARD
#include "hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.h"
#endif

typedef struct {
  uint32_t high;
  uint32_t low;
} stm32g474_millis_epoch_t;

/* The inactive slot is written completely before the active index flips. This
 * keeps snapshots valid even when a higher-priority interrupt preempts the
 * SysTick writer. */
static volatile stm32g474_millis_epoch_t g_millis_epoch[2] = {{0u, 0u},
                                                              {0u, 0u}};
static volatile uint32_t g_millis_epoch_active = 0u;
static volatile stm32g474_millis_epoch_t g_monotonic_offset_us[2] = {{0u, 0u},
                                                                     {0u, 0u}};
static volatile uint32_t g_monotonic_offset_active = 0u;

static void stm32g474_time_barrier(void) { __asm volatile("dmb" ::: "memory"); }

static void stm32g474_millis_advance(uint32_t milliseconds) {
  if (milliseconds == 0u) {
    return;
  }
  const uint32_t active = g_millis_epoch_active;
  const uint32_t next = active ^ 1u;
  uint32_t high = g_millis_epoch[active].high;
  uint32_t low = g_millis_epoch[active].low;

  const uint32_t previous_low = low;
  low += milliseconds;
  if (low < previous_low) {
    high += 1u;
  }
  g_millis_epoch[next].high = high;
  g_millis_epoch[next].low = low;
  stm32g474_time_barrier();
  g_millis_epoch_active = next;
}

#ifdef HAL_ENABLE_FREERTOS
static void stm32g474_millis_snapshot(uint32_t *high, uint32_t *low) {
  uint32_t before;
  uint32_t after;

  do {
    before = g_millis_epoch_active;
    stm32g474_time_barrier();
    *high = g_millis_epoch[before].high;
    *low = g_millis_epoch[before].low;
    stm32g474_time_barrier();
    after = g_millis_epoch_active;
  } while (before != after);
}
#endif

static uint64_t stm32g474_monotonic_offset_snapshot(void) {
  uint32_t before;
  uint32_t after;
  uint32_t high;
  uint32_t low;

  do {
    before = g_monotonic_offset_active;
    stm32g474_time_barrier();
    high = g_monotonic_offset_us[before].high;
    low = g_monotonic_offset_us[before].low;
    stm32g474_time_barrier();
    after = g_monotonic_offset_active;
  } while (before != after);
  return ((uint64_t)high << 32u) | low;
}

void stm32g474_monotonic_compensate_us(uint64_t elapsed_us) {
  if (elapsed_us == 0u) {
    return;
  }
  const uint32_t active = g_monotonic_offset_active;
  const uint32_t next = active ^ 1u;
  const uint64_t current =
      ((uint64_t)g_monotonic_offset_us[active].high << 32u) |
      g_monotonic_offset_us[active].low;
  const uint64_t updated = current + elapsed_us;
  g_monotonic_offset_us[next].high = (uint32_t)(updated >> 32u);
  g_monotonic_offset_us[next].low = (uint32_t)updated;
  stm32g474_time_barrier();
  g_monotonic_offset_active = next;
}

static volatile jh_stm32g474_pll_source_t g_pll_source =
    JH_STM32G474_PLL_FROM_HSI16;

jh_stm32g474_pll_source_t stm32g474_clock_pll_source(void) {
  return g_pll_source;
}

static void stm32g474_clock_init(void) {
  g_pll_source = jh_stm32g474_clock_tree_init();
}

void stm32g474_system_clock_restore_after_stop(void) {
  stm32g474_clock_init();
  COREDEBUG_DEMCR |= COREDEBUG_DEMCR_TRCENA;
  DWT_CYCCNT = 0u;
  DWT_CTRL |= DWT_CTRL_CYCCNTENA;
}

#ifndef HAL_ENABLE_FREERTOS

void SysTick_Handler(void) { stm32g474_millis_advance(1u); }
#else
static uint32_t g_freertos_tick_last = 0u;

void stm32g474_freertos_tick_sync(uint32_t tick_count) {
  const uint32_t elapsed = tick_count - g_freertos_tick_last;
  g_freertos_tick_last = tick_count;
  stm32g474_millis_advance(elapsed);
}
#endif

void SystemInit(void) {
  /* Vector table lives at the start of flash. */
  SCB_VTOR = 0x08000000u;

  /* Enable the FPU (full access to CP10/CP11), then sync. */
  SCB_CPACR |= SCB_CPACR_FPU_FULL;
  __asm volatile("dsb");
  __asm volatile("isb");

  stm32g474_clock_init();

  /* RM0440 6.4.3 recommends turning the UCPD dead-battery pull-downs off in
   * all cases; left on, PA9/PA10 high pulls PB4/PB6 to ground. PWR keeps the
   * bit through STOP. */
  PWR_CR3 |= PWR_CR3_UCPD1_DBDIS;

#if defined(HAL_ENABLE_POWER_MANAGEMENT) && !defined(HAL_ENABLE_FREERTOS)
  /* Preserve the RTC/Standby reason before an RTC handle can reconfigure WUT.
   */
  stm32g474_power_capture_boot_wake();
#endif

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

#ifdef HAL_ENABLE_STACK_GUARD
  /* Protect the bottom 32 bytes of the main stack before application code or
   * interrupts can use it. Failure means the requested protection cannot be
   * guaranteed, so do not continue with a falsely guarded system. */
  if (stm32g474_fault_stack_guard_init() != HAL_OK) {
    for (;;) {
      __asm volatile("nop");
    }
  }
#endif

#ifndef HAL_ENABLE_FREERTOS
  /* SysTick @ 1 kHz from the core clock. */
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

uint64_t stm32g474_systick_micros64(void);

uint32_t stm32g474_systick_millis(void) {
  return (uint32_t)(stm32g474_systick_micros64() / UINT64_C(1000));
}

static uint64_t stm32g474_systick_micros64_raw(void) {
#ifdef HAL_ENABLE_FREERTOS
  uint32_t high;
  uint32_t low;
  stm32g474_millis_snapshot(&high, &low);
  return jh_stm32g474_compose_micros(high, low, 0u);
#else
  uint32_t before;
  uint32_t after;
  uint32_t high;
  uint32_t low;
  uint32_t value;
  uint32_t pending;

  /* Snapshot the epoch and down-counter together. If SysTick rolled while its
   * interrupt was masked, account for the pending millisecond locally. */
  do {
    before = g_millis_epoch_active;
    stm32g474_time_barrier();
    high = g_millis_epoch[before].high;
    low = g_millis_epoch[before].low;
    value = SYSTICK_VAL;
    pending = SCB_ICSR & SCB_ICSR_PENDSTSET;
    if (pending != 0u) {
      value = SYSTICK_VAL;
    }
    stm32g474_time_barrier();
    after = g_millis_epoch_active;
  } while (before != after);

  if (pending != 0u) {
    jh_stm32g474_increment_millis(&high, &low);
  }

  uint32_t micros_in_millis = 0u;
  if (value != 0u) {
    const uint32_t elapsed_cycles = SYSTICK_LOAD - value;
    micros_in_millis = elapsed_cycles / (JH_G474_CORE_CLOCK_HZ / 1000000u);
  }
  return jh_stm32g474_compose_micros(high, low, micros_in_millis);
#endif
}

uint64_t stm32g474_systick_micros64(void) {
  return stm32g474_systick_micros64_raw() +
         stm32g474_monotonic_offset_snapshot();
}

uint32_t stm32g474_systick_micros(void) {
  return (uint32_t)stm32g474_systick_micros64();
}

#endif /* JH_STM32G474_HW */
