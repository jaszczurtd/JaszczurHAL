/**
 * @file stm32g474_fault.cpp
 * @brief STM32G474 SoC-specific crash / fault diagnostics.
 */

#include "stm32g474_fault.h"
#include "../../../../hal_target.h"
#include "../../port/stm32g474_regs.h"

#include <stddef.h>
#include <stdint.h>

#ifdef JH_STM32G474_HW
#include "../../port/exception_info.h"
#endif

namespace {

constexpr uint32_t kRetainedSignature = 0x4A485346u; /* 'JHSF' */
constexpr uint32_t kRetainedAlive = 0x01u;
constexpr uint32_t kRetainedStackOverflow = 0x02u;
constexpr uint32_t kStackOverflowSentinelPc = 0xDEADD000u;
constexpr uint32_t kStackCanary = 0xC4314EA5u;

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
static hal_fault_info_t s_host_fault_frame = {false, 0u, 0u, 0u};
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
    s_retained.pc = 0u;
    s_retained.lr = 0u;
    s_retained.psr = 0u;
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

static hal_reset_reason_t classify_reset_reason(uint32_t csr,
                                                bool has_fault_record,
                                                bool has_stack_overflow_marker,
                                                bool alive_marker) {
  if (has_stack_overflow_marker) {
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

static bool take_fault_frame(hal_fault_info_t *out) {
  jh_exception_info_t rec;
  if (!exception_info_take_last(&rec)) {
    return false;
  }

  out->valid = true;
  out->pc = rec.pc;
  out->lr = rec.lr;
  out->psr = rec.xpsr;
  return true;
}

static volatile uint32_t *stack_canary_addr(void) {
  char *p = &JH_StackLimit;
  __asm__("" : "+r"(p));
  return reinterpret_cast<volatile uint32_t *>(p);
}

[[noreturn]] static void force_system_reset(void) {
  SCB_AIRCR = SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
  __asm volatile("dsb");
  for (;;) {
  }
}
#else
static uint32_t read_reset_flags_raw(void) { return s_host_rcc_csr; }

static void clear_reset_flags(void) {}

static bool take_fault_frame(hal_fault_info_t *out) {
  if (!s_host_fault_frame.valid) {
    return false;
  }
  *out = s_host_fault_frame;
  s_host_fault_frame.valid = false;
  s_host_fault_frame.pc = 0u;
  s_host_fault_frame.lr = 0u;
  s_host_fault_frame.psr = 0u;
  return true;
}

static uint32_t s_host_stack_canary = 0u;

static volatile uint32_t *stack_canary_addr(void) {
  return &s_host_stack_canary;
}

[[noreturn]] static void force_system_reset(void) {
  /* Host build cannot reset hardware; keep behavior deterministic for tests. */
  for (;;) {
  }
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
  const bool stack_overflow = retained_flag(kRetainedStackOverflow);

  if (stack_overflow) {
    g_fault_info.valid = true;
    g_fault_info.pc =
        (s_retained.pc != 0u) ? s_retained.pc : kStackOverflowSentinelPc;
    g_fault_info.lr = s_retained.lr;
    g_fault_info.psr = s_retained.psr;
  } else {
    (void)take_fault_frame(&g_fault_info);
  }

  const uint32_t csr = read_reset_flags_raw();
  g_reset_reason = classify_reset_reason(csr, g_fault_info.valid,
                                         stack_overflow, alive_marker);
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
  *stack_canary_addr() = kStackCanary;
  g_stack_guard_armed = true;
  return true;
}

void stm32g474_fault_stack_guard_check(void) {
  if (!g_stack_guard_armed) {
    return;
  }
  if (*stack_canary_addr() == kStackCanary) {
    return;
  }

  retained_seed_if_needed();
  s_retained.flags |= kRetainedStackOverflow;
  s_retained.pc = kStackOverflowSentinelPc;
  s_retained.lr = 0u;
  s_retained.psr = 0u;

  force_system_reset();
}

#ifndef JH_STM32G474_HW
extern "C" void hal_stm32g474_fault_test_reset(void) {
  g_initialised = false;
  g_reset_reason = HAL_RESET_REASON_UNKNOWN;
  g_fault_info = {false, 0u, 0u, 0u};
  g_brownout_suspected = false;
  g_stack_guard_armed = false;
  s_host_rcc_csr = 0u;
  s_host_fault_frame = {false, 0u, 0u, 0u};
  s_host_stack_canary = 0u;
  s_retained = {0u, 0u, 0u, 0u, 0u};
}

extern "C" void hal_stm32g474_fault_test_set_rcc_csr(uint32_t csr) {
  s_host_rcc_csr = csr;
}

extern "C" void hal_stm32g474_fault_test_set_fault_frame(uint32_t pc,
                                                         uint32_t lr,
                                                         uint32_t psr) {
  s_host_fault_frame.valid = true;
  s_host_fault_frame.pc = pc;
  s_host_fault_frame.lr = lr;
  s_host_fault_frame.psr = psr;
}

extern "C" void hal_stm32g474_fault_test_set_alive_marker(bool marked) {
  retained_seed_if_needed();
  if (marked) {
    s_retained.flags |= kRetainedAlive;
  } else {
    s_retained.flags &= ~kRetainedAlive;
  }
}

extern "C" void hal_stm32g474_fault_test_set_stack_overflow_marker(bool set) {
  retained_seed_if_needed();
  if (set) {
    s_retained.flags |= kRetainedStackOverflow;
    s_retained.pc = kStackOverflowSentinelPc;
  } else {
    s_retained.flags &= ~kRetainedStackOverflow;
    s_retained.pc = 0u;
  }
}
#endif
