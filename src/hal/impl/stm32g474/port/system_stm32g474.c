/**
 * @file system_stm32g474.c
 * @brief SystemInit, PLL clock tree, and monotonic time for STM32G474.
 *
 * The STM32G4 boots on HSI16 (16 MHz). Startup configures HSI16 as the PLL
 * source and runs the core and both APB buses at 170 MHz. It also:
 *   - relocates the vector table to flash base (VTOR),
 *   - enables the FPU (CPACR),
 *   - enables the dedicated fault handlers so CFSR/HFSR are meaningful,
 *   - starts SysTick at 1 kHz in non-FreeRTOS builds.
 *
 * Only built for the ARM hardware target (JH_STM32G474_HW).
 */

#ifdef JH_STM32G474_HW

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

static void stm32g474_clock_init(void) {
  RCC_APB1ENR1 |= RCC_APB1ENR1_PWREN;
  (void)RCC_APB1ENR1;

  /* 170 MHz requires voltage Range 1 boost and four flash wait states. */
  PWR_CR5 &= ~PWR_CR5_R1MODE;
  FLASH_ACR = (FLASH_ACR & ~FLASH_ACR_LATENCY_MASK) | FLASH_ACR_LATENCY_4WS |
              FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
  while ((FLASH_ACR & FLASH_ACR_LATENCY_MASK) != FLASH_ACR_LATENCY_4WS) {
  }

  RCC_CR |= RCC_CR_HSION;
  while ((RCC_CR & RCC_CR_HSIRDY) == 0u) {
  }

  RCC_CR &= ~RCC_CR_PLLON;
  while ((RCC_CR & RCC_CR_PLLRDY) != 0u) {
  }

  RCC_PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI | RCC_PLLCFGR_PLLM_DIV4 |
                RCC_PLLCFGR_PLLN_MUL85 | RCC_PLLCFGR_PLLREN |
                RCC_PLLCFGR_PLLR_DIV2;
  RCC_CR |= RCC_CR_PLLON;
  while ((RCC_CR & RCC_CR_PLLRDY) == 0u) {
  }

  /* Keep HCLK at 85 MHz during the SYSCLK transition, then expose the full
   * 170 MHz after PLL is selected and stable. Both APB buses remain /1. */
  RCC_CFGR = (RCC_CFGR & ~(RCC_CFGR_HPRE_MASK | RCC_CFGR_PPRE1_MASK |
                           RCC_CFGR_PPRE2_MASK)) |
             RCC_CFGR_HPRE_DIV2 | RCC_CFGR_PPRE1_DIV1 | RCC_CFGR_PPRE2_DIV1;
  RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_SW_MASK) | RCC_CFGR_SW_PLL;
  while ((RCC_CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL) {
  }

  for (volatile uint32_t delay = 0u; delay < 200u; ++delay) {
    __asm volatile("nop");
  }
  RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_HPRE_MASK) | RCC_CFGR_HPRE_DIV1;

  /* Keep timing-sensitive I2C on HSI16 and give FDCAN a live PCLK1 source. */
  RCC_CCIPR = (RCC_CCIPR & ~(RCC_CCIPR_I2C1SEL_MASK | RCC_CCIPR_I2C2SEL_MASK |
                             RCC_CCIPR_FDCANSEL_MASK)) |
              RCC_CCIPR_I2C1SEL_HSI16 | RCC_CCIPR_I2C2SEL_HSI16 |
              RCC_CCIPR_FDCANSEL_PCLK1;

  __asm volatile("dsb");
  __asm volatile("isb");
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
  /* SysTick @ 1 kHz from the 170 MHz core clock. */
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
