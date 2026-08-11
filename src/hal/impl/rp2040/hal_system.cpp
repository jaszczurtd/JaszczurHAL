#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "drivers/rp2040/rp2040_fault.h"
#include "drivers/rp2040/rp2040_system.h"
#include "hal/network/jh_network_architecture.h"
#include "hal/system/hal_system.h"
#include "hal/system/hal_system_common.h"
#include <hardware/clocks.h>
#include <hardware/regs/addressmap.h>
#include <pico/time.h>

#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif

#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
#define JH_RP2040_HAL_SYSTEM_FREERTOS 1
#else
#define JH_RP2040_HAL_SYSTEM_FREERTOS 0
#endif

extern "C" bool hal_rp2040_critical_section_active(void);

#if JH_RP2040_HAL_SYSTEM_FREERTOS
static bool hal_freertos_can_block_current_context(void) {
  return !portCHECK_IF_IN_ISR() && !hal_rp2040_critical_section_active() &&
         xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

static TickType_t hal_ms_to_ticks(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (ms > 0u && ticks == 0u) {
    ticks = 1u;
  }
  return ticks;
}
#endif

static bool hal_rp2040_can_sleep_current_context(void) {
  return !rp2040_system_in_isr() && !hal_rp2040_critical_section_active();
}

uint32_t hal_millis(void) { return to_ms_since_boot(get_absolute_time()); }

uint32_t hal_micros(void) { return (uint32_t)time_us_64(); }

uint64_t hal_micros64(void) { return time_us_64(); }

void hal_delay_ms(uint32_t ms) {
#if JH_RP2040_HAL_SYSTEM_FREERTOS
  if (hal_freertos_can_block_current_context()) {
    if (ms == 0u) {
      taskYIELD();
    } else {
      vTaskDelay(hal_ms_to_ticks(ms));
    }
    return;
  }
#endif
  if (hal_rp2040_can_sleep_current_context()) {
    sleep_ms(ms);
  } else {
    busy_wait_ms(ms);
  }
}

void hal_delay_us(uint32_t us) {
  /* busy_wait_us() is interrupt-independent and therefore safe inside
   * hal_critical_section_*(), including OneWire bit timing. */
  busy_wait_us(us);
}

// ─────────────────────────────────────────────────────────────────────────────
// Watchdog, idle, ISR-context check, free-heap, on-die temp, bootloader
// entry, device UID. All RP2040 / pico-sdk bindings and the Cortex-M IPSR
// query live in the SoC driver; this layer is pure dispatch.
// ─────────────────────────────────────────────────────────────────────────────

void hal_watchdog_feed(void) { rp2040_system_watchdog_feed(); }

hal_status_t hal_watchdog_enable(uint32_t ms, bool pause_on_debug) {
#if defined(PICO_RP2350)
  constexpr uint32_t kMaxWatchdogDelayMs = 16777u;
#else
  /* RP2040-E1 makes the counter decrement twice per watchdog tick. */
  constexpr uint32_t kMaxWatchdogDelayMs = 8388u;
#endif
  if (ms > kMaxWatchdogDelayMs) {
    return HAL_EINVAL;
  }
  rp2040_system_watchdog_enable(ms, pause_on_debug);
  return HAL_OK;
}

bool hal_watchdog_caused_reboot(void) {
  return rp2040_system_watchdog_caused_reboot();
}

hal_status_t
hal_system_get_current_architecture(hal_system_architecture_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }

  rp2040_system_arch_info_t arch_info = {};
  rp2040_system_get_arch_info(&arch_info);

#if defined(PICO_FLASH_SIZE_BYTES)
  const uint32_t flash_total = (uint32_t)PICO_FLASH_SIZE_BYTES;
#else
  const uint32_t flash_total = 0u;
#endif

#if HAL_RP_OTA_SLOT_SIZE > 0u
  const uint32_t flash_usable = (uint32_t)HAL_RP_OTA_SLOT_SIZE;
  const uint32_t flash_reserved =
      flash_usable <= flash_total ? flash_total - flash_usable : flash_total;
#else
  const uint32_t flash_reserved =
      (uint32_t)HAL_RP_FLASH_EEPROM_SIZE + (uint32_t)HAL_RP_FLASH_LITTLEFS_SIZE;
  const uint32_t flash_usable =
      flash_reserved <= flash_total ? flash_total - flash_reserved : 0u;
#endif

#if defined(PICO_STACK_SIZE)
  const uint32_t stack_total = (uint32_t)PICO_STACK_SIZE;
#elif defined(HAL_RP2040_STACK_SIZE)
  const uint32_t stack_total = (uint32_t)HAL_RP2040_STACK_SIZE;
#else
  const uint32_t stack_total = 0u;
#endif

  hal_system_architecture_t info = {};
  info.target_name = HAL_TARGET_NAME;
  info.backend_name = arch_info.backend_name;
  info.mcu = arch_info.mcu;
  info.mcu_subtype = arch_info.mcu_subtype;
  info.cpu_arch = arch_info.cpu_arch;
#if JH_RP2040_HAL_SYSTEM_FREERTOS
  info.rtos_name = "FreeRTOS SMP";
#else
  info.rtos_name = "none";
#endif
  info.cpu_cores = arch_info.cpu_cores;
  info.is_hardware = true;
  info.has_fpu = arch_info.has_fpu;
  info.has_rtos = JH_RP2040_HAL_SYSTEM_FREERTOS != 0;
  info.cpu_clock_hz = clock_get_hz(clk_sys);
  info.peripheral_clock_hz = clock_get_hz(clk_peri);
  info.flash_total_bytes = flash_total;
  info.flash_usable_bytes = flash_usable;
  info.flash_reserved_bytes = flash_reserved;
  info.ram_total_bytes = arch_info.ram_total_bytes;
  info.ram_usable_bytes = arch_info.ram_usable_bytes;
#if JH_RP2040_HAL_SYSTEM_FREERTOS
  info.heap_total_bytes = (uint32_t)configTOTAL_HEAP_SIZE;
#else
  info.heap_total_bytes = 0u;
#endif
  info.heap_free_bytes = hal_get_free_heap();
  info.stack_total_bytes = stack_total;
  info.uid_bytes = HAL_DEVICE_UID_BYTES;
  jh_network_architecture_fill(&info);
  *out = info;
  return HAL_OK;
}

void hal_idle(void) {
#if JH_RP2040_HAL_SYSTEM_FREERTOS
  if (hal_freertos_can_block_current_context()) {
    taskYIELD();
    return;
  }
#endif
  rp2040_system_idle();
}

bool hal_in_isr(void) { return rp2040_system_in_isr(); }

uint32_t hal_get_free_heap(void) { return rp2040_system_get_free_heap(); }

hal_status_t hal_read_chip_temp_ex(float *out_celsius) {
  if (out_celsius == nullptr) {
    return HAL_EINVAL;
  }
  *out_celsius = rp2040_system_read_chip_temp();
  return HAL_OK;
}

float hal_read_chip_temp(void) {
  float celsius = 0.0f;
  (void)hal_read_chip_temp_ex(&celsius);
  return celsius;
}

hal_status_t hal_enter_bootloader(void) {
  rp2040_system_enter_bootloader();
  return HAL_OK;
}

hal_status_t hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]) {
  if (uid == nullptr) {
    return HAL_EINVAL;
  }
  rp2040_system_get_device_uid(uid);
  return HAL_OK;
}

hal_status_t hal_get_device_uid_hex_ex(char *buf, size_t buflen) {
  if (buf == nullptr) {
    return HAL_EINVAL;
  }
  if (buflen < HAL_DEVICE_UID_HEX_BUF_SIZE) {
    return HAL_EOVERFLOW;
  }
  return hal_status_from_bool(rp2040_system_get_device_uid_hex(buf, buflen),
                              HAL_EIO);
}

bool hal_get_device_uid_hex(char *buf, size_t buflen) {
  return hal_status_to_bool(hal_get_device_uid_hex_ex(buf, buflen));
}

// ─────────────────────────────────────────────────────────────────────────────
// Fault / crash diagnostics
//
// All architecture-specific logic (HardFault handler, retained scratch
// layout, stack canary placement, reset-reason latching) lives in the
// RP2040 SoC driver. The wrappers below keep the HAL surface uniform
// across backends.
// ─────────────────────────────────────────────────────────────────────────────

void hal_fault_subsystem_init(void) { rp2040_fault_init(); }

hal_reset_reason_t hal_get_reset_reason(void) {
  return rp2040_fault_get_reset_reason();
}

const char *hal_reset_reason_str(hal_reset_reason_t reason) {
  return jh_hal_reset_reason_str(reason);
}

hal_status_t hal_get_last_fault_ex(hal_fault_info_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }
  return rp2040_fault_get_last_fault(out) ? HAL_OK : HAL_ENOENT;
}

bool hal_get_last_fault(hal_fault_info_t *out) {
  return hal_status_to_bool(hal_get_last_fault_ex(out));
}

void hal_clear_last_fault(void) { rp2040_fault_clear_last_fault(); }

bool hal_last_boot_was_brownout(void) {
  return rp2040_fault_brownout_suspected();
}

void hal_alive_mark(void) { rp2040_fault_alive_mark(); }

hal_status_t hal_stack_guard_init_ex(void) {
  return rp2040_fault_stack_guard_init() ? HAL_OK : HAL_EUNSUPPORTED;
}

bool hal_stack_guard_init(void) {
  return hal_status_to_bool(hal_stack_guard_init_ex());
}

void hal_stack_guard_check(void) { rp2040_fault_stack_guard_check(); }
#endif // HAL_TARGET_IS_RP
