#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "jh_esp32_fault.h"

#include <esp_attr.h>
#include <esp_ipc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <xtensa/corebits.h>
#include <xtensa_api.h>

#include <stddef.h>
#include <stdint.h>

extern "C" void xt_unhandled_exception(XtExcFrame *frame);

namespace {

constexpr uint32_t kFaultMagic = UINT32_C(0x4A484658);
constexpr uint32_t kFaultVersion = 1u;

struct retained_fault_t {
  uint32_t magic;
  uint32_t version;
  uint32_t pc;
  uint32_t lr;
  uint32_t ps;
  uint32_t cause;
  uint32_t address;
  uint32_t checksum;
};

RTC_NOINIT_ATTR volatile retained_fault_t s_retained_fault;
hal_fault_info_t s_last_fault = {};
xt_exc_handler s_previous_handlers[portNUM_PROCESSORS][XCHAL_EXCCAUSE_NUM] = {};
bool s_handlers_installed[portNUM_PROCESSORS] = {};
bool s_initialized;
bool s_retained_latched;
bool s_fault_available;

uint32_t checksum(uint32_t version, uint32_t pc, uint32_t lr, uint32_t ps,
                  uint32_t cause, uint32_t address) {
  return UINT32_C(0xA55A17E3) ^ version ^ pc ^ lr ^ ps ^ cause ^ address;
}

void IRAM_ATTR fault_handler(XtExcFrame *frame) {
  const uint32_t pc = frame != nullptr ? (uint32_t)frame->pc : 0u;
  const uint32_t lr = frame != nullptr ? (uint32_t)frame->a0 : 0u;
  const uint32_t ps = frame != nullptr ? (uint32_t)frame->ps : 0u;
  const uint32_t cause = frame != nullptr ? (uint32_t)frame->exccause : 0u;
  const uint32_t address = frame != nullptr ? (uint32_t)frame->excvaddr : 0u;

  s_retained_fault.magic = 0u;
  s_retained_fault.version = kFaultVersion;
  s_retained_fault.pc = pc;
  s_retained_fault.lr = lr;
  s_retained_fault.ps = ps;
  s_retained_fault.cause = cause;
  s_retained_fault.address = address;
  s_retained_fault.checksum =
      UINT32_C(0xA55A17E3) ^ kFaultVersion ^ pc ^ lr ^ ps ^ cause ^ address;
  __atomic_store_n(&s_retained_fault.magic, kFaultMagic, __ATOMIC_RELEASE);

  const unsigned core = (unsigned)xPortGetCoreID();
  xt_exc_handler previous = nullptr;
  if (core < portNUM_PROCESSORS && cause < XCHAL_EXCCAUSE_NUM) {
    previous = s_previous_handlers[core][cause];
  }
  if (previous != nullptr && previous != fault_handler) {
    previous(frame);
  } else {
    xt_unhandled_exception(frame);
  }
}

void install_handlers_on_current_core(void *) {
  const unsigned core = (unsigned)xPortGetCoreID();
  if (core >= portNUM_PROCESSORS || s_handlers_installed[core]) {
    return;
  }
  static constexpr uint8_t kFatalCauses[] = {
      EXCCAUSE_ILLEGAL,          EXCCAUSE_INSTR_ERROR,
      EXCCAUSE_LOAD_STORE_ERROR, EXCCAUSE_DIVIDE_BY_ZERO,
      EXCCAUSE_PRIVILEGED,       EXCCAUSE_UNALIGNED,
      EXCCAUSE_INSTR_DATA_ERROR, EXCCAUSE_LOAD_STORE_DATA_ERROR,
      EXCCAUSE_INSTR_ADDR_ERROR, EXCCAUSE_LOAD_STORE_ADDR_ERROR,
      EXCCAUSE_ITLB_MULTIHIT,    EXCCAUSE_INSTR_RING,
      EXCCAUSE_INSTR_PROHIBITED, EXCCAUSE_DTLB_MULTIHIT,
      EXCCAUSE_LOAD_STORE_RING,  EXCCAUSE_LOAD_PROHIBITED,
      EXCCAUSE_STORE_PROHIBITED,
  };
  for (uint8_t cause : kFatalCauses) {
    s_previous_handlers[core][cause] =
        xt_set_exception_handler(cause, fault_handler);
  }
  s_handlers_installed[core] = true;
}

void latch_retained_fault(void) {
  const uint32_t magic =
      __atomic_load_n(&s_retained_fault.magic, __ATOMIC_ACQUIRE);
  const uint32_t version = s_retained_fault.version;
  const uint32_t pc = s_retained_fault.pc;
  const uint32_t lr = s_retained_fault.lr;
  const uint32_t ps = s_retained_fault.ps;
  const uint32_t cause = s_retained_fault.cause;
  const uint32_t address = s_retained_fault.address;
  const uint32_t stored_checksum = s_retained_fault.checksum;
  if (magic == kFaultMagic && version == kFaultVersion &&
      stored_checksum == checksum(version, pc, lr, ps, cause, address)) {
    s_last_fault = {};
    s_last_fault.valid = true;
    s_last_fault.pc = pc;
    s_last_fault.lr = lr;
    s_last_fault.psr = ps;
    s_fault_available = true;
  }
  __atomic_store_n(&s_retained_fault.magic, 0u, __ATOMIC_RELEASE);
}

bool all_handlers_installed(void) {
  for (size_t core = 0u; core < portNUM_PROCESSORS; ++core) {
    if (!s_handlers_installed[core]) {
      return false;
    }
  }
  return true;
}

} // namespace

void jh_esp32_fault_init(void) {
  if (s_initialized) {
    return;
  }
  if (!s_retained_latched) {
    latch_retained_fault();
    s_retained_latched = true;
  }
  install_handlers_on_current_core(nullptr);
#if defined(CONFIG_ESP_IPC_ENABLE) && CONFIG_ESP_IPC_ENABLE &&                 \
    portNUM_PROCESSORS > 1
  const uint32_t current_core = (uint32_t)xPortGetCoreID();
  for (uint32_t core = 0u; core < portNUM_PROCESSORS; ++core) {
    if (core == current_core || s_handlers_installed[core]) {
      continue;
    }
    const esp_err_t result =
        esp_ipc_call_blocking(core, install_handlers_on_current_core, nullptr);
    if (result != ESP_OK) {
      return;
    }
  }
#endif
  s_initialized = all_handlers_installed();
}

bool jh_esp32_fault_available(void) { return s_fault_available; }

bool jh_esp32_fault_get(hal_fault_info_t *out) {
  if (out == nullptr || !s_fault_available) {
    return false;
  }
  *out = s_last_fault;
  return true;
}

void jh_esp32_fault_clear(void) {
  s_last_fault = {};
  s_fault_available = false;
  __atomic_store_n(&s_retained_fault.magic, 0u, __ATOMIC_RELEASE);
}

#endif // HAL_TARGET_IS_ESP32_FAMILY
