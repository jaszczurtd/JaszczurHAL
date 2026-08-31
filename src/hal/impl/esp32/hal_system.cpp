#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/network/jh_network_architecture.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "hal/system/hal_system_common.h"
#include "jh_esp32_fault.h"
#include "jh_esp32_status.h"

#if HAL_TARGET_IS_ESP32_S3
#include <driver/temperature_sensor.h>
#endif
#include <esp_clk_tree.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/efuse.h>
#include <sdkconfig.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>

#include <limits.h>
#include <string.h>

extern "C" bool hal_esp32_critical_section_active(void);

namespace {

hal_mutex_t s_watchdog_mutex;
esp_task_wdt_user_handle_t s_watchdog_user;
#if HAL_TARGET_IS_ESP32_S3
hal_mutex_t s_temperature_mutex;
temperature_sensor_handle_t s_temperature_sensor;
#endif

uint32_t size_to_u32(size_t value) {
  return value > (size_t)UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

bool can_block_current_context(void) {
  return !hal_in_isr() && !hal_esp32_critical_section_active() &&
         xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

TickType_t milliseconds_to_ticks(uint32_t milliseconds) {
  TickType_t ticks = pdMS_TO_TICKS(milliseconds);
  if (milliseconds != 0u && ticks == 0u) {
    ticks = 1u;
  }
  return ticks;
}

void busy_wait_ms(uint32_t milliseconds) {
  constexpr uint32_t kMaxChunkMs = UINT32_MAX / 1000u;
  while (milliseconds > kMaxChunkMs) {
    esp_rom_delay_us(kMaxChunkMs * 1000u);
    milliseconds -= kMaxChunkMs;
  }
  esp_rom_delay_us(milliseconds * 1000u);
}

uint32_t watchdog_idle_core_mask(void) {
  uint32_t mask = 0u;
#if defined(CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0) &&                       \
    CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
  mask |= UINT32_C(1) << 0u;
#endif
#if defined(CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1) &&                       \
    CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
  mask |= UINT32_C(1) << 1u;
#endif
  return mask;
}

hal_reset_reason_t reset_reason_from_esp(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON:
    return HAL_RESET_REASON_POWER_ON;
  case ESP_RST_EXT:
    return HAL_RESET_REASON_RUN_PIN;
  case ESP_RST_SW:
  case ESP_RST_DEEPSLEEP:
  case ESP_RST_SDIO:
  case ESP_RST_USB:
    return HAL_RESET_REASON_SOFT;
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT:
    return HAL_RESET_REASON_WATCHDOG;
  case ESP_RST_JTAG:
    return HAL_RESET_REASON_DEBUG;
  case ESP_RST_PWR_GLITCH:
    return HAL_RESET_REASON_GLITCH;
  case ESP_RST_BROWNOUT:
    return HAL_RESET_REASON_BROWNOUT;
  case ESP_RST_PANIC:
  case ESP_RST_CPU_LOCKUP:
    return HAL_RESET_REASON_HARDFAULT;
  case ESP_RST_EFUSE:
  case ESP_RST_UNKNOWN:
  default:
    return HAL_RESET_REASON_UNKNOWN;
  }
}

hal_status_t temperature_sensor_get(float *out_celsius) {
#if HAL_TARGET_IS_ESP32_S3
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_temperature_mutex);
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }

  hal_mutex_lock(mutex);
  if (s_temperature_sensor == nullptr) {
    const temperature_sensor_config_t config =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t result =
        temperature_sensor_install(&config, &s_temperature_sensor);
    if (result != ESP_OK) {
      s_temperature_sensor = nullptr;
      hal_mutex_unlock(mutex);
      return jh_esp32_status_from_esp_err(result);
    }
    result = temperature_sensor_enable(s_temperature_sensor);
    if (result != ESP_OK) {
      (void)temperature_sensor_uninstall(s_temperature_sensor);
      s_temperature_sensor = nullptr;
      hal_mutex_unlock(mutex);
      return jh_esp32_status_from_esp_err(result);
    }
  }

  const esp_err_t result =
      temperature_sensor_get_celsius(s_temperature_sensor, out_celsius);
  hal_mutex_unlock(mutex);
  return jh_esp32_status_from_esp_err(result);
#else
  (void)out_celsius;
  return HAL_EUNSUPPORTED;
#endif
}

} // namespace

uint32_t hal_millis(void) {
  return (uint32_t)((uint64_t)esp_timer_get_time() / UINT64_C(1000));
}

uint32_t hal_micros(void) { return (uint32_t)esp_timer_get_time(); }

uint64_t hal_micros64(void) { return (uint64_t)esp_timer_get_time(); }

void hal_delay_ms(uint32_t ms) {
  if (can_block_current_context()) {
    if (ms == 0u) {
      taskYIELD();
    } else {
      vTaskDelay(milliseconds_to_ticks(ms));
    }
    return;
  }
  busy_wait_ms(ms);
}

void hal_delay_us(uint32_t us) { esp_rom_delay_us(us); }

void hal_watchdog_feed(void) {
  esp_task_wdt_user_handle_t user =
      __atomic_load_n(&s_watchdog_user, __ATOMIC_ACQUIRE);
  if (user != nullptr) {
    const esp_err_t result = esp_task_wdt_reset_user(user);
    HAL_ASSERT(result == ESP_OK, "hal_watchdog_feed: ESP-IDF reset failed");
    (void)result;
  }
}

hal_status_t hal_watchdog_enable(uint32_t ms, bool pause_on_debug) {
  if (ms == 0u || hal_in_isr()) {
    return ms == 0u ? HAL_EINVAL : HAL_ESTATE;
  }

  /* OpenOCD controls ESP32 watchdog behavior while a core is halted. There is
   * no per-TWDT runtime equivalent of pause_on_debug in ESP-IDF. */
  (void)pause_on_debug;

  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_watchdog_mutex);
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);

  const esp_task_wdt_config_t config = {
      .timeout_ms = ms,
      .idle_core_mask = watchdog_idle_core_mask(),
      .trigger_panic = true,
  };

  esp_err_t result = esp_task_wdt_status(nullptr);
  if (result == ESP_ERR_INVALID_STATE) {
    result = esp_task_wdt_init(&config);
  } else if (result == ESP_OK || result == ESP_ERR_NOT_FOUND) {
    result = esp_task_wdt_reconfigure(&config);
  }
  if (result != ESP_OK) {
    hal_mutex_unlock(mutex);
    return jh_esp32_status_from_esp_err(result);
  }

  if (s_watchdog_user == nullptr) {
    esp_task_wdt_user_handle_t user = nullptr;
    result = esp_task_wdt_add_user("JaszczurHAL", &user);
    if (result != ESP_OK) {
      hal_mutex_unlock(mutex);
      return jh_esp32_status_from_esp_err(result);
    }
    __atomic_store_n(&s_watchdog_user, user, __ATOMIC_RELEASE);
  }

  hal_mutex_unlock(mutex);
  return HAL_OK;
}

bool hal_watchdog_caused_reboot(void) {
  return reset_reason_from_esp(esp_reset_reason()) == HAL_RESET_REASON_WATCHDOG;
}

hal_status_t
hal_system_get_current_architecture(hal_system_architecture_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }

  uint32_t cpu_clock_hz = 0u;
  if (esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU,
                                   ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                   &cpu_clock_hz) != ESP_OK) {
    cpu_clock_hz =
        (uint32_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * UINT32_C(1000000);
  }
  uint32_t peripheral_clock_hz = 0u;
  if (esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_APB,
                                   ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                   &peripheral_clock_hz) != ESP_OK) {
    peripheral_clock_hz = APB_CLK_FREQ;
  }

  const uint32_t flash_total = (uint32_t)HAL_BOARD_EXPECTED_FLASH_BYTES;
  const esp_partition_t *running = esp_ota_get_running_partition();
  const uint32_t flash_usable =
      running != nullptr ? size_to_u32(running->size) : 0u;
  const uint32_t flash_reserved =
      flash_usable <= flash_total ? flash_total - flash_usable : 0u;
  const uint32_t external_ram =
#if HAL_BOARD_HAS_PSRAM
      (uint32_t)HAL_BOARD_PSRAM_BYTES;
#else
      0u;
#endif

  hal_system_architecture_t info = {};
  info.target_name = HAL_TARGET_DESCRIPTOR_ID;
  info.backend_name = HAL_TARGET_BACKEND_NAME;
  info.mcu = HAL_TARGET_MCU_NAME;
  info.mcu_subtype = HAL_TARGET_MCU_SUBTYPE_NAME;
  info.cpu_arch = HAL_TARGET_CPU_ARCH_NAME;
  info.rtos_name = "ESP-IDF FreeRTOS";
  info.cpu_cores = HAL_TARGET_CPU_CORES;
  info.is_hardware = true;
  info.has_fpu = HAL_TARGET_HAS_FPU != 0;
  info.has_rtos = true;
  info.cpu_clock_hz = cpu_clock_hz;
  info.peripheral_clock_hz = peripheral_clock_hz;
  info.flash_total_bytes = flash_total;
  info.flash_usable_bytes = flash_usable;
  info.flash_reserved_bytes = flash_reserved;
  info.ram_total_bytes = (uint32_t)HAL_TARGET_RAM_TOTAL_BYTES + external_ram;
  info.ram_usable_bytes = (uint32_t)HAL_TARGET_RAM_USABLE_BYTES + external_ram;
  info.heap_total_bytes =
      size_to_u32(heap_caps_get_total_size(MALLOC_CAP_8BIT));
  info.heap_free_bytes = hal_get_free_heap();
  info.stack_total_bytes = (uint32_t)CONFIG_ESP_MAIN_TASK_STACK_SIZE;
  info.uid_bytes = HAL_DEVICE_UID_BYTES;
  jh_network_architecture_fill(&info);
  *out = info;
  return HAL_OK;
}

void hal_idle(void) {
  if (!hal_in_isr()) {
    taskYIELD();
  }
}

bool hal_in_isr(void) { return xPortInIsrContext() != pdFALSE; }

uint32_t hal_get_free_heap(void) { return esp_get_free_heap_size(); }

hal_status_t hal_read_chip_temp_ex(float *out_celsius) {
  if (out_celsius == nullptr) {
    return HAL_EINVAL;
  }
  if (hal_in_isr()) {
    return HAL_ESTATE;
  }
  return temperature_sensor_get(out_celsius);
}

float hal_read_chip_temp(void) {
  float celsius = 0.0f;
  (void)hal_read_chip_temp_ex(&celsius);
  return celsius;
}

hal_status_t hal_enter_bootloader(void) {
#if HAL_TARGET_IS_ESP32_S3
  if (ets_efuse_download_modes_disabled()) {
    return HAL_EUNSUPPORTED;
  }
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
  return HAL_OK;
#else
  return HAL_EUNSUPPORTED;
#endif
}

hal_status_t hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]) {
  if (uid == nullptr) {
    return HAL_EINVAL;
  }

  uint8_t mac[6] = {};
  const esp_err_t result = esp_efuse_mac_get_default(mac);
  if (result != ESP_OK) {
    return jh_esp32_status_from_esp_err(result);
  }
  uid[0] = 0u;
  uid[1] = 0u;
  memcpy(uid + 2u, mac, sizeof(mac));
  return HAL_OK;
}

hal_status_t hal_get_device_uid_hex_ex(char *buf, size_t buflen) {
  if (buf == nullptr) {
    return HAL_EINVAL;
  }
  if (buflen < HAL_DEVICE_UID_HEX_BUF_SIZE) {
    return HAL_EOVERFLOW;
  }

  uint8_t uid[HAL_DEVICE_UID_BYTES] = {};
  const hal_status_t status = hal_get_device_uid(uid);
  if (status != HAL_OK) {
    return status;
  }
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t index = 0u; index < HAL_DEVICE_UID_BYTES; ++index) {
    buf[index * 2u] = kHex[(uid[index] >> 4u) & 0x0Fu];
    buf[index * 2u + 1u] = kHex[uid[index] & 0x0Fu];
  }
  buf[HAL_DEVICE_UID_BYTES * 2u] = '\0';
  return HAL_OK;
}

bool hal_get_device_uid_hex(char *buf, size_t buflen) {
  return hal_status_to_bool(hal_get_device_uid_hex_ex(buf, buflen));
}

void hal_fault_subsystem_init(void) { jh_esp32_fault_init(); }

hal_reset_reason_t hal_get_reset_reason(void) {
  if (jh_esp32_fault_available()) {
    return HAL_RESET_REASON_HARDFAULT;
  }
  return reset_reason_from_esp(esp_reset_reason());
}

const char *hal_reset_reason_str(hal_reset_reason_t reason) {
  return jh_hal_reset_reason_str(reason);
}

hal_status_t hal_get_last_fault_ex(hal_fault_info_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }
  return jh_esp32_fault_get(out) ? HAL_OK : HAL_ENOENT;
}

bool hal_get_last_fault(hal_fault_info_t *out) {
  return hal_status_to_bool(hal_get_last_fault_ex(out));
}

void hal_clear_last_fault(void) { jh_esp32_fault_clear(); }

bool hal_last_boot_was_brownout(void) {
  return esp_reset_reason() == ESP_RST_BROWNOUT;
}

void hal_alive_mark(void) {}

hal_status_t hal_stack_guard_init_ex(void) {
#if defined(HAL_ENABLE_STACK_GUARD) &&                                         \
    defined(CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY) &&                     \
    CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY &&                              \
    defined(CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK) &&                        \
    CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK
  return HAL_OK;
#else
  return HAL_EUNSUPPORTED;
#endif
}

bool hal_stack_guard_init(void) {
  return hal_status_to_bool(hal_stack_guard_init_ex());
}

void hal_stack_guard_check(void) {}

#endif // HAL_TARGET_IS_ESP32_FAMILY
