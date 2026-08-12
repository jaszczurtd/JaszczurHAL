/**
 * @file stm32g474_system.cpp
 * @brief STM32G474 SoC-specific system services (host-stub implementation).
 *
 * See @c stm32g474_system.h for the contract. This file compiles on both
 * the host (CMake @c .build/gate/stm32-host gate) and on a real ARM toolchain;
 * Cortex-M specific bits are guarded by @c __arm__ / @c __thumb__.
 */

#include "stm32g474_system.h"

#include <string.h>

#if defined(HAL_ENABLE_FREERTOS) && !defined(JH_STM32G474_HW)
#include <FreeRTOS.h>
#include <task.h>
#endif

#ifdef JH_STM32G474_HW
/* Real hardware time base lives in port/system_stm32g474.c (SysTick). */
#include "../../port/stm32g474_regs.h"
extern "C" uint32_t stm32g474_systick_millis(void);
extern "C" uint32_t stm32g474_systick_micros(void);
extern uint8_t __jh_stm32_stack_top;
extern uint8_t __jh_stm32_stack_limit;
extern "C" uint32_t stm32g474_runtime_heap_total_bytes(void);
extern "C" uint32_t stm32g474_runtime_heap_free_bytes(void);

#ifdef HAL_ENABLE_FREERTOS
static void stm32g474_delay_cycles(uint32_t cycles) {
  const uint32_t start = DWT_CYCCNT;
  while ((uint32_t)(DWT_CYCCNT - start) < cycles) {
    __asm volatile("nop");
  }
}

static void stm32g474_delay_us_busy(uint32_t us) {
  const uint32_t cycles_per_us = JH_G474_CORE_CLOCK_HZ / 1000000u;
  const uint32_t max_us_per_chunk = 0xFFFFFFFFu / cycles_per_us;

  while (us > max_us_per_chunk) {
    stm32g474_delay_cycles(max_us_per_chunk * cycles_per_us);
    us -= max_us_per_chunk;
  }
  stm32g474_delay_cycles(us * cycles_per_us);
}
#endif

static uint32_t stm32g474_symbol_span_bytes(const uint8_t *start,
                                            const uint8_t *end) {
  return (uint32_t)((uintptr_t)end - (uintptr_t)start);
}
#endif

namespace {

#ifndef JH_STM32G474_HW
/* Host-stub time source and placeholder UID (replaced by SysTick / UID_BASE
 * on the hardware build). */
uint32_t g_millis = 0u;
uint32_t g_micros = 0u;

uint8_t g_device_uid[8] = {0x47, 0x34, 0x74, 0x00, 0x00, 0x00, 0x00, 0x01};
#endif

bool g_watchdog_fed = false;
bool g_watchdog_caused_reboot = false;
#ifndef JH_STM32G474_HW
uint32_t g_free_heap = 0u;
#endif
float g_chip_temp_c = 0.0f;

#if defined(HAL_ENABLE_FREERTOS) && !defined(JH_STM32G474_HW)
static bool host_freertos_scheduler_has_ticks(void) {
  return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
}

static uint32_t host_freertos_millis(void) {
  return (uint32_t)xTaskGetTickCount();
}
#endif

} // namespace

uint32_t stm32g474_system_main_stack_bytes(void) {
#ifdef JH_STM32G474_HW
  return stm32g474_symbol_span_bytes(&__jh_stm32_stack_limit,
                                     &__jh_stm32_stack_top);
#else
  return 0x800u;
#endif
}

uint32_t stm32g474_system_millis(void) {
#ifdef JH_STM32G474_HW
  return stm32g474_systick_millis();
#elif defined(HAL_ENABLE_FREERTOS)
  return host_freertos_scheduler_has_ticks() ? host_freertos_millis()
                                             : g_millis;
#else
  return g_millis;
#endif
}

uint32_t stm32g474_system_micros(void) {
#ifdef JH_STM32G474_HW
  return stm32g474_systick_micros();
#elif defined(HAL_ENABLE_FREERTOS)
  return host_freertos_scheduler_has_ticks() ? (host_freertos_millis() * 1000u)
                                             : g_micros;
#else
  return g_micros;
#endif
}

uint64_t stm32g474_system_micros64(void) {
#ifdef JH_STM32G474_HW
  return (uint64_t)stm32g474_systick_micros();
#elif defined(HAL_ENABLE_FREERTOS)
  return host_freertos_scheduler_has_ticks()
             ? ((uint64_t)host_freertos_millis() * 1000u)
             : (uint64_t)g_micros;
#else
  return (uint64_t)g_micros;
#endif
}

void stm32g474_system_delay_ms(uint32_t ms) {
#ifdef JH_STM32G474_HW
#ifdef HAL_ENABLE_FREERTOS
  while (ms > 0u) {
    stm32g474_delay_us_busy(1000u);
    --ms;
  }
#else
  const uint32_t start = stm32g474_systick_millis();
  while ((stm32g474_systick_millis() - start) < ms) {
    __asm volatile("wfi");
  }
#endif
#else
  g_millis += ms;
  g_micros += (ms * 1000u);
#endif
}

void stm32g474_system_delay_us(uint32_t us) {
#ifdef JH_STM32G474_HW
#ifdef HAL_ENABLE_FREERTOS
  stm32g474_delay_us_busy(us);
#else
  const uint32_t start = stm32g474_systick_micros();
  while ((stm32g474_systick_micros() - start) < us) {
    /* busy-wait at sub-millisecond resolution */
  }
#endif
#else
  g_micros += us;
  g_millis = g_micros / 1000u;
#endif
}

void stm32g474_system_watchdog_feed(void) { g_watchdog_fed = true; }

void stm32g474_system_watchdog_enable(uint32_t ms, bool pause_on_debug) {
  (void)ms;
  (void)pause_on_debug;
  g_watchdog_fed = false;
}

bool stm32g474_system_watchdog_caused_reboot(void) {
  return g_watchdog_caused_reboot;
}

void stm32g474_system_idle(void) {
#ifdef JH_STM32G474_HW
  __asm volatile("wfi");
#endif
}

bool stm32g474_system_in_isr(void) {
#if defined(__arm__) || defined(__thumb__) || defined(__aarch64__)
  /* On Cortex-M, IPSR is zero in Thread mode and equal to the active
   * exception number in Handler mode. Mask to the documented 9-bit
   * exception-number field. */
  uint32_t ipsr;
  __asm__ __volatile__("MRS %0, ipsr" : "=r"(ipsr));
  return (ipsr & 0x1FFu) != 0u;
#else
  /* Host/sanity builds of stm32_lib use a desktop compiler and cannot
   * execute Cortex-M specific instructions. */
  return false;
#endif
}

uint32_t stm32g474_system_heap_total_bytes(void) {
#ifdef JH_STM32G474_HW
  return stm32g474_runtime_heap_total_bytes();
#else
  return 0u;
#endif
}

uint32_t stm32g474_system_get_free_heap(void) {
#ifdef JH_STM32G474_HW
  return stm32g474_runtime_heap_free_bytes();
#else
  return g_free_heap;
#endif
}

float stm32g474_system_read_chip_temp(void) { return g_chip_temp_c; }

void stm32g474_system_enter_bootloader(void) {
  /* STM32G474 TODO: deinit + jump to STM32 system bootloader. */
}

void stm32g474_system_get_device_uid(uint8_t *uid) {
  if (uid == nullptr) {
    return;
  }
#ifdef JH_STM32G474_HW
  /* Fold the 96-bit factory UID (3 words at UID_BASE) into 8 bytes. */
  const uint32_t w0 = JH_REG32(STM32_UID_BASE + 0u);
  const uint32_t w1 = JH_REG32(STM32_UID_BASE + 4u);
  const uint32_t w2 = JH_REG32(STM32_UID_BASE + 8u);
  const uint32_t lo = w0 ^ w2;
  const uint32_t hi = w1 ^ w2;
  uid[0] = (uint8_t)(lo >> 0);
  uid[1] = (uint8_t)(lo >> 8);
  uid[2] = (uint8_t)(lo >> 16);
  uid[3] = (uint8_t)(lo >> 24);
  uid[4] = (uint8_t)(hi >> 0);
  uid[5] = (uint8_t)(hi >> 8);
  uid[6] = (uint8_t)(hi >> 16);
  uid[7] = (uint8_t)(hi >> 24);
#else
  memcpy(uid, g_device_uid, 8u);
#endif
}

bool stm32g474_system_get_device_uid_hex(char *buf, size_t buflen) {
  constexpr size_t kUidBytes = 8u;
  constexpr size_t kHexBufSize = (kUidBytes * 2u) + 1u;
  if (buf == nullptr) {
    return false;
  }
  if (buflen < kHexBufSize) {
    return false;
  }
  uint8_t uid[kUidBytes];
  stm32g474_system_get_device_uid(uid);
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < kUidBytes; ++i) {
    buf[(i * 2u) + 0u] = kHex[(uid[i] >> 4) & 0x0Fu];
    buf[(i * 2u) + 1u] = kHex[uid[i] & 0x0Fu];
  }
  buf[kUidBytes * 2u] = '\0';
  return true;
}
