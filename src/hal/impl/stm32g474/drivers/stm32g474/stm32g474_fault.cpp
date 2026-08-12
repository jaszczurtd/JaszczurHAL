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

namespace {

constexpr uint32_t kRetainedSignature = 0x4A485346u; /* 'JHSF' */
constexpr uint32_t kRetainedAlive = 0x01u;
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
} stm32_fault_retained_t;

#ifdef JH_STM32G474_HW
__attribute__((section(".noinit"))) static stm32_fault_retained_t s_retained;
extern "C" char JH_StackLimit;
#else
static stm32_fault_retained_t s_retained;
static uint32_t s_host_rcc_csr = 0u;
static jh_exception_info_t s_host_fault_frame = {};
static bool s_host_fault_valid = false;
#endif

static bool g_initialised = false;
static hal_reset_reason_t g_reset_reason = HAL_RESET_REASON_UNKNOWN;
static hal_fault_info_t g_fault_info = {false, 0u, 0u, 0u};
static bool g_brownout_suspected = false;
static bool g_stack_guard_armed = false;

static void retained_seed_if_needed(void) {
  if (s_retained.signature != kRetainedSignature) {
    s_retained.signature = kRetainedSignature;
    s_retained.flags = 0u;
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

#ifdef JH_STM32G474_HW
static uint32_t read_reset_flags_raw(void) { return RCC_CSR; }

static void clear_reset_flags(void) { RCC_CSR |= RCC_CSR_RMVF; }

#ifdef HAL_ENABLE_STACK_GUARD
static uintptr_t stack_guard_base(void) {
  return reinterpret_cast<uintptr_t>(&JH_StackLimit);
}
#endif

static bool fault_hits_stack_guard(uint32_t kind, uint32_t cfsr,
                                   uint32_t mmfar) {
#ifdef HAL_ENABLE_STACK_GUARD
  const uintptr_t guard_base = stack_guard_base();
  return kind == JH_FAULT_MEMMANAGE && (cfsr & SCB_CFSR_MMARVALID) != 0u &&
         mmfar >= guard_base && mmfar < guard_base + kStackGuardBytes;
#else
  (void)kind;
  (void)cfsr;
  (void)mmfar;
  return false;
#endif
}

static bool take_fault_frame(hal_fault_info_t *out, bool *stack_overflow) {
  jh_exception_info_t rec;
  if (!exception_info_take_last(&rec)) {
    return false;
  }

  out->valid = true;
  out->pc = rec.pc;
  out->lr = rec.lr;
  out->psr = rec.xpsr;
  *stack_overflow = fault_hits_stack_guard(rec.kind, rec.cfsr, rec.mmfar);
  return true;
}
#else
static uint32_t read_reset_flags_raw(void) { return s_host_rcc_csr; }

static void clear_reset_flags(void) {}

#ifdef HAL_ENABLE_STACK_GUARD
static uintptr_t stack_guard_base(void) { return kHostStackGuardBase; }
#endif

static bool fault_hits_stack_guard(uint32_t kind, uint32_t cfsr,
                                   uint32_t mmfar) {
#ifdef HAL_ENABLE_STACK_GUARD
  const uintptr_t guard_base = stack_guard_base();
  return kind == JH_FAULT_MEMMANAGE && (cfsr & SCB_CFSR_MMARVALID) != 0u &&
         mmfar >= guard_base && mmfar < guard_base + kStackGuardBytes;
#else
  (void)kind;
  (void)cfsr;
  (void)mmfar;
  return false;
#endif
}

static bool take_fault_frame(hal_fault_info_t *out, bool *stack_overflow) {
  if (!s_host_fault_valid) {
    return false;
  }
  out->valid = true;
  out->pc = s_host_fault_frame.pc;
  out->lr = s_host_fault_frame.lr;
  out->psr = s_host_fault_frame.xpsr;
  *stack_overflow =
      fault_hits_stack_guard(s_host_fault_frame.kind, s_host_fault_frame.cfsr,
                             s_host_fault_frame.mmfar);
  s_host_fault_valid = false;
  s_host_fault_frame = {};
  return true;
}
#endif

} // namespace

void stm32g474_fault_init(void) {
  if (g_initialised) {
    return;
  }
  g_initialised = true;

  g_reset_reason = HAL_RESET_REASON_UNKNOWN;
  g_fault_info = {false, 0u, 0u, 0u};
  g_brownout_suspected = false;

  retained_seed_if_needed();
  const bool alive_marker = retained_flag(kRetainedAlive);
  bool stack_overflow = false;
  const bool has_fault_record =
      take_fault_frame(&g_fault_info, &stack_overflow);

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
}

bool stm32g474_fault_brownout_suspected(void) { return g_brownout_suspected; }

void stm32g474_fault_alive_mark(void) { retained_set_flag(kRetainedAlive); }

bool stm32g474_fault_stack_guard_init(void) {
#ifndef HAL_ENABLE_STACK_GUARD
  return false;
#else
  if (g_stack_guard_armed) {
    return true;
  }
#ifdef JH_STM32G474_HW
  const uintptr_t guard_base = stack_guard_base();
  if ((guard_base & (kStackGuardBytes - 1u)) != 0u ||
      ((MPU_TYPE >> 8u) & 0xFFu) <= kStackGuardRegion) {
    return false;
  }

  const uint32_t previous_region = MPU_RNR;
  MPU_RNR = kStackGuardRegion;
  if ((MPU_RASR & MPU_RASR_ENABLE) != 0u) {
    const bool already_configured =
        (MPU_RBAR & MPU_RBAR_ADDR_MASK) == guard_base &&
        MPU_RASR == kStackGuardRasr;
    MPU_RNR = previous_region;
    g_stack_guard_armed = already_configured;
    return already_configured;
  }

  MPU_RBAR = (uint32_t)guard_base;
  MPU_RASR = kStackGuardRasr;
  MPU_CTRL |= MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA;
  MPU_RNR = previous_region;
  __asm volatile("dsb" ::: "memory");
  __asm volatile("isb" ::: "memory");
#endif
  g_stack_guard_armed = true;
  return true;
#endif
}

void stm32g474_fault_stack_guard_check(void) {}

#ifndef JH_STM32G474_HW
extern "C" void hal_stm32g474_fault_test_reset(void) {
  g_initialised = false;
  g_reset_reason = HAL_RESET_REASON_UNKNOWN;
  g_fault_info = {false, 0u, 0u, 0u};
  g_brownout_suspected = false;
  g_stack_guard_armed = false;
  s_host_rcc_csr = 0u;
  s_host_fault_frame = {};
  s_host_fault_valid = false;
  s_retained = {0u, 0u};
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
  s_host_fault_frame.cfsr = SCB_CFSR_MMARVALID;
  s_host_fault_frame.mmfar = (uint32_t)kHostStackGuardBase;
  s_host_fault_valid = true;
}
#endif
