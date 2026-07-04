#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_config.h"
#include "../../hal_system.h"
#include "drivers/stm32g474/stm32g474_fault.h"
#include "drivers/stm32g474/stm32g474_system.h"
#include "port/stm32g474_regs.h"

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

hal_status_t
hal_system_get_current_architecture(hal_system_architecture_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }

  stm32g474_system_arch_info_t arch_info = {};
  stm32g474_system_get_arch_info(&arch_info);

#ifndef HAL_STM32_FLASH_SIZE
#define HAL_STM32_FLASH_SIZE (512u * 1024u)
#endif

  const uint32_t flash_reserved = (uint32_t)HAL_STM32_FLASH_EEPROM_SIZE +
                                  (uint32_t)HAL_STM32_FLASH_LITTLEFS_SIZE;
  const uint32_t flash_total = (uint32_t)HAL_STM32_FLASH_SIZE;
  const uint32_t flash_usable =
      flash_total > flash_reserved ? flash_total - flash_reserved : 0u;

  hal_system_architecture_t info = {};
  info.target_name = HAL_TARGET_NAME;
  info.backend_name = arch_info.backend_name;
  info.mcu = arch_info.mcu;
  info.mcu_subtype = arch_info.mcu_subtype;
  info.cpu_arch = arch_info.cpu_arch;
#if JH_STM32_HAL_SYSTEM_FREERTOS
  info.rtos_name = "FreeRTOS";
#else
  info.rtos_name = "none";
#endif
  info.cpu_cores = arch_info.cpu_cores;
#if defined(JH_STM32G474_HW)
  info.is_hardware = true;
#else
  info.is_hardware = false;
#endif
  info.has_fpu = arch_info.has_fpu;
  info.has_rtos = JH_STM32_HAL_SYSTEM_FREERTOS != 0;
  info.cpu_clock_hz = JH_G474_CORE_CLOCK_HZ;
  info.peripheral_clock_hz = JH_G474_PCLK1_HZ;
  info.flash_total_bytes = flash_total;
  info.flash_usable_bytes = flash_usable;
  info.flash_reserved_bytes = flash_reserved;
  info.ram_total_bytes = arch_info.ram_total_bytes;
  info.ram_usable_bytes = arch_info.ram_usable_bytes;
  info.heap_total_bytes = 0u;
  info.heap_free_bytes = hal_get_free_heap();
  info.stack_total_bytes = stm32g474_system_main_stack_bytes();
  info.uid_bytes = HAL_DEVICE_UID_BYTES;
  *out = info;
  return HAL_OK;
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
// (RCC->CSR reset flags, retained exception_info handoff, stack guard
// marker path). The wrappers below keep
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
