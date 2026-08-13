#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"
#include "hal/network/jh_network_architecture.h"
#include "hal/system/hal_system.h"
#include "hal/system/hal_system_common.h"
#include "hal_mock.h"

#include <string.h>

static uint32_t s_millis = 0;
static uint32_t s_micros = 0;
static bool s_watchdog_fed = false;
static bool s_caused_reboot = false;
static uint32_t s_free_heap = 256 * 1024;
static bool s_bootloader_requested = false;

/* Default deterministic mock UID so tests can rely on a stable value. */
static uint8_t s_device_uid[HAL_DEVICE_UID_BYTES] = {0xE6, 0x61, 0xA4, 0xD1,
                                                     0x23, 0x45, 0x67, 0xAB};

uint32_t hal_millis(void) { return s_millis; }

uint32_t hal_micros(void) { return s_micros; }

uint64_t hal_micros64(void) { return (uint64_t)s_micros; }

void hal_delay_ms(uint32_t ms) {
  s_millis += ms;
  s_micros += ms * 1000;
}

void hal_delay_us(uint32_t us) { s_micros += us; }

void hal_watchdog_feed(void) { s_watchdog_fed = true; }

hal_status_t hal_watchdog_enable(uint32_t ms, bool pause_on_debug) {
  (void)ms;
  (void)pause_on_debug;
  return HAL_OK;
}

bool hal_watchdog_caused_reboot(void) { return s_caused_reboot; }

hal_status_t
hal_system_get_current_architecture(hal_system_architecture_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }

  hal_system_architecture_t info = {};
  info.target_name = HAL_TARGET_DESCRIPTOR_ID;
  info.backend_name = HAL_TARGET_BACKEND_NAME;
  info.mcu = HAL_TARGET_MCU_NAME;
  info.mcu_subtype = HAL_TARGET_MCU_SUBTYPE_NAME;
  info.cpu_arch = HAL_TARGET_CPU_ARCH_NAME;
  info.rtos_name = "none";
  info.cpu_cores = HAL_TARGET_CPU_CORES;
  info.is_hardware = false;
  info.has_fpu = HAL_TARGET_HAS_FPU != 0;
  info.has_rtos = false;
  info.flash_total_bytes = HAL_BOARD_EXPECTED_FLASH_BYTES;
  info.ram_total_bytes = HAL_TARGET_RAM_TOTAL_BYTES;
  info.ram_usable_bytes = HAL_TARGET_RAM_USABLE_BYTES;
  info.heap_free_bytes = hal_get_free_heap();
  info.uid_bytes = HAL_DEVICE_UID_BYTES;
  jh_network_architecture_fill(&info);
  *out = info;
  return HAL_OK;
}

void hal_idle(void) {
  // no-op
}

static bool s_in_isr = false;

bool hal_in_isr(void) { return s_in_isr; }

void hal_mock_set_in_isr(bool in_isr) { s_in_isr = in_isr; }

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

void hal_mock_set_millis(uint32_t ms) {
  s_millis = ms;
  s_micros = ms * 1000;
}

void hal_mock_advance_millis(uint32_t ms) {
  s_millis += ms;
  s_micros += ms * 1000;
}

void hal_mock_set_micros(uint32_t us) {
  s_micros = us;
  s_millis = us / 1000;
}

void hal_mock_advance_micros(uint32_t us) {
  s_micros += us;
  s_millis = s_micros / 1000;
}

bool hal_mock_watchdog_was_fed(void) { return s_watchdog_fed; }

void hal_mock_watchdog_reset_flag(void) { s_watchdog_fed = false; }

void hal_mock_set_caused_reboot(bool val) { s_caused_reboot = val; }

uint32_t hal_get_free_heap(void) { return s_free_heap; }

void hal_mock_set_free_heap(uint32_t bytes) { s_free_heap = bytes; }

static float s_chip_temp = 25.0f;

hal_status_t hal_read_chip_temp_ex(float *out_celsius) {
  if (out_celsius == nullptr) {
    return HAL_EINVAL;
  }
  *out_celsius = s_chip_temp;
  return HAL_OK;
}

float hal_read_chip_temp(void) {
  float celsius = 0.0f;
  (void)hal_read_chip_temp_ex(&celsius);
  return celsius;
}

void hal_mock_set_chip_temp(float celsius) { s_chip_temp = celsius; }

hal_status_t hal_enter_bootloader(void) {
  s_bootloader_requested = true;
  return HAL_OK;
}

bool hal_mock_bootloader_was_requested(void) { return s_bootloader_requested; }

void hal_mock_bootloader_reset_flag(void) { s_bootloader_requested = false; }

hal_status_t hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]) {
  if (uid == nullptr) {
    return HAL_EINVAL;
  }
  memcpy(uid, s_device_uid, HAL_DEVICE_UID_BYTES);
  return HAL_OK;
}

hal_status_t hal_get_device_uid_hex_ex(char *buf, size_t buflen) {
  if (buf == nullptr) {
    return HAL_EINVAL;
  }
  if (buflen < HAL_DEVICE_UID_HEX_BUF_SIZE) {
    return HAL_EOVERFLOW;
  }
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < HAL_DEVICE_UID_BYTES; ++i) {
    buf[(i * 2u) + 0u] = kHex[(s_device_uid[i] >> 4) & 0x0Fu];
    buf[(i * 2u) + 1u] = kHex[s_device_uid[i] & 0x0Fu];
  }
  buf[HAL_DEVICE_UID_BYTES * 2u] = '\0';
  return HAL_OK;
}

bool hal_get_device_uid_hex(char *buf, size_t buflen) {
  return hal_status_to_bool(hal_get_device_uid_hex_ex(buf, buflen));
}

void hal_mock_set_device_uid(const uint8_t uid[HAL_DEVICE_UID_BYTES]) {
  if (uid == nullptr) {
    return;
  }
  memcpy(s_device_uid, uid, HAL_DEVICE_UID_BYTES);
}

void hal_mock_reset_device_uid(void) {
  static const uint8_t kDefault[HAL_DEVICE_UID_BYTES] = {
      0xE6, 0x61, 0xA4, 0xD1, 0x23, 0x45, 0x67, 0xAB};
  memcpy(s_device_uid, kDefault, HAL_DEVICE_UID_BYTES);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fault / crash diagnostics -- mock backend
// ─────────────────────────────────────────────────────────────────────────────

static hal_reset_reason_t s_reset_reason = HAL_RESET_REASON_UNKNOWN;
static hal_fault_info_t s_fault_info = {};
static bool s_brownout_suspected = false;
static bool s_alive_marked = false;
static bool s_subsystem_init = false;
static bool s_stack_guard_armed = false;

void hal_fault_subsystem_init(void) {
  s_subsystem_init = true;
  // Test fixtures stage state via hal_mock_set_reset_reason() /
  // hal_mock_set_last_fault() / hal_mock_set_brownout_suspected()
  // before calling this function; the mock impl preserves whatever
  // the test set and does not auto-reset it.
}

hal_reset_reason_t hal_get_reset_reason(void) { return s_reset_reason; }

const char *hal_reset_reason_str(hal_reset_reason_t reason) {
  return jh_hal_reset_reason_str(reason);
}

hal_status_t hal_get_last_fault_ex(hal_fault_info_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_fault_info.valid) {
    return HAL_ENOENT;
  }
  *out = s_fault_info;
  return HAL_OK;
}

bool hal_get_last_fault(hal_fault_info_t *out) {
  return hal_status_to_bool(hal_get_last_fault_ex(out));
}

void hal_clear_last_fault(void) {
  s_fault_info.valid = false;
  s_fault_info.pc = 0;
  s_fault_info.lr = 0;
  s_fault_info.psr = 0;
  s_fault_info.cfsr = 0u;
  s_fault_info.hfsr = 0u;
  s_fault_info.mmfar = 0u;
  s_fault_info.bfar = 0u;
}

bool hal_last_boot_was_brownout(void) { return s_brownout_suspected; }

void hal_alive_mark(void) { s_alive_marked = true; }

hal_status_t hal_stack_guard_init_ex(void) {
#ifdef HAL_ENABLE_STACK_GUARD
  s_stack_guard_armed = true;
  return HAL_OK;
#else
  s_stack_guard_armed = false;
  return HAL_EUNSUPPORTED;
#endif
}

bool hal_stack_guard_init(void) {
  return hal_status_to_bool(hal_stack_guard_init_ex());
}

void hal_stack_guard_check(void) {}

// ── Mock-only test hooks ────────────────────────────────────────────────────

void hal_mock_set_reset_reason(hal_reset_reason_t reason) {
  s_reset_reason = reason;
}

void hal_mock_set_last_fault(const hal_fault_info_t *info) {
  if (info == nullptr) {
    s_fault_info.valid = false;
    s_fault_info.pc = 0;
    s_fault_info.lr = 0;
    s_fault_info.psr = 0;
    s_fault_info.cfsr = 0u;
    s_fault_info.hfsr = 0u;
    s_fault_info.mmfar = 0u;
    s_fault_info.bfar = 0u;
    return;
  }
  s_fault_info = *info;
}

void hal_mock_set_brownout_suspected(bool val) { s_brownout_suspected = val; }

bool hal_mock_alive_was_marked(void) { return s_alive_marked; }

void hal_mock_alive_reset_flag(void) { s_alive_marked = false; }

bool hal_mock_fault_subsystem_was_inited(void) { return s_subsystem_init; }

bool hal_mock_stack_guard_is_armed(void) { return s_stack_guard_armed; }

void hal_mock_fault_diagnostics_reset(void) {
  s_reset_reason = HAL_RESET_REASON_UNKNOWN;
  s_fault_info = {};
  s_brownout_suspected = false;
  s_alive_marked = false;
  s_subsystem_init = false;
  s_stack_guard_armed = false;
}
#endif // HAL_TARGET_IS_MOCK
