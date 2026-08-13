/**
 * @file stm32g474_fault.cpp
 * @brief STM32G474 SoC-specific crash / fault diagnostics.
 */

#include "stm32g474_fault.h"
#include "../../port/exception_info.h"
#include "../../port/stm32g474_regs.h"
#include "hal/core/hal_target.h"

#include <stddef.h>
#include <stdint.h>

#ifdef JH_STM32G474_HW
extern "C" {
/* Shared by exception entry and software-detected overflow entry. Keeping the
 * storage in this driver also makes the static-library fault path complete. */
__attribute__((section(".ccmram.jh_fault_stack"), aligned(8), used)) uint8_t
    jh_stm32_fault_emergency_stack[JH_STM32_FAULT_STACK_BYTES];
}
#endif

namespace {

constexpr uint32_t kRetainedSignature = 0x4A485332u; /* 'JHS2' */
constexpr uint32_t kRetainedAlive = 0x01u;
constexpr uint32_t kRetainedStackOverflow = 0x02u;
#ifdef HAL_ENABLE_STACK_GUARD
constexpr uint32_t kStackGuardBytes = 32u;
#ifdef JH_STM32G474_HW
constexpr uint32_t kStackGuardRegion = 7u;
constexpr uint32_t kStackGuardRasr =
    MPU_RASR_XN | MPU_RASR_SIZE_32B | MPU_RASR_ENABLE;
#endif
#endif
#ifndef JH_STM32G474_HW
constexpr uintptr_t kHostStackGuardBase = 0x20017800u;
#endif

typedef struct {
  uint32_t signature;
  uint32_t flags;
  uint32_t pc;
  uint32_t lr;
  uint32_t psr;
} stm32_fault_retained_t;

#ifdef JH_STM32G474_HW
__attribute__((section(".noinit"))) static stm32_fault_retained_t s_retained;
extern "C" char JH_StackLimit;
#else
static stm32_fault_retained_t s_retained;
static uint32_t s_host_rcc_csr = 0u;
static jh_exception_info_t s_host_fault_frame = {};
static bool s_host_fault_valid = false;
static bool s_host_stack_guard_matches = true;
#endif

static bool g_initialised = false;
static hal_reset_reason_t g_reset_reason = HAL_RESET_REASON_UNKNOWN;
static hal_fault_info_t g_fault_info = {};
static bool g_brownout_suspected = false;
[[maybe_unused]] static bool g_stack_guard_armed = false;
#ifdef JH_STM32G474_HW
static jh_exception_info_t g_detailed_fault = {};
static bool g_detailed_fault_valid = false;
#endif

static void retained_seed_if_needed(void) {
  if (s_retained.signature != kRetainedSignature) {
    /* Keep the slot invalid until every payload field is deterministic. A
     * power loss partway through seeding must not turn random .noinit bytes
     * into a believable overflow record on the next boot. */
    s_retained.signature = 0u;
    s_retained.flags = 0u;
    s_retained.pc = 0u;
    s_retained.lr = 0u;
    s_retained.psr = 0u;
    __asm volatile("" ::: "memory");
#ifdef JH_STM32G474_HW
    __asm volatile("dsb" ::: "memory");
#endif
    s_retained.signature = kRetainedSignature;
  }
}

static bool retained_flag(uint32_t flag) {
  retained_seed_if_needed();
  return (s_retained.flags & flag) != 0u;
}

static void retained_set_flag(uint32_t flag) {
  retained_seed_if_needed();
  s_retained.flags |= flag;
}

static void retained_clear_flags(void) {
  retained_seed_if_needed();
  s_retained.flags = 0u;
  s_retained.pc = 0u;
  s_retained.lr = 0u;
  s_retained.psr = 0u;
}

static void retained_record_stack_overflow(uint32_t pc, uint32_t lr,
                                           uint32_t psr) {
  /* Replace any unconsumed record atomically. Compiler failure can occur in a
   * constructor before early boot has consumed the previous run's .noinit
   * state, so keeping an old valid signature while changing the payload would
   * allow a reset in this window to publish mixed diagnostics. */
  s_retained.signature = 0u;
  __asm volatile("" ::: "memory");
#ifdef JH_STM32G474_HW
  __asm volatile("dsb" ::: "memory");
#endif
  s_retained.flags = 0u;
  s_retained.pc = pc;
  s_retained.lr = lr;
  s_retained.psr = psr;
  __asm volatile("" ::: "memory");
#ifdef JH_STM32G474_HW
  __asm volatile("dsb" ::: "memory");
#endif
  s_retained.flags = kRetainedStackOverflow;
  __asm volatile("" ::: "memory");
#ifdef JH_STM32G474_HW
  __asm volatile("dsb" ::: "memory");
#endif
  s_retained.signature = kRetainedSignature;
  __asm volatile("" ::: "memory");
#ifdef JH_STM32G474_HW
  __asm volatile("dsb" ::: "memory");
#endif
}

static hal_reset_reason_t classify_reset_reason(uint32_t csr,
                                                bool has_fault_record,
                                                bool stack_overflow,
                                                bool alive_marker) {
  if (stack_overflow) {
    return HAL_RESET_REASON_STACK_OVERFLOW;
  }
  if (has_fault_record) {
    return HAL_RESET_REASON_HARDFAULT;
  }

  const bool lpwr = (csr & RCC_CSR_LPWRRSTF) != 0u;
  const bool wwdg = (csr & RCC_CSR_WWDGRSTF) != 0u;
  const bool iwdg = (csr & RCC_CSR_IWDGRSTF) != 0u;
  const bool sftr = (csr & RCC_CSR_SFTRSTF) != 0u;
  const bool borr = (csr & RCC_CSR_BORRSTF) != 0u;
  const bool pinr = (csr & RCC_CSR_PINRSTF) != 0u;
  const bool oblr = (csr & RCC_CSR_OBLRSTF) != 0u;

  if (wwdg || iwdg) {
    return HAL_RESET_REASON_WATCHDOG;
  }
  if (sftr || oblr) {
    return HAL_RESET_REASON_SOFT;
  }
  if (lpwr) {
    return HAL_RESET_REASON_GLITCH;
  }

  if (borr) {
    /* On STM32G4 a cold power-up can set both BORRSTF and PINRSTF. If
     * no alive marker from the previous run survived, classify this as
     * POWER_ON. Otherwise, report BROWNOUT. */
    if (pinr && !alive_marker) {
      return HAL_RESET_REASON_POWER_ON;
    }
    return HAL_RESET_REASON_BROWNOUT;
  }

  if (pinr) {
    return HAL_RESET_REASON_RUN_PIN;
  }

  return HAL_RESET_REASON_POWER_ON;
}

static bool should_report_brownout(uint32_t csr, bool alive_marker) {
  const bool borr = (csr & RCC_CSR_BORRSTF) != 0u;
  const bool pinr = (csr & RCC_CSR_PINRSTF) != 0u;

  if (!borr) {
    return false;
  }

  if (pinr && !alive_marker) {
    return false;
  }
  return true;
}

#ifdef HAL_ENABLE_STACK_GUARD
static uintptr_t stack_guard_base(void);
#endif

static bool fault_hits_stack_guard(const jh_exception_info_t *record) {
#ifdef HAL_ENABLE_STACK_GUARD
  if (record == nullptr || stm32g474_fault_stack_guard_init() != HAL_OK) {
    return false;
  }

  const uintptr_t guard_base = stack_guard_base();
  const bool direct_guard_fault =
      (record->cfsr & SCB_CFSR_MMFSR_DACCVIOL) != 0u &&
      (record->cfsr & SCB_CFSR_MMARVALID) != 0u &&
      record->mmfar >= guard_base &&
      record->mmfar < guard_base + kStackGuardBytes;

  const uint32_t memory_stacking_fault = SCB_CFSR_MMFSR_MSTKERR;
  const uintptr_t frame_bytes = (record->exc_return & (1u << 4u)) != 0u
                                    ? 8u * sizeof(uint32_t)
                                    : 26u * sizeof(uint32_t);
  /* On MSTKERR the processor has already adjusted SP to the low end of the
   * attempted exception frame, even though its contents are not trustworthy.
   * STKALIGN can add one word that is not recoverable when xPSR was not
   * stacked, so conservatively include that maximum padding. */
  constexpr uintptr_t kMaximumAlignmentPadding = sizeof(uint32_t);
  const uintptr_t raw_sp = record->raw_sp;
  const bool frame_end_valid =
      raw_sp <= UINTPTR_MAX - frame_bytes - kMaximumAlignmentPadding;
  const uintptr_t frame_end =
      frame_end_valid ? raw_sp + frame_bytes + kMaximumAlignmentPadding
                      : UINTPTR_MAX;
  const uintptr_t guard_end = guard_base + kStackGuardBytes;
  const bool stacking_reached_guard =
      (record->cfsr & memory_stacking_fault) != 0u && raw_sp < guard_end &&
      frame_end > guard_base;

  /* MMFAR is not guaranteed valid when exception stacking itself faults.
   * In that case, use the captured post-adjustment SP plus the exact live MPU
   * readback above as a deliberately narrow fallback. MLSPERR is deliberately
   * excluded: lazy FP preservation uses FPCAR, not this exception-frame SP. */
  return direct_guard_fault || stacking_reached_guard;
#else
  (void)record;
  return false;
#endif
}

#ifdef JH_STM32G474_HW
static uint32_t read_reset_flags_raw(void) { return RCC_CSR; }

static void clear_reset_flags(void) { RCC_CSR |= RCC_CSR_RMVF; }

#ifdef HAL_ENABLE_STACK_GUARD
static uintptr_t stack_guard_base(void) {
  return reinterpret_cast<uintptr_t>(&JH_StackLimit);
}
#endif

static bool take_fault_frame(hal_fault_info_t *out, bool *stack_overflow) {
  jh_exception_info_t rec;
  if (!exception_info_take_last(&rec)) {
    return false;
  }

  g_detailed_fault = rec;
  g_detailed_fault_valid = true;
  out->valid = true;
  out->pc = rec.pc;
  out->lr = rec.lr;
  out->psr = rec.xpsr;
  out->cfsr = rec.cfsr;
  out->hfsr = rec.hfsr;
  out->mmfar = rec.mmfar;
  out->bfar = rec.bfar;
  *stack_overflow |= fault_hits_stack_guard(&rec);
  return true;
}
#else
static uint32_t read_reset_flags_raw(void) { return s_host_rcc_csr; }

static void clear_reset_flags(void) {}

#ifdef HAL_ENABLE_STACK_GUARD
static uintptr_t stack_guard_base(void) { return kHostStackGuardBase; }
#endif

static bool take_fault_frame(hal_fault_info_t *out, bool *stack_overflow) {
  if (!s_host_fault_valid) {
    return false;
  }
  out->valid = true;
  out->pc = s_host_fault_frame.pc;
  out->lr = s_host_fault_frame.lr;
  out->psr = s_host_fault_frame.xpsr;
  out->cfsr = s_host_fault_frame.cfsr;
  out->hfsr = s_host_fault_frame.hfsr;
  out->mmfar = s_host_fault_frame.mmfar;
  out->bfar = s_host_fault_frame.bfar;
  *stack_overflow |= fault_hits_stack_guard(&s_host_fault_frame);
  s_host_fault_valid = false;
  s_host_fault_frame = {};
  return true;
}
#endif

} // namespace

extern "C" HAL_NO_STACK_PROTECTOR bool
jh_stm32_fault_hits_stack_guard(const jh_exception_info_t *record) {
  return fault_hits_stack_guard(record);
}

void stm32g474_fault_init(void) {
  if (g_initialised) {
    return;
  }
  g_initialised = true;

  g_reset_reason = HAL_RESET_REASON_UNKNOWN;
  g_fault_info = {};
  g_brownout_suspected = false;

  retained_seed_if_needed();
  const bool alive_marker = retained_flag(kRetainedAlive);
  bool stack_overflow = retained_flag(kRetainedStackOverflow);
  const bool has_fault_record =
      take_fault_frame(&g_fault_info, &stack_overflow);
  if (!has_fault_record && stack_overflow && s_retained.pc != 0u) {
    g_fault_info.valid = true;
    g_fault_info.pc = s_retained.pc;
    g_fault_info.lr = s_retained.lr;
    g_fault_info.psr = s_retained.psr;
    g_fault_info.cfsr = 0u;
    g_fault_info.hfsr = 0u;
    g_fault_info.mmfar = 0u;
    g_fault_info.bfar = 0u;
  }

  const uint32_t csr = read_reset_flags_raw();
  g_reset_reason = classify_reset_reason(csr, has_fault_record, stack_overflow,
                                         alive_marker);
  g_brownout_suspected = should_report_brownout(csr, alive_marker);

  clear_reset_flags();
  retained_clear_flags();
}

hal_reset_reason_t stm32g474_fault_get_reset_reason(void) {
  return g_reset_reason;
}

bool stm32g474_fault_get_last_fault(hal_fault_info_t *out) {
  if (out == nullptr || !g_fault_info.valid) {
    return false;
  }
  *out = g_fault_info;
  return true;
}

void stm32g474_fault_clear_last_fault(void) {
  g_fault_info.valid = false;
  g_fault_info.pc = 0u;
  g_fault_info.lr = 0u;
  g_fault_info.psr = 0u;
  g_fault_info.cfsr = 0u;
  g_fault_info.hfsr = 0u;
  g_fault_info.mmfar = 0u;
  g_fault_info.bfar = 0u;
#ifdef JH_STM32G474_HW
  g_detailed_fault = {};
  g_detailed_fault_valid = false;
#endif
}

bool stm32g474_fault_report_last(void) {
#ifdef JH_STM32G474_HW
  if (!g_detailed_fault_valid) {
    return false;
  }
  const bool reported = exception_info_report_record(&g_detailed_fault);
  g_detailed_fault = {};
  g_detailed_fault_valid = false;
  return reported;
#else
  return false;
#endif
}

bool stm32g474_fault_brownout_suspected(void) { return g_brownout_suspected; }

void stm32g474_fault_alive_mark(void) { retained_set_flag(kRetainedAlive); }

hal_status_t stm32g474_fault_stack_guard_init(void) {
#ifndef HAL_ENABLE_STACK_GUARD
  return HAL_EUNSUPPORTED;
#else
#ifdef JH_STM32G474_HW
  const uintptr_t guard_base = stack_guard_base();
  if ((guard_base & (kStackGuardBytes - 1u)) != 0u ||
      ((MPU_TYPE >> 8u) & 0xFFu) <= kStackGuardRegion) {
    return HAL_EHW;
  }

  uint32_t primask = 0u;
  __asm volatile("mrs %0, primask\n"
                 "cpsid i"
                 : "=r"(primask)
                 :
                 : "memory");
  const uint32_t previous_region = MPU_RNR;
  MPU_RNR = kStackGuardRegion;
  if ((MPU_CTRL & MPU_CTRL_HFNMIENA) != 0u) {
    MPU_RNR = previous_region;
    if ((primask & 1u) == 0u) {
      __asm volatile("cpsie i" ::: "memory");
    }
    return HAL_EHW;
  }
  if ((MPU_RASR & MPU_RASR_ENABLE) != 0u) {
    const bool already_configured =
        (MPU_RBAR & MPU_RBAR_ADDR_MASK) == guard_base &&
        MPU_RASR == kStackGuardRasr &&
        (MPU_CTRL &
         (MPU_CTRL_ENABLE | MPU_CTRL_HFNMIENA | MPU_CTRL_PRIVDEFENA)) ==
            (MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA);
    MPU_RNR = previous_region;
    if ((primask & 1u) == 0u) {
      __asm volatile("cpsie i" ::: "memory");
    }
    g_stack_guard_armed = already_configured;
    return already_configured ? HAL_OK : HAL_EHW;
  }

  if (g_stack_guard_armed) {
    MPU_RNR = previous_region;
    if ((primask & 1u) == 0u) {
      __asm volatile("cpsie i" ::: "memory");
    }
    return HAL_EHW;
  }

  MPU_RBAR = (uint32_t)guard_base;
  MPU_RASR = kStackGuardRasr;
  MPU_CTRL |= MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA;
  const bool configured = (MPU_RBAR & MPU_RBAR_ADDR_MASK) == guard_base &&
                          MPU_RASR == kStackGuardRasr &&
                          (MPU_CTRL & (MPU_CTRL_ENABLE | MPU_CTRL_HFNMIENA |
                                       MPU_CTRL_PRIVDEFENA)) ==
                              (MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA);
  MPU_RNR = previous_region;
  __asm volatile("dsb" ::: "memory");
  __asm volatile("isb" ::: "memory");
  if ((primask & 1u) == 0u) {
    __asm volatile("cpsie i" ::: "memory");
  }
  if (!configured) {
    return HAL_EHW;
  }
#else
  if (g_stack_guard_armed && !s_host_stack_guard_matches) {
    return HAL_EHW;
  }
  s_host_stack_guard_matches = true;
#endif
  g_stack_guard_armed = true;
  return HAL_OK;
#endif
}

void stm32g474_fault_stack_guard_check(void) {}

#ifdef JH_STM32G474_HW
static HAL_NO_STACK_PROTECTOR void fault_console_best_effort(void) {
  if ((RCC_APB1ENR1 & RCC_APB1ENR1_USART2EN) == 0u ||
      (USART2_CR1 & (USART_CR1_UE | USART_CR1_TE)) !=
          (USART_CR1_UE | USART_CR1_TE)) {
    return;
  }

  static const char message[] = "STACK OVERFLOW; resetting\r\n";
  constexpr uint32_t kFallbackBudget = 80000u;
  constexpr uint32_t kCycleBudget = JH_G474_CORE_CLOCK_HZ / 200u; // 5 ms
  const bool cycle_counter_available =
      (COREDEBUG_DEMCR & COREDEBUG_DEMCR_TRCENA) != 0u &&
      (DWT_CTRL & DWT_CTRL_CYCCNTENA) != 0u;
  const uint32_t started_at = DWT_CYCCNT;
  uint32_t budget = kFallbackBudget;
  for (const char *cursor = message; *cursor != '\0'; ++cursor) {
    while ((USART2_ISR & USART_ISR_TXE) == 0u) {
      if (budget == 0u ||
          (cycle_counter_available &&
           static_cast<uint32_t>(DWT_CYCCNT - started_at) >= kCycleBudget)) {
        return;
      }
      --budget;
    }
    USART2_TDR = (uint32_t)(uint8_t)*cursor;
  }
  while ((USART2_ISR & USART_ISR_TC) == 0u) {
    if (budget == 0u ||
        (cycle_counter_available &&
         static_cast<uint32_t>(DWT_CYCCNT - started_at) >= kCycleBudget)) {
      break;
    }
    --budget;
  }
}

extern "C" HAL_NORETURN HAL_NO_STACK_PROTECTOR void
jh_stm32_stack_overflow_reset_c(uint32_t pc, uint32_t lr) {
  /* A compiler/RTOS overflow can happen before early boot consumed the prior
   * exception slot. Do not combine that stale record with this fresh event. */
  exception_info_discard_last();
  retained_record_stack_overflow(pc, lr, 0u);
  __asm volatile("dsb" ::: "memory");
  fault_console_best_effort();
  SCB_AIRCR = (SCB_AIRCR & 0x700u) | SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
  __asm volatile("dsb" ::: "memory");
  for (;;) {
    __asm volatile("nop");
  }
}

extern "C" HAL_NORETURN HAL_NO_STACK_PROTECTOR void
jh_stm32_stack_fault_reset(const jh_exception_info_t *record) {
  const uint32_t pc = record != nullptr ? record->pc : 0u;
  const uint32_t lr = record != nullptr ? record->lr : 0u;
  retained_record_stack_overflow(pc, lr, record != nullptr ? record->xpsr : 0u);
  __asm volatile("dsb" ::: "memory");
  fault_console_best_effort();
  SCB_AIRCR = (SCB_AIRCR & 0x700u) | SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
  __asm volatile("dsb" ::: "memory");
  for (;;) {
    __asm volatile("nop");
  }
}

extern "C" __attribute__((naked, used, no_stack_protector, noreturn)) void
jh_stack_overflow_reset_with_context(uintptr_t, uintptr_t) {
  __asm volatile("cpsid i                                      \n"
                 "ldr r2, =jh_stm32_fault_emergency_stack + %c0\n"
                 "mov sp, r2                                  \n"
                 "isb                                         \n"
                 "b jh_stm32_stack_overflow_reset_c           \n" ::"i"(
                     JH_STM32_FAULT_STACK_BYTES)
                 : "r2");
}

extern "C" __attribute__((naked, used, no_stack_protector, noreturn)) void
jh_stack_overflow_reset(void) {
  __asm volatile("cpsid i                                \n"
                 "mov r0, lr                             \n"
                 "movs r1, #0                           \n"
                 "b jh_stack_overflow_reset_with_context\n");
}
#else
extern "C" HAL_NORETURN HAL_NO_STACK_PROTECTOR void
jh_stack_overflow_reset_with_context(uintptr_t pc, uintptr_t lr) {
  (void)pc;
  (void)lr;
  HAL_TRAP();
  for (;;) {
  }
}

extern "C" HAL_NORETURN HAL_NO_STACK_PROTECTOR void
jh_stack_overflow_reset(void) {
  jh_stack_overflow_reset_with_context(0u, 0u);
}
#endif

#ifndef JH_STM32G474_HW
extern "C" void hal_stm32g474_fault_test_reset(void) {
  g_initialised = false;
  g_reset_reason = HAL_RESET_REASON_UNKNOWN;
  g_fault_info = {};
  g_brownout_suspected = false;
  g_stack_guard_armed = false;
  s_host_rcc_csr = 0u;
  s_host_fault_frame = {};
  s_host_fault_valid = false;
  s_host_stack_guard_matches = true;
  s_retained = {0u, 0u, 0u, 0u, 0u};
}

extern "C" void hal_stm32g474_fault_test_set_rcc_csr(uint32_t csr) {
  s_host_rcc_csr = csr;
}

extern "C" void hal_stm32g474_fault_test_set_fault_frame(uint32_t pc,
                                                         uint32_t lr,
                                                         uint32_t psr) {
  s_host_fault_frame.kind = JH_FAULT_HARD;
  s_host_fault_frame.pc = pc;
  s_host_fault_frame.lr = lr;
  s_host_fault_frame.xpsr = psr;
  s_host_fault_valid = true;
}

extern "C" void hal_stm32g474_fault_test_set_alive_marker(bool marked) {
  retained_seed_if_needed();
  if (marked) {
    s_retained.flags |= kRetainedAlive;
  } else {
    s_retained.flags &= ~kRetainedAlive;
  }
}

extern "C" void hal_stm32g474_fault_test_set_stack_guard_fault(void) {
  s_host_fault_frame = {};
  s_host_fault_frame.kind = JH_FAULT_MEMMANAGE;
  s_host_fault_frame.pc = 0x0800DEADu;
  s_host_fault_frame.xpsr = 0x21000000u;
  s_host_fault_frame.cfsr = SCB_CFSR_MMFSR_DACCVIOL | SCB_CFSR_MMARVALID;
  s_host_fault_frame.mmfar = (uint32_t)kHostStackGuardBase;
  s_host_fault_valid = true;
}

extern "C" void
hal_stm32g474_fault_test_set_escalated_guard_fault(bool data_access_violation) {
  s_host_fault_frame = {};
  s_host_fault_frame.kind = JH_FAULT_HARD;
  s_host_fault_frame.hfsr = SCB_HFSR_FORCED;
  s_host_fault_frame.cfsr = SCB_CFSR_MMARVALID;
  if (data_access_violation) {
    s_host_fault_frame.cfsr |= SCB_CFSR_MMFSR_DACCVIOL;
  }
  s_host_fault_frame.mmfar = (uint32_t)kHostStackGuardBase;
  s_host_fault_valid = true;
}

extern "C" void
hal_stm32g474_fault_test_set_stack_guard_stacking_fault(bool near_guard) {
  s_host_fault_frame = {};
  s_host_fault_frame.kind = JH_FAULT_MEMMANAGE;
  s_host_fault_frame.cfsr = SCB_CFSR_MMFSR_MSTKERR;
  s_host_fault_frame.exc_return = 1u << 4u; /* basic exception frame */
  s_host_fault_frame.raw_sp =
      static_cast<uint32_t>(kHostStackGuardBase + (near_guard ? 31u : 0x1000u));
  s_host_fault_valid = true;
}

extern "C" void hal_stm32g474_fault_test_set_stacking_fault_at(
    uint32_t raw_sp, bool extended_frame, bool lazy_fp_fault) {
  s_host_fault_frame = {};
  s_host_fault_frame.kind = JH_FAULT_MEMMANAGE;
  s_host_fault_frame.cfsr =
      lazy_fp_fault ? SCB_CFSR_MMFSR_MLSPERR : SCB_CFSR_MMFSR_MSTKERR;
  s_host_fault_frame.exc_return = extended_frame ? 0u : (1u << 4u);
  s_host_fault_frame.raw_sp = raw_sp;
  s_host_fault_valid = true;
}

extern "C" uint32_t hal_stm32g474_fault_test_stack_guard_base(void) {
  return static_cast<uint32_t>(kHostStackGuardBase);
}

extern "C" void hal_stm32g474_fault_test_set_retained_stack_overflow(void) {
  retained_record_stack_overflow(0u, 0u, 0u);
}

extern "C" void hal_stm32g474_fault_test_corrupt_stack_guard(void) {
  s_host_stack_guard_matches = false;
}
#endif
