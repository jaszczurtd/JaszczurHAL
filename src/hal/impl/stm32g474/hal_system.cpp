#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_config.h"
#include "../../hal_system.h"
#include "drivers/stm32g474/stm32g474_fault.h"
#include "drivers/stm32g474/stm32g474_system.h"

#if defined(HAL_ENABLE_FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif

#if defined(HAL_ENABLE_FREERTOS)
#define JH_STM32_HAL_SYSTEM_FREERTOS 1
#else
#define JH_STM32_HAL_SYSTEM_FREERTOS 0
#endif

#if JH_STM32_HAL_SYSTEM_FREERTOS
extern "C" bool hal_stm32g474_critical_section_active(void);

static bool hal_freertos_can_block_current_context(void) {
  return !stm32g474_system_in_isr() &&
         !hal_stm32g474_critical_section_active() &&
         xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

static TickType_t hal_ms_to_ticks(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if ((ms > 0u) && (ticks == 0u)) {
    ticks = 1u;
  }
  return ticks;
}

static void hal_freertos_idle_fallback(void) {
#if defined(__arm__) || defined(__thumb__)
  __asm volatile("nop");
#endif
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Time / delays
// ─────────────────────────────────────────────────────────────────────────────

uint32_t hal_millis(void) { return stm32g474_system_millis(); }
uint32_t hal_micros(void) { return stm32g474_system_micros(); }
uint64_t hal_micros64(void) { return stm32g474_system_micros64(); }
void hal_delay_us(uint32_t us) { stm32g474_system_delay_us(us); }

void hal_delay_ms(uint32_t ms) {
#if JH_STM32_HAL_SYSTEM_FREERTOS
  if (hal_freertos_can_block_current_context()) {
    if (ms == 0u) {
      taskYIELD();
    } else {
      vTaskDelay(hal_ms_to_ticks(ms));
    }
    return;
  }
#endif

  stm32g474_system_delay_ms(ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Watchdog, idle, ISR-context check, free-heap, on-die temp, bootloader
// entry, device UID. All STM32G474-specific (or planned-specific) bindings
// live in the SoC driver; this layer is pure dispatch.
// ─────────────────────────────────────────────────────────────────────────────

void hal_watchdog_feed(void) { stm32g474_system_watchdog_feed(); }

void hal_watchdog_enable(uint32_t ms, bool pause_on_debug) {
  stm32g474_system_watchdog_enable(ms, pause_on_debug);
}

bool hal_watchdog_caused_reboot(void) {
  return stm32g474_system_watchdog_caused_reboot();
}

void hal_idle(void) {
#if JH_STM32_HAL_SYSTEM_FREERTOS
  if (hal_freertos_can_block_current_context()) {
    taskYIELD();
    return;
  }
  hal_freertos_idle_fallback();
#else
  stm32g474_system_idle();
#endif
}

bool hal_in_isr(void) { return stm32g474_system_in_isr(); }

uint32_t hal_get_free_heap(void) { return stm32g474_system_get_free_heap(); }

float hal_read_chip_temp(void) { return stm32g474_system_read_chip_temp(); }

void hal_enter_bootloader(void) { stm32g474_system_enter_bootloader(); }

void hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]) {
  stm32g474_system_get_device_uid(uid);
}

bool hal_get_device_uid_hex(char *buf, size_t buflen) {
  return stm32g474_system_get_device_uid_hex(buf, buflen);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fault / crash diagnostics
//
// All architecture-specific logic lives in the STM32G474 SoC driver
// (currently a no-op stub; planned real impl will use RCC->CSR,
// SCB->{CFSR,HFSR,MMFAR,BFAR} and TAMP->BKPxR). The wrappers below keep
// the HAL surface uniform across backends. `hal_reset_reason_str` is a
// pure mapping and stays here.
// ─────────────────────────────────────────────────────────────────────────────

void hal_fault_subsystem_init(void) { stm32g474_fault_init(); }

hal_reset_reason_t hal_get_reset_reason(void) {
  return stm32g474_fault_get_reset_reason();
}

const char *hal_reset_reason_str(hal_reset_reason_t reason) {
  switch (reason) {
  case HAL_RESET_REASON_POWER_ON:
    return "POWER_ON";
  case HAL_RESET_REASON_RUN_PIN:
    return "RUN_PIN";
  case HAL_RESET_REASON_SOFT:
    return "SOFT";
  case HAL_RESET_REASON_WATCHDOG:
    return "WATCHDOG";
  case HAL_RESET_REASON_DEBUG:
    return "DEBUG";
  case HAL_RESET_REASON_GLITCH:
    return "GLITCH";
  case HAL_RESET_REASON_BROWNOUT:
    return "BROWNOUT";
  case HAL_RESET_REASON_HARDFAULT:
    return "HARDFAULT";
  case HAL_RESET_REASON_STACK_OVERFLOW:
    return "STACK_OVERFLOW";
  case HAL_RESET_REASON_UNKNOWN:
  default:
    return "UNKNOWN";
  }
}

bool hal_get_last_fault(hal_fault_info_t *out) {
  return stm32g474_fault_get_last_fault(out);
}

void hal_clear_last_fault(void) { stm32g474_fault_clear_last_fault(); }

bool hal_last_boot_was_brownout(void) {
  return stm32g474_fault_brownout_suspected();
}

void hal_alive_mark(void) { stm32g474_fault_alive_mark(); }

bool hal_stack_guard_init(void) { return stm32g474_fault_stack_guard_init(); }

void hal_stack_guard_check(void) { stm32g474_fault_stack_guard_check(); }

#endif // HAL_TARGET_IS_STM32G474
