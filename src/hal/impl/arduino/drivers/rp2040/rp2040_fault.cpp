/**
 * @file rp2040_fault.cpp
 * @brief RP2040 SoC-specific crash / fault diagnostics implementation.
 *
 * See @c rp2040_fault.h for the retained scratch layout and the contract.
 */

#include "rp2040_fault.h"

#include <Arduino.h>
#include <pico/stdlib.h>
#include <hardware/watchdog.h>
#include <hardware/structs/watchdog.h>
#include <RP2040Support.h>

namespace {

constexpr uint32_t kStateSignatureMask = 0xFFFFFF00u;
constexpr uint32_t kStateSignature     = 0x4A484400u; // 'J','H','D',0
constexpr uint32_t kFlagFault          = 0x01u;
constexpr uint32_t kFlagAlive          = 0x02u;
constexpr uint32_t kFlagStackOverflow  = 0x04u;

// Sentinel PC value reported when stack overflow is detected by the canary
// check (no real exception frame is captured; the corruption is observed
// asynchronously, not at the moment it occurred).
constexpr uint32_t kStackOverflowSentinelPc = 0xDEADD000u;

// Canary value written at the bottom of the stack region. Chosen so a single
// stuck bit or zero-write does not look like the canary.
constexpr uint32_t kStackCanary = 0xC4314EA5u;

// arduino-pico's RP2040Support.h declares the linker symbol as
// `extern "C" char __StackLimit;` (a single byte). We need to read a 32-bit
// word at that address; route the address through an inline-asm "memory
// operand" so the optimiser's -Warray-bounds analysis cannot reach the
// underlying `char [1]` type.
extern "C" char __StackLimit;

inline uint32_t *stack_canary_addr(void) {
    char *p = &__StackLimit;
    __asm__ ("" : "+r"(p));
    return reinterpret_cast<uint32_t *>(p);
}

hal_reset_reason_t g_reset_reason       = HAL_RESET_REASON_UNKNOWN;
hal_fault_info_t   g_fault_info         = { false, 0u, 0u, 0u };
bool               g_brownout_suspected = false;
bool               g_stack_guard_armed  = false;
bool               g_initialised        = false;

// Set by the same logic that decides hal_watchdog_caused_reboot(): true if
// the previous boot was caused by a real watchdog timeout while the
// application watchdog was armed (vs. a programmatic watchdog_reboot()
// used by UF2 / BOOTSEL / firmware-driven reset).
inline bool latched_watchdog_timeout(void) {
    return watchdog_enable_caused_reboot();
}

inline uint32_t state_word(void) {
    return watchdog_hw->scratch[0];
}

inline void state_word_set(uint32_t v) {
    watchdog_hw->scratch[0] = v;
}

inline bool state_signature_valid(uint32_t s) {
    return (s & kStateSignatureMask) == kStateSignature;
}

void map_pico_reset_reason(void) {
    using rrt = RP2040::resetReason_t;
    rrt r = rp2040.getResetReason();
    switch (r) {
        case rrt::PWRON_RESET:    g_reset_reason = HAL_RESET_REASON_POWER_ON; break;
        case rrt::RUN_PIN_RESET:  g_reset_reason = HAL_RESET_REASON_RUN_PIN;  break;
        case rrt::SOFT_RESET:     g_reset_reason = HAL_RESET_REASON_SOFT;     break;
        case rrt::WDT_RESET:      g_reset_reason = HAL_RESET_REASON_WATCHDOG; break;
        case rrt::DEBUG_RESET:    g_reset_reason = HAL_RESET_REASON_DEBUG;    break;
        case rrt::GLITCH_RESET:   g_reset_reason = HAL_RESET_REASON_GLITCH;   break;
        case rrt::BROWNOUT_RESET: g_reset_reason = HAL_RESET_REASON_BROWNOUT; break;
        default:                  g_reset_reason = HAL_RESET_REASON_UNKNOWN;  break;
    }
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

extern "C" __attribute__((used, noreturn))
void rp2040_fault_capture_c(const uint32_t *frame) {
    uint32_t pc  = (frame != nullptr) ? frame[6] : 0u;
    uint32_t lr  = (frame != nullptr) ? frame[5] : 0u;
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
    __asm__ volatile (
        "movs r0, #4              \n"
        "mov  r1, lr              \n"
        "tst  r0, r1              \n"
        "beq  1f                  \n"
        "mrs  r0, psp             \n"
        "b    2f                  \n"
        "1:                       \n"
        "mrs  r0, msp             \n"
        "2:                       \n"
        "ldr  r1, =rp2040_fault_capture_c \n"
        "bx   r1                  \n"
    );
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
            g_fault_info.pc    = watchdog_hw->scratch[1];
            g_fault_info.lr    = watchdog_hw->scratch[2];
            g_fault_info.psr   = watchdog_hw->scratch[3];

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

bool rp2040_fault_brownout_suspected(void) {
    return g_brownout_suspected;
}

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
