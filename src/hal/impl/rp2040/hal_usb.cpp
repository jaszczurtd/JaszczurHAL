#include "../../hal_target.h"
#if HAL_TARGET_IS_RP

#include "../../hal_board.h"
#include "../../hal_config.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include "../../hal_usb.h"
#include "../shared/hal_mutex_once.h"
#include "../shared/jh_board_runtime.h"
#include "drivers/flash/rp_flash_runtime.h"

#include <pico/bootrom.h>
#include <pico/time.h>
#include <tusb.h>

#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#else
#include <hardware/irq.h>
#endif
#include <pico/multicore.h>

namespace {

volatile bool s_initialized;
hal_usb_bootloader_reset_hook_t s_reset_hook;
void *s_reset_hook_user;

#ifndef HAL_USB_TASK_INTERVAL_US
#define HAL_USB_TASK_INTERVAL_US 1000
#endif

#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
#define JH_RP_USB_FREERTOS 1
#else
#define JH_RP_USB_FREERTOS 0
#endif

#ifndef HAL_USB_FREERTOS_TASK_STACK
#define HAL_USB_FREERTOS_TASK_STACK 512u
#endif

#ifndef HAL_USB_FREERTOS_TASK_PRIORITY
#define HAL_USB_FREERTOS_TASK_PRIORITY (tskIDLE_PRIORITY + 2u)
#endif

hal_mutex_t s_usb_mutex;
#if JH_RP_USB_FREERTOS
TaskHandle_t s_worker_task;
volatile bool s_worker_stop;
#else
repeating_timer_t s_task_timer;
bool s_task_timer_active;
int s_worker_irq = -1;
#endif
volatile bool s_touch_armed;
volatile bool s_flash_paused;
volatile uintptr_t s_flash_pause_owner = UINTPTR_MAX;

hal_mutex_t usb_mutex(void) { return jh_hal_mutex_create_once(&s_usb_mutex); }

uintptr_t flash_owner_token() {
#if JH_RP_USB_FREERTOS
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    return reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  }
#endif
  return (uintptr_t)get_core_num() + 1u;
}

class UsbLock {
public:
  UsbLock() : mutex_(usb_mutex()) {
    if (mutex_ != nullptr) {
      hal_mutex_lock(mutex_);
    }
  }

  ~UsbLock() {
    if (mutex_ != nullptr) {
      hal_mutex_unlock(mutex_);
    }
  }

  bool ok() const { return mutex_ != nullptr; }

private:
  hal_mutex_t mutex_;
};

#if !JH_RP_USB_FREERTOS
void usb_worker_irq(void) {
  hal_mutex_t mutex = usb_mutex();
  if (mutex == nullptr || !hal_mutex_try_lock(mutex)) {
    return;
  }
  if (__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
    tud_task();
  }
  hal_mutex_unlock(mutex);
}

bool usb_task_timer(repeating_timer_t *) {
  const int worker_irq = __atomic_load_n(&s_worker_irq, __ATOMIC_ACQUIRE);
  if (__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE) && worker_irq >= 0) {
    irq_set_pending((uint)worker_irq);
    return true;
  }
  return false;
}
#endif

#define HAL_USB_LOCK_GUARD(name) UsbLock name
#define HAL_USB_LOCK_OK(name) ((name).ok())

bool usb_stack_inited(void) { return tud_inited(); }

void usb_task_locked(void) {
  if (usb_stack_inited()) {
    tud_task();
  }
}

#if JH_RP_USB_FREERTOS
void usb_worker_task(void *) {
  TickType_t delay_ticks =
      pdMS_TO_TICKS((HAL_USB_TASK_INTERVAL_US + 999u) / 1000u);
  if (delay_ticks == 0u) {
    delay_ticks = 1u;
  }

  while (!__atomic_load_n(&s_worker_stop, __ATOMIC_ACQUIRE)) {
    hal_mutex_t mutex = usb_mutex();
    if (mutex != nullptr) {
      hal_mutex_lock(mutex);
      if (__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        usb_task_locked();
      }
      hal_mutex_unlock(mutex);
    }
    vTaskDelay(delay_ticks);
  }

  __atomic_store_n(&s_worker_task, nullptr, __ATOMIC_RELEASE);
  vTaskDelete(nullptr);
}
#endif

bool usb_connected_locked(void) {
  return usb_stack_inited() && tud_cdc_connected();
}

void notify_reset_hook(void) {
  hal_usb_bootloader_reset_hook_t hook =
      __atomic_load_n(&s_reset_hook, __ATOMIC_ACQUIRE);
  if (hook != nullptr) {
    hook(s_reset_hook_user);
  }
}

} // namespace

hal_status_t hal_usb_init(void) {
  if (__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
    return HAL_OK;
  }

  if (get_core_num() != 0u) {
    return HAL_ESTATE;
  }

  HAL_USB_LOCK_GUARD(lock);
  if (!HAL_USB_LOCK_OK(lock)) {
    (void)jh_board_runtime_set_failed(HAL_BOARD_CAP_USB_DEVICE);
    return HAL_ENOMEM;
  }
  if (__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
    return HAL_OK;
  }

  if (!tusb_init()) {
    (void)jh_board_runtime_set_failed(HAL_BOARD_CAP_USB_DEVICE);
    return HAL_EHW;
  }

#if JH_RP_USB_FREERTOS
  __atomic_store_n(&s_worker_stop, false, __ATOMIC_RELEASE);
  __atomic_store_n(&s_initialized, true, __ATOMIC_RELEASE);
  TaskHandle_t worker = nullptr;
#if configNUMBER_OF_CORES > 1
  const BaseType_t created = xTaskCreateAffinitySet(
      usb_worker_task, "jh_usb",
      (configSTACK_DEPTH_TYPE)HAL_USB_FREERTOS_TASK_STACK, nullptr,
      (UBaseType_t)HAL_USB_FREERTOS_TASK_PRIORITY, 1u << 0u, &worker);
#else
  const BaseType_t created =
      xTaskCreate(usb_worker_task, "jh_usb",
                  (configSTACK_DEPTH_TYPE)HAL_USB_FREERTOS_TASK_STACK, nullptr,
                  (UBaseType_t)HAL_USB_FREERTOS_TASK_PRIORITY, &worker);
#endif
  if (created != pdPASS) {
    __atomic_store_n(&s_initialized, false, __ATOMIC_RELEASE);
    (void)tud_disconnect();
    (void)jh_board_runtime_set_failed(HAL_BOARD_CAP_USB_DEVICE);
    return HAL_ENOMEM;
  }
  __atomic_store_n(&s_worker_task, worker, __ATOMIC_RELEASE);
#else
  s_worker_irq = user_irq_claim_unused(false);
  if (s_worker_irq < 0) {
    (void)tud_disconnect();
    (void)jh_board_runtime_set_failed(HAL_BOARD_CAP_USB_DEVICE);
    return HAL_EBUSY;
  }
  irq_set_exclusive_handler((uint)s_worker_irq, usb_worker_irq);
  irq_set_enabled((uint)s_worker_irq, true);

  __atomic_store_n(&s_initialized, true, __ATOMIC_RELEASE);
  s_task_timer_active = add_repeating_timer_us(
      HAL_USB_TASK_INTERVAL_US, usb_task_timer, nullptr, &s_task_timer);
  if (!s_task_timer_active) {
    __atomic_store_n(&s_initialized, false, __ATOMIC_RELEASE);
    irq_set_enabled((uint)s_worker_irq, false);
    irq_remove_handler((uint)s_worker_irq, usb_worker_irq);
    user_irq_unclaim((uint)s_worker_irq);
    s_worker_irq = -1;
    (void)tud_disconnect();
    (void)jh_board_runtime_set_failed(HAL_BOARD_CAP_USB_DEVICE);
    return HAL_EHW;
  }
#endif

  usb_task_locked();
  (void)jh_board_runtime_set_available(HAL_BOARD_CAP_USB_DEVICE);
  return HAL_OK;
}

hal_status_t hal_usb_deinit(void) {
  if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
    return HAL_EUNINIT;
  }
  if (get_core_num() != 0u) {
    return HAL_ESTATE;
  }

#if JH_RP_USB_FREERTOS
  TaskHandle_t worker = __atomic_load_n(&s_worker_task, __ATOMIC_ACQUIRE);
  if (worker != nullptr && xTaskGetCurrentTaskHandle() == worker) {
    return HAL_ESTATE;
  }
  __atomic_store_n(&s_worker_stop, true, __ATOMIC_RELEASE);
  __atomic_store_n(&s_initialized, false, __ATOMIC_RELEASE);
  if (worker != nullptr) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
      while (__atomic_load_n(&s_worker_task, __ATOMIC_ACQUIRE) != nullptr) {
        vTaskDelay(1u);
      }
    } else {
      vTaskDelete(worker);
      __atomic_store_n(&s_worker_task, nullptr, __ATOMIC_RELEASE);
    }
  }
#else
  __atomic_store_n(&s_initialized, false, __ATOMIC_RELEASE);
  if (s_worker_irq >= 0) {
    irq_set_enabled((uint)s_worker_irq, false);
  }
  if (s_task_timer_active) {
    (void)cancel_repeating_timer(&s_task_timer);
    s_task_timer_active = false;
  }
#endif

  HAL_USB_LOCK_GUARD(lock);
  if (!HAL_USB_LOCK_OK(lock)) {
    return HAL_ENOMEM;
  }
  (void)tud_disconnect();

#if !JH_RP_USB_FREERTOS
  if (s_worker_irq >= 0) {
    irq_remove_handler((uint)s_worker_irq, usb_worker_irq);
    user_irq_unclaim((uint)s_worker_irq);
    s_worker_irq = -1;
  }
#endif
  (void)jh_board_runtime_set_inactive(HAL_BOARD_CAP_USB_DEVICE);
  return HAL_OK;
}

hal_status_t hal_usb_task(void) {
  if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE) ||
      !usb_stack_inited()) {
    return HAL_EUNINIT;
  }
  HAL_USB_LOCK_GUARD(lock);
  if (!HAL_USB_LOCK_OK(lock)) {
    return HAL_EBUSY;
  }
  usb_task_locked();
  return HAL_OK;
}

hal_status_t hal_usb_cdc_is_connected(bool *out_connected) {
  if (out_connected == nullptr) {
    return HAL_EINVAL;
  }
  *out_connected = false;
  if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE) ||
      !usb_stack_inited()) {
    return HAL_EUNINIT;
  }

  HAL_USB_LOCK_GUARD(lock);
  if (!HAL_USB_LOCK_OK(lock)) {
    return HAL_EBUSY;
  }
  usb_task_locked();
  *out_connected = usb_connected_locked();
  return HAL_OK;
}

hal_status_t hal_usb_cdc_available(size_t *out_available) {
  if (out_available == nullptr) {
    return HAL_EINVAL;
  }
  *out_available = 0u;
  if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE) ||
      !usb_stack_inited()) {
    return HAL_EUNINIT;
  }

  HAL_USB_LOCK_GUARD(lock);
  if (!HAL_USB_LOCK_OK(lock)) {
    return HAL_EBUSY;
  }
  usb_task_locked();
  *out_available = (size_t)tud_cdc_available();
  return HAL_OK;
}

hal_status_t hal_usb_cdc_read(uint8_t *data, size_t capacity,
                              size_t *out_read) {
  if (out_read == nullptr || (data == nullptr && capacity != 0u)) {
    return HAL_EINVAL;
  }
  *out_read = 0u;
  if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE) ||
      !usb_stack_inited()) {
    return HAL_EUNINIT;
  }
  if (capacity == 0u) {
    return HAL_EAGAIN;
  }

  HAL_USB_LOCK_GUARD(lock);
  if (!HAL_USB_LOCK_OK(lock)) {
    return HAL_EBUSY;
  }
  usb_task_locked();
  const uint32_t request =
      capacity > UINT32_MAX ? UINT32_MAX : (uint32_t)capacity;
  const uint32_t count = tud_cdc_read(data, request);
  *out_read = (size_t)count;
  return count != 0u ? HAL_OK : HAL_EAGAIN;
}

hal_status_t hal_usb_cdc_write(const uint8_t *data, size_t length,
                               uint32_t timeout_ms, size_t *out_written) {
  if (out_written == nullptr || (data == nullptr && length != 0u)) {
    return HAL_EINVAL;
  }
  *out_written = 0u;
  if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE) ||
      !usb_stack_inited()) {
    return HAL_EUNINIT;
  }
  if (length == 0u) {
    return HAL_OK;
  }

  HAL_USB_LOCK_GUARD(lock);
  if (!HAL_USB_LOCK_OK(lock)) {
    return HAL_EBUSY;
  }
  usb_task_locked();
  if (!usb_connected_locked()) {
    return HAL_EAGAIN;
  }
  if (tud_suspended()) {
    (void)tud_remote_wakeup();
  }

  size_t written_total = 0u;
  uint64_t last_progress_us = time_us_64();
  const uint64_t timeout_us = (uint64_t)timeout_ms * 1000u;

  while (written_total < length) {
    usb_task_locked();
    if (!usb_connected_locked()) {
      break;
    }

    const uint32_t available = tud_cdc_write_available();
    const size_t remaining = length - written_total;
    const uint32_t chunk =
        remaining < (size_t)available ? (uint32_t)remaining : available;
    if (chunk != 0u) {
      const uint32_t written = tud_cdc_write(data + written_total, chunk);
      (void)tud_cdc_write_flush();
      usb_task_locked();
      if (written != 0u) {
        written_total += written;
        last_progress_us = time_us_64();
        continue;
      }
    } else {
      (void)tud_cdc_write_flush();
    }

    if (timeout_ms == 0u || time_us_64() - last_progress_us >= timeout_us) {
      break;
    }
  }

  *out_written = written_total;
  if (written_total == length) {
    return HAL_OK;
  }
  return usb_connected_locked() ? HAL_ETIMEOUT : HAL_EAGAIN;
}

hal_status_t hal_usb_cdc_flush(uint32_t timeout_ms) {
  if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE) ||
      !usb_stack_inited()) {
    return HAL_EUNINIT;
  }

  HAL_USB_LOCK_GUARD(lock);
  if (!HAL_USB_LOCK_OK(lock)) {
    return HAL_EBUSY;
  }
  usb_task_locked();
  if (!usb_connected_locked()) {
    return HAL_EAGAIN;
  }

  const uint64_t start_us = time_us_64();
  const uint64_t timeout_us = (uint64_t)timeout_ms * 1000u;
  for (;;) {
    usb_task_locked();
    if (!usb_connected_locked()) {
      return HAL_EAGAIN;
    }
    if (tud_cdc_write_flush() == 0u) {
      return HAL_OK;
    }
    if (timeout_ms == 0u || time_us_64() - start_us >= timeout_us) {
      return HAL_ETIMEOUT;
    }
  }
}

hal_status_t hal_usb_reset_to_bootloader(void) {
  notify_reset_hook();
  reset_usb_boot(0u, 0u);
  return HAL_EHW;
}

hal_status_t
hal_usb_set_bootloader_reset_hook(hal_usb_bootloader_reset_hook_t hook,
                                  void *user) {
  s_reset_hook_user = user;
  __atomic_store_n(&s_reset_hook, hook, __ATOMIC_RELEASE);
  return HAL_OK;
}

hal_status_t jh_rp_usb_flash_quiesce(uint32_t timeout_ms,
                                     bool *out_mutex_held) {
  if (out_mutex_held == nullptr) {
    return HAL_EINVAL;
  }
  *out_mutex_held = false;
  if (hal_in_isr()) {
    return HAL_ESTATE;
  }
  if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
    return HAL_OK;
  }

#if JH_RP_USB_FREERTOS
  if (__atomic_load_n(&s_worker_task, __ATOMIC_ACQUIRE) ==
      xTaskGetCurrentTaskHandle()) {
    return HAL_ESTATE;
  }
#endif

  const uintptr_t owner = flash_owner_token();
  if (__atomic_load_n(&s_flash_paused, __ATOMIC_ACQUIRE) &&
      __atomic_load_n(&s_flash_pause_owner, __ATOMIC_ACQUIRE) == owner) {
    return HAL_ESTATE;
  }

  hal_mutex_t mutex = usb_mutex();
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

  if (__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
    usb_task_locked();
    (void)tud_cdc_write_flush();
  }
  __atomic_store_n(&s_flash_pause_owner, owner, __ATOMIC_RELEASE);
  __atomic_store_n(&s_flash_paused, true, __ATOMIC_RELEASE);
  *out_mutex_held = true;
  return HAL_OK;
}

hal_status_t jh_rp_usb_flash_resume(bool mutex_held) {
  if (!mutex_held) {
    return HAL_OK;
  }
  const uintptr_t owner = flash_owner_token();
  if (!__atomic_load_n(&s_flash_paused, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&s_flash_pause_owner, __ATOMIC_ACQUIRE) != owner) {
    return HAL_ESTATE;
  }

  if (__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
    usb_task_locked();
  }
  __atomic_store_n(&s_flash_paused, false, __ATOMIC_RELEASE);
  __atomic_store_n(&s_flash_pause_owner, UINTPTR_MAX, __ATOMIC_RELEASE);
  hal_mutex_unlock(usb_mutex());
  return HAL_OK;
}

extern "C" void tud_cdc_line_coding_cb(uint8_t interface_number,
                                       cdc_line_coding_t const *line_coding) {
  (void)interface_number;
  if (line_coding == nullptr) {
    return;
  }
  const bool armed = line_coding->bit_rate == HAL_USB_BOOTLOADER_TOUCH_BAUD;
  __atomic_store_n(&s_touch_armed, armed, __ATOMIC_RELEASE);
}

extern "C" void tud_cdc_line_state_cb(uint8_t interface_number, bool dtr,
                                      bool rts) {
  (void)interface_number;
  (void)rts;
  if (!dtr && __atomic_load_n(&s_touch_armed, __ATOMIC_ACQUIRE)) {
    (void)hal_usb_reset_to_bootloader();
  }
}

#endif
