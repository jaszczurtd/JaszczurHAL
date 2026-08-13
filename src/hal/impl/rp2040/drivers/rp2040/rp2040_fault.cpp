/**
 * @file rp2040_fault.cpp
 * @brief RP2040 SoC-specific crash / fault diagnostics implementation.
 *
 * See @c rp2040_fault.h for the retained scratch layout and the contract.
 */

#include "rp2040_fault.h"

#include "hal/core/hal_target.h"

#include <hardware/regs/addressmap.h>
#include <hardware/regs/uart.h>
#include <hardware/structs/uart.h>
#include <hardware/structs/watchdog.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <pico/runtime_init.h>
#include <pico/stdlib.h>
#if HAL_TARGET_IS_RP2040
#include <hardware/regs/m0plus.h>
#include <hardware/structs/mpu.h>
#elif HAL_TARGET_IS_RP2350_ARM
#include <hardware/regs/m33.h>
#include <hardware/structs/scb.h>
#elif HAL_TARGET_IS_RP2350_RISCV
#include <hardware/regs/rvcsr.h>
#include <hardware/riscv.h>
#endif

#if HAL_TARGET_IS_RP2040
#include <hardware/structs/vreg_and_chip_reset.h>
#else
#include <hardware/structs/powman.h>
#endif

#if defined(HAL_ENABLE_STACK_GUARD) &&                                         \
    (!defined(PICO_USE_STACK_GUARDS) || !PICO_USE_STACK_GUARDS)
#error "HAL_ENABLE_STACK_GUARD requires PICO_USE_STACK_GUARDS=1"
#endif

#if defined(HAL_ENABLE_STACK_GUARD) &&                                         \
    defined(PICO_RUNTIME_SKIP_INIT_PER_CORE_INSTALL_STACK_GUARD) &&            \
    PICO_RUNTIME_SKIP_INIT_PER_CORE_INSTALL_STACK_GUARD
#error "HAL_ENABLE_STACK_GUARD requires Pico SDK per-core stack-guard startup"
#endif

#if defined(HAL_ENABLE_STACK_GUARD) &&                                         \
    defined(PICO_RUNTIME_NO_INIT_PER_CORE_INSTALL_STACK_GUARD) &&              \
    PICO_RUNTIME_NO_INIT_PER_CORE_INSTALL_STACK_GUARD
#error "HAL_ENABLE_STACK_GUARD requires the Pico SDK stack-guard implementation"
#endif

extern "C" {
#ifdef HAL_ENABLE_STACK_GUARD
extern char __StackBottom;
extern char __StackOneBottom;
#endif

alignas(16) uint32_t jh_rp_fault_emergency_stack0[128];
alignas(16) uint32_t jh_rp_fault_emergency_stack1[128];

void jh_rp_fault_handlers_link_anchor(void);
}

namespace {

constexpr uint32_t kStateSignatureMask = 0xFFFFFF00u;
constexpr uint32_t kStateSignature = 0x4A484400u; // 'J','H','D',0
constexpr uint32_t kFlagFault = 0x01u;
constexpr uint32_t kFlagAlive = 0x02u;
constexpr uint32_t kFlagStackOverflow = 0x04u;
#ifdef HAL_ENABLE_STACK_GUARD
constexpr uintptr_t kStackGuardSize = 32u;
#endif

struct rp_retained_fault_detail_t {
  uint32_t signature;
  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t mmfar;
  uint32_t bfar;
};

constexpr uint32_t kDetailSignature = 0x4A484632u; // 'JHF2'
__attribute__((section(".uninitialized_data.jh_fault")))
rp_retained_fault_detail_t s_retained_detail;

hal_reset_reason_t g_reset_reason = HAL_RESET_REASON_UNKNOWN;
hal_fault_info_t g_fault_info = {};
bool g_brownout_suspected = false;
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

#ifdef HAL_ENABLE_STACK_GUARD
uintptr_t current_stack_bottom(void) {
  const uint core = get_core_num();
  if (core == 0u) {
    return reinterpret_cast<uintptr_t>(&__StackBottom);
  }
  if (core == 1u) {
    return reinterpret_cast<uintptr_t>(&__StackOneBottom);
  }
  return 0u;
}

uintptr_t current_stack_guard_base(void) {
  const uintptr_t bottom = current_stack_bottom();
#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_RP2350_RISCV
  return (bottom + (kStackGuardSize - 1u)) & ~(kStackGuardSize - 1u);
#else
  return bottom & ~uintptr_t{7u};
#endif
}

#if HAL_TARGET_IS_RP2040
bool stack_guard_registers_match(void) {
  const uintptr_t guard = current_stack_guard_base();
  if (guard == 0u) {
    return false;
  }

  const uint32_t interrupt_state = save_and_disable_interrupts();
  const uint32_t saved_region = mpu_hw->rnr;
  mpu_hw->rnr = 0u;
  const uint32_t ctrl = mpu_hw->ctrl;
  const uint32_t rbar = mpu_hw->rbar;
  const uint32_t rasr = mpu_hw->rasr;
  mpu_hw->rnr = saved_region;
  restore_interrupts(interrupt_state);

  const uint32_t subregion = static_cast<uint32_t>((guard >> 5u) & 7u);
  const uint32_t disabled_subregions = 0xffu ^ (1u << subregion);
  const uint32_t expected_rasr =
      M0PLUS_MPU_RASR_ENABLE_BITS | (7u << M0PLUS_MPU_RASR_SIZE_LSB) |
      (disabled_subregions << M0PLUS_MPU_RASR_SRD_LSB) |
      (1u << 28u); // XN, AP=0: no data or instruction access.
  const uint32_t expected_ctrl =
      M0PLUS_MPU_CTRL_ENABLE_BITS | M0PLUS_MPU_CTRL_PRIVDEFENA_BITS;

  return (ctrl & M0PLUS_MPU_CTRL_BITS) == expected_ctrl &&
         (rbar & M0PLUS_MPU_RBAR_ADDR_BITS) ==
             (static_cast<uint32_t>(guard) & M0PLUS_MPU_RBAR_ADDR_BITS) &&
         (rasr & M0PLUS_MPU_RASR_BITS) == expected_rasr;
}
#elif HAL_TARGET_IS_RP2350_ARM
bool stack_guard_registers_match(void) {
  const uintptr_t expected = current_stack_guard_base();
  uint32_t actual = 0u;
  __asm volatile("mrs %0, msplim" : "=r"(actual));
  return expected != 0u && actual == static_cast<uint32_t>(expected);
}
#elif HAL_TARGET_IS_RP2350_RISCV
bool stack_guard_registers_match(void) {
  const uintptr_t guard = current_stack_guard_base();
  if (guard == 0u) {
    return false;
  }

  const uint32_t expected_address =
      static_cast<uint32_t>((guard | 0x0fu) >> 2u);
  const uint32_t expected_config = RVCSR_PMPCFG0_R0_A_VALUE_NAPOT
                                   << RVCSR_PMPCFG0_R0_A_LSB;
  const uint32_t config = riscv_read_csr(pmpcfg0);
  const uint32_t machine_config = riscv_read_csr(RVCSR_PMPCFGM0_OFFSET);

  return riscv_read_csr(pmpaddr0) == expected_address &&
         (config & 0xffu) == expected_config && (machine_config & 1u) != 0u;
}
#endif
#endif

#if HAL_RP_ARCH_ARM
bool address_is_readable_fault_frame(const uint32_t *frame) {
  const uintptr_t address = reinterpret_cast<uintptr_t>(frame);
  return (address & 3u) == 0u && address >= SRAM_BASE &&
         address <= (SRAM_END - (8u * sizeof(uint32_t)));
}

void read_arm_fault_frame(const uint32_t *frame, uint32_t exc_return,
                          uint32_t *pc, uint32_t *lr, uint32_t *psr) {
#if HAL_TARGET_IS_RP2350_ARM
  // EXC_RETURN bit 4 is clear when an extended floating-point frame precedes
  // the basic eight-word exception frame.
  if ((exc_return & (1u << 4u)) == 0u) {
    frame += 18u;
  }
#else
  (void)exc_return;
#endif

  if (!address_is_readable_fault_frame(frame)) {
    *pc = 0u;
    *lr = 0u;
    *psr = 0u;
    return;
  }
  *pc = frame[6];
  *lr = frame[5];
  *psr = frame[7];
}
#endif

HAL_NORETURN __attribute__((no_stack_protector)) void
record_fault_and_reset(bool stack_overflow, uint32_t pc, uint32_t lr,
                       uint32_t psr, uint32_t cfsr = 0u, uint32_t hfsr = 0u,
                       uint32_t mmfar = 0u, uint32_t bfar = 0u) {
  uint32_t state = kStateSignature | kFlagFault;
  if (stack_overflow) {
    state |= kFlagStackOverflow;
  }

  // Invalidate both retained records first, then publish their signatures
  // only after the complete payload is visible. A reset in the middle can
  // therefore lose the new record, but can never accept stale fields as the
  // diagnostics for this fault.
  watchdog_hw->scratch[0] = 0u;
  s_retained_detail.signature = 0u;
  __dmb();
  watchdog_hw->scratch[1] = pc;
  watchdog_hw->scratch[2] = lr;
  watchdog_hw->scratch[3] = psr;
  s_retained_detail.cfsr = cfsr;
  s_retained_detail.hfsr = hfsr;
  s_retained_detail.mmfar = mmfar;
  s_retained_detail.bfar = bfar;
  __asm volatile("" ::: "memory");
  __dmb();
  s_retained_detail.signature = kDetailSignature;
  __dmb();
  watchdog_hw->scratch[0] = state;
  __dmb();

  // Arm an independent short reset deadline before any diagnostic I/O. A
  // broken peripheral can therefore never turn this terminal path into a
  // permanent hang; the call below is replaced with an immediate reset later.
  watchdog_reboot(0u, 0u, 4u);

  // Best effort only: write directly to any already-enabled hardware UART.
  // The bounded register polling uses no locks, interrupts, heap, or damaged
  // foreground stack. USB CDC is deliberately skipped because its panic-safe
  // state cannot be established after an arbitrary fault.
  if (stack_overflow) {
    static const char message[] = "STACK OVERFLOW; resetting\r\n";
    uart_hw_t *const ports[] = {uart0_hw, uart1_hw};
    for (uart_hw_t *const port : ports) {
      if ((port->cr & (UART_UARTCR_UARTEN_BITS | UART_UARTCR_TXE_BITS)) !=
              (UART_UARTCR_UARTEN_BITS | UART_UARTCR_TXE_BITS) ||
          (port->fr & (UART_UARTFR_TXFE_BITS | UART_UARTFR_BUSY_BITS)) !=
              UART_UARTFR_TXFE_BITS) {
        continue;
      }
      uint32_t budget = 250000u;
      for (const char *cursor = message; *cursor != '\0'; ++cursor) {
        while ((port->fr & UART_UARTFR_TXFF_BITS) != 0u) {
          if (budget == 0u) {
            break;
          }
          --budget;
        }
        if (budget == 0u) {
          break;
        }
        port->dr = static_cast<uint32_t>(static_cast<uint8_t>(*cursor));
      }
      while ((port->fr & UART_UARTFR_BUSY_BITS) != 0u) {
        if (budget == 0u) {
          break;
        }
        --budget;
      }
      break;
    }
  }

  watchdog_reboot(0u, 0u, 0u);
  while (true) {
    tight_loop_contents();
  }
}

#if HAL_TARGET_IS_RP2040
bool rp2040_fault_frame_reached_guard(const uint32_t *frame) {
#ifdef HAL_ENABLE_STACK_GUARD
  if (!stack_guard_registers_match()) {
    return false;
  }
  const uintptr_t frame_address = reinterpret_cast<uintptr_t>(frame);
  const uintptr_t guard = current_stack_guard_base();
  // Armv6-M exposes no fault address/status register. A frame beginning up to
  // one basic exception frame below the 32-byte guard is the narrowest useful
  // indication that MPU region 0 stopped a descending system stack.
  return frame_address >= (guard - (8u * sizeof(uint32_t))) &&
         frame_address <= (guard + kStackGuardSize);
#else
  (void)frame;
  return false;
#endif
}
#endif

#if HAL_TARGET_IS_RP2350_RISCV
#ifdef HAL_ENABLE_STACK_GUARD
bool riscv_instruction_address_is_readable(uintptr_t address, size_t size) {
  if (size == 0u) {
    return false;
  }
  const uintptr_t last = address + size - 1u;
  if (last < address) {
    return false;
  }
  return (address >= XIP_BASE && last < XIP_END) ||
         (address >= SRAM_BASE && last < SRAM_END);
}

uint32_t sign_extend_12(uint32_t value) {
  return (value & 0x800u) != 0u ? value | 0xfffff000u : value;
}

bool riscv_decode_fault_address(const uint32_t *registers, uint32_t cause,
                                uintptr_t mepc, uint32_t *address) {
  if (registers == nullptr || address == nullptr ||
      !riscv_instruction_address_is_readable(mepc, sizeof(uint16_t))) {
    return false;
  }

  const auto *instruction_ptr =
      reinterpret_cast<const volatile uint16_t *>(mepc);
  const uint16_t low = instruction_ptr[0];
  const uint32_t cause_code = cause & RVCSR_MCAUSE_CODE_BITS;
  const bool load_fault = cause_code == RVCSR_MCAUSE_CODE_VALUE_LOAD_FAULT;
  const bool store_fault = cause_code == RVCSR_MCAUSE_CODE_VALUE_STORE_FAULT;
  if ((cause & RVCSR_MCAUSE_INTERRUPT_BITS) != 0u ||
      (!load_fault && !store_fault)) {
    return false;
  }

  if ((low & 3u) == 3u) {
    if (!riscv_instruction_address_is_readable(mepc, sizeof(uint32_t))) {
      return false;
    }
    const uint32_t instruction =
        static_cast<uint32_t>(low) |
        (static_cast<uint32_t>(instruction_ptr[1]) << 16u);
    const uint32_t opcode = instruction & 0x7fu;
    const uint32_t funct3 = (instruction >> 12u) & 7u;
    const uint32_t rs1 = (instruction >> 15u) & 0x1fu;

    if (load_fault && opcode == 0x03u &&
        (funct3 == 0u || funct3 == 1u || funct3 == 2u || funct3 == 4u ||
         funct3 == 5u)) {
      const uint32_t immediate = sign_extend_12(instruction >> 20u);
      *address = registers[rs1] + immediate;
      return true;
    }
    if (store_fault && opcode == 0x23u && funct3 <= 2u) {
      const uint32_t immediate = sign_extend_12(((instruction >> 25u) << 5u) |
                                                ((instruction >> 7u) & 0x1fu));
      *address = registers[rs1] + immediate;
      return true;
    }
    if (store_fault && opcode == 0x2fu && funct3 == 2u) {
      *address = registers[rs1];
      return true;
    }
    return false;
  }

  const uint32_t quadrant = low & 3u;
  const uint32_t funct3 = (low >> 13u) & 7u;
  if (quadrant == 0u &&
      ((load_fault && funct3 == 2u) || (store_fault && funct3 == 6u))) {
    const uint32_t rs1 = 8u + ((low >> 7u) & 7u);
    const uint32_t immediate =
        ((low >> 7u) & 0x38u) | ((low << 1u) & 0x40u) | ((low >> 4u) & 0x04u);
    *address = registers[rs1] + immediate;
    return true;
  }
  if (quadrant == 2u && load_fault && funct3 == 2u &&
      ((low >> 7u) & 0x1fu) != 0u) {
    const uint32_t immediate =
        ((low >> 7u) & 0x20u) | ((low >> 2u) & 0x1cu) | ((low << 4u) & 0xc0u);
    *address = registers[2] + immediate;
    return true;
  }
  if (quadrant == 2u && store_fault && funct3 == 6u) {
    const uint32_t immediate = ((low >> 7u) & 0x3cu) | ((low >> 1u) & 0xc0u);
    *address = registers[2] + immediate;
    return true;
  }
  return false;
}
#endif

bool riscv_fault_accesses_stack_guard(const uint32_t *registers, uint32_t cause,
                                      uintptr_t mepc) {
#ifdef HAL_ENABLE_STACK_GUARD
  if (!stack_guard_registers_match()) {
    return false;
  }
  uint32_t fault_address = 0u;
  if (!riscv_decode_fault_address(registers, cause, mepc, &fault_address)) {
    return false;
  }
  const uintptr_t guard = current_stack_guard_base();
  return fault_address >= guard && fault_address < guard + kStackGuardSize;
#else
  (void)registers;
  (void)cause;
  (void)mepc;
  return false;
#endif
}
#endif

// Decode the reset cause straight from the watchdog reason register and the SoC
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
// Architecture entry stubs switch to a per-core emergency stack before these
// C functions run. This remains reliable when the protected stack is exhausted.
//
// An ARM basic exception frame contains:
//   [0]=R0 [1]=R1 [2]=R2 [3]=R3 [4]=R12 [5]=LR [6]=PC [7]=xPSR
// ─────────────────────────────────────────────────────────────────────────────

#if HAL_RP_ARCH_ARM
extern "C" HAL_NORETURN __attribute__((no_stack_protector)) void
jh_rp_arm_fault_capture(const uint32_t *frame, uint32_t exc_return) {
  bool stack_overflow = false;
#if HAL_TARGET_IS_RP2350_ARM
  const uint32_t cfsr = scb_hw->cfsr;
  stack_overflow = (cfsr & M33_CFSR_UFSR_STKOF_BITS) != 0u;
#else
  stack_overflow = rp2040_fault_frame_reached_guard(frame);
#endif

  uint32_t pc = 0u;
  uint32_t lr = 0u;
  uint32_t psr = 0u;
#if HAL_TARGET_IS_RP2350_ARM
  // STKOF and explicit memory/bus stacking failures can leave no complete
  // exception frame. Do not treat a merely SRAM-looking pointer as valid.
  const uint32_t stacking_errors = (1u << 4u) | (1u << 5u) |
                                   M33_CFSR_BFSR_STKERR_BITS |
                                   M33_CFSR_BFSR_LSPERR_BITS;
  if (!stack_overflow && (cfsr & stacking_errors) == 0u) {
    read_arm_fault_frame(frame, exc_return, &pc, &lr, &psr);
  }
  record_fault_and_reset(stack_overflow, pc, lr, psr, cfsr, scb_hw->hfsr,
                         scb_hw->mmfar, scb_hw->bfar);
#else
  // Armv6-M has no stacking-status register. The MPU is disabled in
  // HardFault (HFNMIENA=0), so retain any frame that passes the SRAM bounds
  // check, including a frame used by the stack-overflow heuristic itself.
  read_arm_fault_frame(frame, exc_return, &pc, &lr, &psr);
  record_fault_and_reset(stack_overflow, pc, lr, psr);
#endif
}
#elif HAL_TARGET_IS_RP2350_RISCV
extern "C" HAL_NORETURN __attribute__((no_stack_protector)) void
jh_rp_riscv_fault_capture(const uint32_t *registers, uint32_t cause,
                          uint32_t mepc) {
  const bool stack_overflow =
      riscv_fault_accesses_stack_guard(registers, cause, mepc);
  const uint32_t return_address = registers != nullptr ? registers[1] : 0u;
  record_fault_and_reset(stack_overflow, mepc, return_address, cause);
}
#endif

extern "C" HAL_NORETURN __attribute__((no_stack_protector)) void
jh_rp_stack_overflow_reset_c(uint32_t pc, uint32_t lr) {
  record_fault_and_reset(true, pc, lr, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public driver API
// ─────────────────────────────────────────────────────────────────────────────

void rp2040_fault_init(void) {
  // Keep the architecture entry object in static-library links. The strong
  // fault/trap symbols live in that object and otherwise have no ordinary
  // undefined reference that would make an archive linker extract it.
  jh_rp_fault_handlers_link_anchor();

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
    const bool fault_flag = (state & kFlagFault) != 0u;
    const bool alive_flag = (state & kFlagAlive) != 0u;
    const bool stack_overflow_flag = (state & kFlagStackOverflow) != 0u;

    if (fault_flag || stack_overflow_flag) {
      g_fault_info.valid = true;
      g_fault_info.pc = watchdog_hw->scratch[1];
      g_fault_info.lr = watchdog_hw->scratch[2];
      g_fault_info.psr = watchdog_hw->scratch[3];
      if (s_retained_detail.signature == kDetailSignature) {
        g_fault_info.cfsr = s_retained_detail.cfsr;
        g_fault_info.hfsr = s_retained_detail.hfsr;
        g_fault_info.mmfar = s_retained_detail.mmfar;
        g_fault_info.bfar = s_retained_detail.bfar;
      }

      // The overflow marker is more specific than the generic fault marker
      // and must win over the watchdog reset used to persist the capture.
      g_reset_reason = stack_overflow_flag ? HAL_RESET_REASON_STACK_OVERFLOW
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
    s_retained_detail.signature = 0u;
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
  g_fault_info.cfsr = 0u;
  g_fault_info.hfsr = 0u;
  g_fault_info.mmfar = 0u;
  g_fault_info.bfar = 0u;
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

hal_status_t rp2040_fault_stack_guard_init(void) {
#ifdef HAL_ENABLE_STACK_GUARD
  return stack_guard_registers_match() ? HAL_OK : HAL_EHW;
#else
  return HAL_EUNSUPPORTED;
#endif
}

void rp2040_fault_stack_guard_check(void) {}
