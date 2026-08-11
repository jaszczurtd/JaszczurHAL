#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/storage/flash/jh_flash_transaction_engine.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "rp_flash_runtime.h"
#include "rp_flash_transaction.h"

#include <hardware/dma.h>
#include <hardware/regs/addressmap.h>
#include <hardware/structs/dma.h>
#include <hardware/sync.h>
#include <pico/error.h>
#include <pico/flash.h>
#include <pico/multicore.h>
#include <pico/platform.h>
#include <pico/time.h>

#include <stddef.h>
#include <stdint.h>

#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif

namespace {

constexpr uintptr_t kNoOwner = UINTPTR_MAX;

struct RpFlashBackend {
  bool usb_mutex_held;
};

hal_mutex_t s_transaction_mutex;
volatile uintptr_t s_owner = kNoOwner;
volatile bool s_active;

extern "C" bool hal_rp2040_critical_section_active(void);

uintptr_t current_owner_token() {
#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    return reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  }
#endif
  return (uintptr_t)get_core_num() + 1u;
}

bool address_is_xip(const void *address) {
  const uintptr_t value = reinterpret_cast<uintptr_t>(address);
#if defined(PICO_RP2350)
  return value >= (uintptr_t)XIP_BASE &&
         value < (uintptr_t)XIP_NOCACHE_NOALLOC_NOTRANSLATE_END;
#else
  return (value >= (uintptr_t)XIP_BASE && value < (uintptr_t)XIP_CTRL_BASE) ||
         (value >= (uintptr_t)XIP_SRAM_BASE && value < (uintptr_t)XIP_SRAM_END);
#endif
}

hal_status_t pico_status_to_hal(int status) {
  switch (status) {
  case PICO_OK:
    return HAL_OK;
  case PICO_ERROR_TIMEOUT:
    return HAL_ETIMEOUT;
  case PICO_ERROR_INSUFFICIENT_RESOURCES:
    return HAL_ENOMEM;
  case PICO_ERROR_NOT_PERMITTED:
    return HAL_EPERM;
  default:
    return HAL_EHW;
  }
}

hal_status_t acquire_transaction(void *, uint32_t timeout_ms) {
  if (hal_in_isr() || hal_rp2040_critical_section_active()) {
    return HAL_ESTATE;
  }

  const uintptr_t owner = current_owner_token();
  if (__atomic_load_n(&s_owner, __ATOMIC_ACQUIRE) == owner) {
    return HAL_ESTATE;
  }

  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_transaction_mutex);
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }

  const uint64_t start_us = time_us_64();
  const uint64_t timeout_us = (uint64_t)timeout_ms * 1000u;
  while (!hal_mutex_try_lock(mutex)) {
    if (timeout_ms == 0u) {
      return HAL_EBUSY;
    }
    if (time_us_64() - start_us >= timeout_us) {
      return HAL_ETIMEOUT;
    }
    hal_idle();
  }

  if (__atomic_load_n(&s_active, __ATOMIC_ACQUIRE)) {
    hal_mutex_unlock(mutex);
    return HAL_ESTATE;
  }

  __atomic_store_n(&s_owner, owner, __ATOMIC_RELEASE);
  __atomic_store_n(&s_active, true, __ATOMIC_RELEASE);
  return HAL_OK;
}

hal_status_t quiesce_runtime(void *backend_context, uint32_t timeout_ms) {
  auto *backend = static_cast<RpFlashBackend *>(backend_context);
  return jh_rp_usb_flash_quiesce(timeout_ms, &backend->usb_mutex_held);
}

struct SafeExecuteContext {
  jh_flash_transaction_operation_t operation;
  void *operation_context;
  hal_status_t status;
};

void __no_inline_not_in_flash_func(execute_in_safe_zone)(void *raw_context) {
  auto *context = static_cast<SafeExecuteContext *>(raw_context);
  __compiler_memory_barrier();

  for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
    if ((dma_hw->ch[channel].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) != 0u) {
      context->status = HAL_EBUSY;
      return;
    }
  }

  context->status = context->operation(context->operation_context);
  __compiler_memory_barrier();
}

hal_status_t safe_execute(void *, jh_flash_transaction_operation_t operation,
                          void *operation_context, uint32_t timeout_ms) {
  SafeExecuteContext context = {operation, operation_context, HAL_EINTERNAL};
#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
  const BaseType_t scheduler_state = xTaskGetSchedulerState();
  if (scheduler_state == taskSCHEDULER_NOT_STARTED) {
    if (get_core_num() != 0u) {
      return HAL_ESTATE;
    }
    const uint32_t irq_state = save_and_disable_interrupts();
    execute_in_safe_zone(&context);
    restore_interrupts(irq_state);
    return context.status;
  }
  if (scheduler_state != taskSCHEDULER_RUNNING) {
    return HAL_ESTATE;
  }
#endif
  const int pico_status =
      flash_safe_execute(execute_in_safe_zone, &context, timeout_ms);
  if (pico_status != PICO_OK) {
    return pico_status_to_hal(pico_status);
  }
  return context.status;
}

hal_status_t resume_runtime(void *backend_context) {
  auto *backend = static_cast<RpFlashBackend *>(backend_context);
  const hal_status_t status = jh_rp_usb_flash_resume(backend->usb_mutex_held);
  backend->usb_mutex_held = false;
  return status;
}

hal_status_t release_transaction(void *) {
  hal_mutex_t mutex = __atomic_load_n(&s_transaction_mutex, __ATOMIC_ACQUIRE);
  if (mutex == nullptr || !__atomic_load_n(&s_active, __ATOMIC_ACQUIRE)) {
    return HAL_ESTATE;
  }

  __atomic_store_n(&s_active, false, __ATOMIC_RELEASE);
  __atomic_store_n(&s_owner, kNoOwner, __ATOMIC_RELEASE);
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

const jh_flash_transaction_backend_t kBackend = {
    acquire_transaction, quiesce_runtime, safe_execute, resume_runtime,
    release_transaction};

} // namespace

hal_status_t jh_rp_flash_transaction_core_init(void) {
  if (hal_in_isr() || hal_rp2040_critical_section_active()) {
    return HAL_ESTATE;
  }
  return hal_status_from_bool(flash_safe_execute_core_init(), HAL_EHW);
}

hal_status_t __no_inline_not_in_flash_func(jh_rp_flash_transaction_execute)(
    jh_rp_flash_operation_t operation, void *context, uint32_t timeout_ms) {
  if (__atomic_load_n(&s_active, __ATOMIC_ACQUIRE) &&
      __atomic_load_n(&s_owner, __ATOMIC_ACQUIRE) == current_owner_token()) {
    return HAL_ESTATE;
  }
  if (operation == nullptr || address_is_xip((const void *)operation) ||
      (context != nullptr && address_is_xip(context))) {
    return HAL_EINVAL;
  }

  RpFlashBackend backend = {};
  return jh_flash_transaction_engine_execute(&kBackend, &backend, operation,
                                             context, timeout_ms);
}

#endif
