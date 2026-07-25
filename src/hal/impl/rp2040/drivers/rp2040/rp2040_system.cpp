/**
 * @file rp2040_system.cpp
 * @brief RP2040 SoC-specific system services driver implementation.
 *
 * See @c rp2040_system.h for the contract.
 */

#include "rp2040_system.h"
#include "../../rp2040_adc_shared.h"

#include <hardware/watchdog.h>
#include <malloc.h>
#include <pico/bootrom.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>
#include <stddef.h>
#include <string.h>

#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
#include <FreeRTOS.h>
#include <portable.h>
#endif

// Heap region bounds provided by the Pico SDK linker script:
// the allocator hands out memory between the end of .bss and the stack limit.
extern "C" char __StackLimit;
extern "C" char __bss_end__;

namespace {

// Latched once during C++ static initialization, before app_start() runs,
// and therefore before any rp2040_system_watchdog_enable() call can rewrite
// watchdog scratch register 4. This lets us tell apart a genuine application
// watchdog *timeout* (the watchdog was armed via watchdog_enable(), which
// writes WATCHDOG_NON_REBOOT_MAGIC into scratch[4]) from a *commanded*
// reboot through watchdog_reboot() -- used by picotool upload and BOOTSEL/UF2
// relaunch -- which sets scratch[4] to 0xb007c0d3 or 0.
// watchdog_reboot also uses the watchdog hardware, so plain
// watchdog_caused_reboot() reports true for a fresh flash and makes every
// upload look like a watchdog starvation. watchdog_enable_caused_reboot()
// additionally checks the scratch marker, so it is true ONLY for a real
// timeout after watchdog_enable().
bool g_watchdog_timeout_boot = false;

__attribute__((constructor)) void
rp2040_system_watchdog_latch_boot_reason(void) {
  g_watchdog_timeout_boot = watchdog_enable_caused_reboot();
}

} // namespace

void rp2040_system_get_arch_info(rp2040_system_arch_info_t *out) {
  if (out == nullptr) {
    return;
  }

#if defined(PICO_RP2350)
  out->mcu = "RP2350";
#if defined(PICO_RP2350A)
  out->mcu_subtype = "RP2350A";
#elif defined(PICO_RP2350B)
  out->mcu_subtype = "RP2350B";
#else
  out->mcu_subtype = "RP2350";
#endif
  out->ram_total_bytes = 520u * 1024u;
  out->ram_usable_bytes = 520u * 1024u;
#else
  out->mcu = "RP2040";
  out->mcu_subtype = "RP2040";
  out->cpu_arch = "ARM Cortex-M0+";
  out->ram_total_bytes = 264u * 1024u;
  out->ram_usable_bytes = 256u * 1024u;
#endif

#if HAL_TARGET_IS_RP2350_RISCV
  out->cpu_arch = "Hazard3 RISC-V";
#elif HAL_TARGET_IS_RP2350_ARM
  out->cpu_arch = "ARM Cortex-M33";
#endif

  out->backend_name = "rp/pico-sdk";
  out->cpu_cores = 2u;
#if defined(__ARM_FP) && (__ARM_FP != 0)
  out->has_fpu = true;
#else
  out->has_fpu = false;
#endif
}

void rp2040_system_watchdog_feed(void) { watchdog_update(); }

void rp2040_system_watchdog_enable(uint32_t ms, bool pause_on_debug) {
  watchdog_enable(ms, pause_on_debug);
}

bool rp2040_system_watchdog_caused_reboot(void) {
  return g_watchdog_timeout_boot;
}

void rp2040_system_idle(void) { tight_loop_contents(); }

uint32_t rp2040_system_get_free_heap(void) {
#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
  return (uint32_t)xPortGetFreeHeapSize();
#else
  // total heap span (end of .bss to the stack limit, from the linker)
  // minus the bytes currently allocated by the newlib allocator. This is
  // an upper bound; fragmentation can still make a single large
  // allocation fail even when this reports enough free bytes.
  const ptrdiff_t total = &__StackLimit - &__bss_end__;
  const ptrdiff_t used = (ptrdiff_t)mallinfo().uordblks;
  if (total <= used) {
    return 0u;
  }
  return (uint32_t)(total - used);
#endif
}

float rp2040_system_read_chip_temp(void) {
  const float vref = 3.3f;
  const uint16_t raw = rp2040_adc_read_temperature_raw();
  const float voltage = (float)raw * vref / 4096.0f;
  return 27.0f - (voltage - 0.706f) / 0.001721f;
}

void rp2040_system_enter_bootloader(void) {
  reset_usb_boot(0, 0);
  while (true) {
    tight_loop_contents();
  }
}

void rp2040_system_get_device_uid(uint8_t *uid) {
  if (uid == nullptr) {
    return;
  }
  pico_unique_board_id_t id;
  pico_get_unique_board_id(&id);
  /* PICO_UNIQUE_BOARD_ID_SIZE_BYTES is defined as 8. */
  memcpy(uid, id.id, 8u);
}

bool rp2040_system_get_device_uid_hex(char *buf, size_t buflen) {
  constexpr size_t kUidBytes = 8u;
  constexpr size_t kHexBufSize = (kUidBytes * 2u) + 1u;
  if (buf == nullptr) {
    return false;
  }
  if (buflen < kHexBufSize) {
    return false;
  }
  uint8_t uid[kUidBytes];
  rp2040_system_get_device_uid(uid);
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < kUidBytes; ++i) {
    buf[(i * 2u) + 0u] = kHex[(uid[i] >> 4) & 0x0Fu];
    buf[(i * 2u) + 1u] = kHex[uid[i] & 0x0Fu];
  }
  buf[kUidBytes * 2u] = '\0';
  return true;
}

bool rp2040_system_in_isr(void) { return __get_current_exception() != 0u; }
