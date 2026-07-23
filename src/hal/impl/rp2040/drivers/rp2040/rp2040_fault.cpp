/**
 * @file rp2040_fault.cpp
 * @brief RP2040 SoC-specific crash / fault diagnostics implementation.
 *
 * See @c rp2040_fault.h for the retained scratch layout and the contract.
 */

#include "rp2040_fault.h"

#include <hardware/structs/watchdog.h>
#include <hardware/watchdog.h>
#include <pico/stdlib.h>
#if defined(PICO_RP2350)
#include <hardware/structs/powman.h>
#else
#include <hardware/structs/vreg_and_chip_reset.h>
#endif

namespace {

constexpr uint32_t kStateSignatureMask = 0xFFFFFF00u;
constexpr uint32_t kStateSignature = 0x4A484400u; // 'J','H','D',0
constexpr uint32_t kFlagFault = 0x01u;
constexpr uint32_t kFlagAlive = 0x02u;
constexpr uint32_t kFlagStackOverflow = 0x04u;

// Sentinel PC value reported when stack overflow is detected by the canary
// check (no real exception frame is captured; the corruption is observed
// asynchronously, not at the moment it occurred).
constexpr uint32_t kStackOverflowSentinelPc = 0xDEADD000u;

// Canary value written at the bottom of the stack region. Chosen so a single
// stuck bit or zero-write does not look like the canary.
constexpr uint32_t kStackCanary = 0xC4314EA5u;

// The pico-sdk linker script exports the stack limit as a single byte
// (`char __StackLimit`). We need to read a 32-bit word at that address; route
// the address through an inline-asm "memory operand" so the optimiser's
// -Warray-bounds analysis cannot reach the underlying `char [1]` type.
extern "C" char __StackLimit;

inline uint32_t *stack_canary_addr(void) {
  char *p = &__StackLimit;
  __asm__("" : "+r"(p));
  return reinterpret_cast<uint32_t *>(p);
}

hal_reset_reason_t g_reset_reason = HAL_RESET_REASON_UNKNOWN;
hal_fault_info_t g_fault_info = {false, 0u, 0u, 0u};
bool g_brownout_suspected = false;
bool g_stack_guard_armed = false;
bool g_initialised = false;

// Set by the same logic that decides hal_watchdog_caused_reboot(): true if
// the previous boot was caused by a real watchdog timeout while the
// application watchdog was armed (vs. a programmatic watchdog_reboot()
// used by UF2 / BOOTSEL / firmware-driven reset).
inline bool latched_watchdog_timeout(void) {
  return watchdog_enable_caused_reboot();
}

inline uint32_t state_word(void) { return watchdog_hw->scratch[0]; }

inline void state_word_set(uint32_t v) { watchdog_hw->scratch[0] = v; }

inline bool state_signature_valid(uint32_t s) {
  return (s & kStateSignatureMask) == kStateSignature;
}

// Native reimplementation of arduino-pico's RP2040::getResetReason(): decode
// the reset cause straight from the watchdog reason register and the SoC
// chip-reset register (VREG_AND_CHIP_RESET on RP2040, POWMAN on RP2350) into
// hal_reset_reason_t. Precedence matches the upstream: a genuine watchdog
// timeout wins, then a soft reset()/reboot() routed through the watchdog
// timer, then the chip-reset bits.
void map_pico_reset_reason(void) {
  if (watchdog_caused_reboot() && watchdog_enable_caused_reboot()) {
    g_reset_reason = HAL_RESET_REASON_WATCHDOG;
    return;
  }

  if ((watchdog_hw->reason & WATCHDOG_REASON_TIMER_BITS) != 0u) {
    g_reset_reason = HAL_RESET_REASON_SOFT;
    return;
  }

#if defined(PICO_RP2350)
  const uint32_t chip_reset = powman_hw->chip_reset;
  if (chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS) {
    g_reset_reason = HAL_RESET_REASON_POWER_ON;
  } else if (chip_reset & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) {
    g_reset_reason = HAL_RESET_REASON_RUN_PIN;
  } else if ((chip_reset & POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS) ||
             (chip_reset & POWMAN_CHIP_RESET_HAD_RESCUE_BITS) ||
             (chip_reset & POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS)) {
    g_reset_reason = HAL_RESET_REASON_DEBUG;
  } else if (chip_reset & POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS) {
    g_reset_reason = HAL_RESET_REASON_GLITCH;
  } else if (chip_reset & POWMAN_CHIP_RESET_HAD_BOR_BITS) {
    g_reset_reason = HAL_RESET_REASON_BROWNOUT;
  } else {
    g_reset_reason = HAL_RESET_REASON_UNKNOWN;
  }
#else
  const uint32_t chip_reset = vreg_and_chip_reset_hw->chip_reset;
  if (chip_reset & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS) {
    // POR covers both power-on and brown-out on RP2040; brown-out is teased
    // apart later in rp2040_fault_init() via the retained alive marker.
    g_reset_reason = HAL_RESET_REASON_POWER_ON;
  } else if (chip_reset & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS) {
    g_reset_reason = HAL_RESET_REASON_RUN_PIN;
  } else if (chip_reset & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS) {
    g_reset_reason = HAL_RESET_REASON_DEBUG;
  } else {
    g_reset_reason = HAL_RESET_REASON_UNKNOWN;
  }
#endif
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// HardFault handler (overrides pico-sdk's weak isr_hardfault)
//
// Cortex-M0+ exception frame at the address pointed to by MSP/PSP
// (selected by EXC_RETURN bit 2 in LR):
//   [0]=R0 [1]=R1 [2]=R2 [3]=R3 [4]=R12 [5]=LR [6]=PC [7]=xPSR
//
// We extract PC, LR and xPSR, stash them into watchdog scratch[1..3] under
// our state-word flag, then trigger an immediate reboot via the watchdog.
// rp2040_fault_init() picks the values up on the next boot.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" __attribute__((used, noreturn)) void
rp2040_fault_capture_c(const uint32_t *frame) {
  uint32_t pc = (frame != nullptr) ? frame[6] : 0u;
  uint32_t lr = (frame != nullptr) ? frame[5] : 0u;
  uint32_t psr = (frame != nullptr) ? frame[7] : 0u;

  uint32_t state = watchdog_hw->scratch[0];
  if ((state & kStateSignatureMask) != kStateSignature) {
    state = kStateSignature;
  }
  state |= kFlagFault;

  watchdog_hw->scratch[0] = state;
  watchdog_hw->scratch[1] = pc;
  watchdog_hw->scratch[2] = lr;
  watchdog_hw->scratch[3] = psr;

  watchdog_reboot(0, 0, 0);
  while (true) {
    tight_loop_contents();
  }
}

extern "C" __attribute__((naked, used)) void isr_hardfault(void) {
  __asm__ volatile("movs r0, #4              \n"
                   "mov  r1, lr              \n"
                   "tst  r0, r1              \n"
                   "beq  1f                  \n"
                   "mrs  r0, psp             \n"
                   "b    2f                  \n"
                   "1:                       \n"
                   "mrs  r0, msp             \n"
                   "2:                       \n"
                   "ldr  r1, =rp2040_fault_capture_c \n"
                   "bx   r1                  \n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Public driver API
// ─────────────────────────────────────────────────────────────────────────────

void rp2040_fault_init(void) {
  if (g_initialised) {
    return;
  }
  g_initialised = true;

  map_pico_reset_reason();

  // Watchdog timeout latch wins over getResetReason() in disagreement
  // cases (both look at the same hardware but through different filters).
  if (latched_watchdog_timeout()) {
    g_reset_reason = HAL_RESET_REASON_WATCHDOG;
  }

  uint32_t state = state_word();
  if (state_signature_valid(state)) {
    bool fault_flag = (state & kFlagFault) != 0u;
    bool stack_flag = (state & kFlagStackOverflow) != 0u;
    bool alive_flag = (state & kFlagAlive) != 0u;

    if (fault_flag) {
      g_fault_info.valid = true;
      g_fault_info.pc = watchdog_hw->scratch[1];
      g_fault_info.lr = watchdog_hw->scratch[2];
      g_fault_info.psr = watchdog_hw->scratch[3];

      // Stack overflow takes priority over generic HardFault in the
      // reason classification -- it is the actionable root cause.
      g_reset_reason = stack_flag ? HAL_RESET_REASON_STACK_OVERFLOW
                                  : HAL_RESET_REASON_HARDFAULT;
    }

    // Brown-out heuristic: silicon reports POR but our alive marker
    // survived in retained storage -> the previous run must have been
    // past the point where it called rp2040_fault_alive_mark() at
    // least once. Treat it as a brown-out.
    if (alive_flag && g_reset_reason == HAL_RESET_REASON_POWER_ON) {
      g_brownout_suspected = true;
      g_reset_reason = HAL_RESET_REASON_BROWNOUT;
    }

    // Clear volatile bits, keep signature. PC/LR/PSR stay readable
    // through g_fault_info even after clearing the retained copy.
    state_word_set(kStateSignature);
    watchdog_hw->scratch[1] = 0;
    watchdog_hw->scratch[2] = 0;
    watchdog_hw->scratch[3] = 0;
  } else {
    // Cold boot or clobbered slot 0. Seed signature, leave flags clear.
    state_word_set(kStateSignature);
  }
}

hal_reset_reason_t rp2040_fault_get_reset_reason(void) {
  return g_reset_reason;
}

bool rp2040_fault_get_last_fault(hal_fault_info_t *out) {
  if (out == nullptr || !g_fault_info.valid) {
    return false;
  }
  *out = g_fault_info;
  return true;
}

void rp2040_fault_clear_last_fault(void) {
  g_fault_info.valid = false;
  g_fault_info.pc = 0;
  g_fault_info.lr = 0;
  g_fault_info.psr = 0;
}

bool rp2040_fault_brownout_suspected(void) { return g_brownout_suspected; }

void rp2040_fault_alive_mark(void) {
  if (!g_initialised) {
    rp2040_fault_init();
  }
  uint32_t state = state_word();
  if (!state_signature_valid(state)) {
    state = kStateSignature;
  }
  state |= kFlagAlive;
  state_word_set(state);
}

bool rp2040_fault_stack_guard_init(void) {
  *stack_canary_addr() = kStackCanary;
  g_stack_guard_armed = true;
  return true;
}

void rp2040_fault_stack_guard_check(void) {
  if (!g_stack_guard_armed) {
    return;
  }
  if (*stack_canary_addr() == kStackCanary) {
    return;
  }

  uint32_t state = state_word();
  if (!state_signature_valid(state)) {
    state = kStateSignature;
  }
  state |= (kFlagFault | kFlagStackOverflow);
  state_word_set(state);
  watchdog_hw->scratch[1] = kStackOverflowSentinelPc;
  watchdog_hw->scratch[2] = 0u;
  watchdog_hw->scratch[3] = 0u;

  watchdog_reboot(0, 0, 0);
  while (true) {
    tight_loop_contents();
  }
}
