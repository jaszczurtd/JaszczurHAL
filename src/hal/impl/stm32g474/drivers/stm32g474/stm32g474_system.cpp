/**
 * @file stm32g474_system.cpp
 * @brief STM32G474 SoC-specific system services (host-stub implementation).
 *
 * See @c stm32g474_system.h for the contract. This file compiles on both
 * the host (CMake @c .build/gate/stm32-host gate) and on a real ARM toolchain;
 * Cortex-M specific bits are guarded by @c __arm__ / @c __thumb__.
 */

#include "stm32g474_system.h"

#include "../../port/stm32g474_regs.h"
#include "stm32g474_fault.h"

#include <string.h>

#if defined(HAL_ENABLE_FREERTOS) && !defined(JH_STM32G474_HW)
#include <FreeRTOS.h>
#include <task.h>
#endif

#ifdef JH_STM32G474_HW
/* Real hardware time base lives in port/system_stm32g474.c (SysTick). */
extern "C" uint32_t stm32g474_systick_millis(void);
extern "C" uint32_t stm32g474_systick_micros(void);
extern "C" uint64_t stm32g474_systick_micros64(void);
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
uint64_t g_micros64 = 0u;

uint8_t g_device_uid[8] = {0x47, 0x34, 0x74, 0x00, 0x00, 0x00, 0x00, 0x01};
#endif

#ifndef JH_STM32G474_HW
uint32_t g_free_heap = 0u;
bool g_watchdog_enabled = false;
bool g_watchdog_fed = false;
bool g_watchdog_pause_on_debug = false;
uint32_t g_watchdog_prescaler = 0u;
uint32_t g_watchdog_reload = 0u;
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

#if defined(JH_STM32G474_SYSTEM_TESTING) && !defined(JH_STM32G474_HW)
extern "C" void stm32g474_system_test_set_micros64(uint64_t micros) {
  g_micros64 = micros;
}

extern "C" void stm32g474_system_test_reset_watchdog(void) {
  g_watchdog_enabled = false;
  g_watchdog_fed = false;
  g_watchdog_pause_on_debug = false;
  g_watchdog_prescaler = 0u;
  g_watchdog_reload = 0u;
}

extern "C" bool stm32g474_system_test_watchdog_enabled(void) {
  return g_watchdog_enabled;
}

extern "C" uint32_t stm32g474_system_test_watchdog_prescaler(void) {
  return g_watchdog_prescaler;
}

extern "C" uint32_t stm32g474_system_test_watchdog_reload(void) {
  return g_watchdog_reload;
}

extern "C" bool stm32g474_system_test_watchdog_pause_on_debug(void) {
  return g_watchdog_pause_on_debug;
}
#endif

struct WatchdogConfig {
  uint32_t prescaler_code;
  uint32_t reload;
};

static bool watchdog_compute_config(uint32_t ms, WatchdogConfig *out) {
  constexpr uint32_t kLsiHz = 32000u;
  constexpr uint32_t kPrescalers[] = {4u, 8u, 16u, 32u, 64u, 128u, 256u};

  if (ms == 0u || out == nullptr) {
    return false;
  }

  for (uint32_t code = 0u;
       code < (sizeof(kPrescalers) / sizeof(kPrescalers[0])); ++code) {
    const uint64_t numerator = (uint64_t)ms * kLsiHz;
    const uint64_t denominator = (uint64_t)1000u * kPrescalers[code];
    const uint64_t ticks = (numerator + denominator - 1u) / denominator;
    if (ticks >= 1u && ticks <= (uint64_t)IWDG_RLR_MASK + 1u) {
      out->prescaler_code = code;
      out->reload = (uint32_t)(ticks - 1u);
      return true;
    }
  }
  return false;
}

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
                                             : (uint32_t)(g_micros64 / 1000u);
#else
  return (uint32_t)(g_micros64 / 1000u);
#endif
}

uint32_t stm32g474_system_micros(void) {
#ifdef JH_STM32G474_HW
  return stm32g474_systick_micros();
#elif defined(HAL_ENABLE_FREERTOS)
  return host_freertos_scheduler_has_ticks() ? (host_freertos_millis() * 1000u)
                                             : (uint32_t)g_micros64;
#else
  return (uint32_t)g_micros64;
#endif
}

uint64_t stm32g474_system_micros64(void) {
#ifdef JH_STM32G474_HW
  return stm32g474_systick_micros64();
#elif defined(HAL_ENABLE_FREERTOS)
  return host_freertos_scheduler_has_ticks()
             ? ((uint64_t)host_freertos_millis() * 1000u)
             : g_micros64;
#else
  return g_micros64;
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
  g_micros64 += (uint64_t)ms * 1000u;
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
  g_micros64 += us;
#endif
}

void stm32g474_system_watchdog_feed(void) {
#ifdef JH_STM32G474_HW
  IWDG_KR = IWDG_KR_RELOAD;
#else
  g_watchdog_fed = true;
#endif
}

hal_status_t stm32g474_system_watchdog_enable(uint32_t ms,
                                              bool pause_on_debug) {
  WatchdogConfig config = {};
  if (!watchdog_compute_config(ms, &config)) {
    return HAL_EINVAL;
  }

#ifdef JH_STM32G474_HW
  if (pause_on_debug) {
    DBGMCU_APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP;
  } else {
    DBGMCU_APB1FZR1 &= ~DBGMCU_APB1FZR1_DBG_IWDG_STOP;
  }

  /* Starting IWDG also forces LSI on. Configuration remains writable only
   * after the write-access key; wait for both shadow-register updates before
   * loading the requested period. */
  IWDG_KR = IWDG_KR_START;
  IWDG_KR = IWDG_KR_WRITE_ACCESS;
  IWDG_PR = config.prescaler_code;
  IWDG_RLR = config.reload;

  constexpr uint32_t kUpdatePollBudget = 20000000u;
  uint32_t budget = kUpdatePollBudget;
  while ((IWDG_SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0u && budget > 0u) {
    --budget;
  }
  if ((IWDG_SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0u) {
    return HAL_ETIMEOUT;
  }
  IWDG_KR = IWDG_KR_RELOAD;
#else
  g_watchdog_enabled = true;
  g_watchdog_fed = false;
  g_watchdog_pause_on_debug = pause_on_debug;
  g_watchdog_prescaler = config.prescaler_code;
  g_watchdog_reload = config.reload;
#endif
  return HAL_OK;
}

bool stm32g474_system_watchdog_caused_reboot(void) {
  return stm32g474_fault_get_reset_reason() == HAL_RESET_REASON_WATCHDOG;
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
