#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "drivers/stm32g474/stm32g474_fault.h"
#include "drivers/stm32g474/stm32g474_system.h"
#include "hal/core/hal_config.h"
#include "hal/network/jh_network_architecture.h"
#include "hal/system/hal_system.h"
#include "hal/system/hal_system_common.h"
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

hal_status_t hal_watchdog_enable(uint32_t ms, bool pause_on_debug) {
  (void)ms;
  (void)pause_on_debug;
  return HAL_EUNSUPPORTED;
}

bool hal_watchdog_caused_reboot(void) {
  return stm32g474_system_watchdog_caused_reboot();
}

hal_status_t
hal_system_get_current_architecture(hal_system_architecture_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }

  const uint32_t flash_reserved = (uint32_t)HAL_STM32_FLASH_EEPROM_SIZE +
                                  (uint32_t)HAL_STM32_FLASH_LITTLEFS_SIZE;
  const uint32_t flash_total = (uint32_t)HAL_BOARD_EXPECTED_FLASH_BYTES;
  const uint32_t flash_usable =
      flash_total > flash_reserved ? flash_total - flash_reserved : 0u;

  hal_system_architecture_t info = {};
  info.target_name = HAL_TARGET_DESCRIPTOR_ID;
  info.backend_name = HAL_TARGET_BACKEND_NAME;
  info.mcu = HAL_TARGET_MCU_NAME;
  info.mcu_subtype = HAL_TARGET_MCU_SUBTYPE_NAME;
  info.cpu_arch = HAL_TARGET_CPU_ARCH_NAME;
#if JH_STM32_HAL_SYSTEM_FREERTOS
  info.rtos_name = "FreeRTOS";
#else
  info.rtos_name = "none";
#endif
  info.cpu_cores = HAL_TARGET_CPU_CORES;
#if defined(JH_STM32G474_HW)
  info.is_hardware = true;
#else
  info.is_hardware = false;
#endif
  info.has_fpu = HAL_TARGET_HAS_FPU != 0;
  info.has_rtos = JH_STM32_HAL_SYSTEM_FREERTOS != 0;
  info.cpu_clock_hz = JH_G474_CORE_CLOCK_HZ;
  info.peripheral_clock_hz = JH_G474_PCLK1_HZ;
  info.flash_total_bytes = flash_total;
  info.flash_usable_bytes = flash_usable;
  info.flash_reserved_bytes = flash_reserved;
  info.ram_total_bytes = HAL_TARGET_RAM_TOTAL_BYTES;
  info.ram_usable_bytes = HAL_TARGET_RAM_USABLE_BYTES;
  info.heap_total_bytes = stm32g474_system_heap_total_bytes();
  info.heap_free_bytes = hal_get_free_heap();
  info.stack_total_bytes = stm32g474_system_main_stack_bytes();
  info.uid_bytes = HAL_DEVICE_UID_BYTES;
  jh_network_architecture_fill(&info);
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

hal_status_t hal_read_chip_temp_ex(float *out_celsius) {
  if (out_celsius == nullptr) {
    return HAL_EINVAL;
  }
  return HAL_EUNSUPPORTED;
}

float hal_read_chip_temp(void) {
  float celsius = 0.0f;
  (void)hal_read_chip_temp_ex(&celsius);
  return celsius;
}

hal_status_t hal_enter_bootloader(void) { return HAL_EUNSUPPORTED; }

hal_status_t hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]) {
  if (uid == nullptr) {
    return HAL_EINVAL;
  }
  stm32g474_system_get_device_uid(uid);
  return HAL_OK;
}

hal_status_t hal_get_device_uid_hex_ex(char *buf, size_t buflen) {
  if (buf == nullptr) {
    return HAL_EINVAL;
  }
  if (buflen < HAL_DEVICE_UID_HEX_BUF_SIZE) {
    return HAL_EOVERFLOW;
  }
  return hal_status_from_bool(stm32g474_system_get_device_uid_hex(buf, buflen),
                              HAL_EIO);
}

bool hal_get_device_uid_hex(char *buf, size_t buflen) {
  return hal_status_to_bool(hal_get_device_uid_hex_ex(buf, buflen));
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
  return jh_hal_reset_reason_str(reason);
}

hal_status_t hal_get_last_fault_ex(hal_fault_info_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }
  return stm32g474_fault_get_last_fault(out) ? HAL_OK : HAL_ENOENT;
}

bool hal_get_last_fault(hal_fault_info_t *out) {
  return hal_status_to_bool(hal_get_last_fault_ex(out));
}

void hal_clear_last_fault(void) { stm32g474_fault_clear_last_fault(); }

bool hal_last_boot_was_brownout(void) {
  return stm32g474_fault_brownout_suspected();
}

void hal_alive_mark(void) { stm32g474_fault_alive_mark(); }

hal_status_t hal_stack_guard_init_ex(void) {
  return stm32g474_fault_stack_guard_init() ? HAL_OK : HAL_EUNSUPPORTED;
}

bool hal_stack_guard_init(void) {
  return hal_status_to_bool(hal_stack_guard_init_ex());
}

void hal_stack_guard_check(void) { stm32g474_fault_stack_guard_check(); }

#endif // HAL_TARGET_IS_STM32G474
