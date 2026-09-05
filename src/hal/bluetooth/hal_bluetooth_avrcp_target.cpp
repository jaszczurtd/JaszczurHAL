#include "hal/bluetooth/hal_bluetooth_avrcp_target.h"

#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET

#include "hal/bluetooth/jh_bluetooth_avrcp_runtime.h"
#include "hal/bluetooth/jh_bluetooth_classic_runtime.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_target.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/system/hal_sync.h"

#include <string.h>

#define JH_BLUETOOTH_AVRCP_HANDLE_KIND 18u

namespace {

struct avrcp_runtime_t {
  hal_mutex_t mutex;
  jh_handle_pool_t handle_pool;
  jh_handle_slot_t handle_slot;
  hal_bluetooth_classic_t classic;
  hal_bluetooth_avrcp_target_info_t info;
  bool handle_pool_initialized;
};

avrcp_runtime_t s_avrcp{};

hal_mutex_t runtime_mutex() { return jh_hal_mutex_create_once(&s_avrcp.mutex); }

hal_status_t ensure_handle_pool_locked() {
  if (s_avrcp.handle_pool_initialized) {
    return HAL_OK;
  }
  const hal_status_t status =
      jh_handle_pool_init(&s_avrcp.handle_pool, &s_avrcp.handle_slot, 1u,
                          JH_BLUETOOTH_AVRCP_HANDLE_KIND);
  s_avrcp.handle_pool_initialized = status == HAL_OK;
  return status;
}

bool handle_valid_locked(hal_bluetooth_avrcp_target_t target) {
  if (!s_avrcp.handle_pool_initialized || s_avrcp.classic == nullptr) {
    return false;
  }
  void *runtime = nullptr;
  return jh_handle_resolve(&s_avrcp.handle_pool, target, &runtime, nullptr) ==
             HAL_OK &&
         runtime == &s_avrcp &&
         jh_bluetooth_classic_handle_valid(s_avrcp.classic);
}

hal_status_t resolve(hal_bluetooth_avrcp_target_t target,
                     hal_bluetooth_classic_t *out_classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(target)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  *out_classic = s_avrcp.classic;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

} // namespace

hal_status_t
hal_bluetooth_avrcp_target_open(hal_bluetooth_classic_t classic,
                                uint8_t initial_volume,
                                hal_bluetooth_avrcp_target_t *out_target) {
  if (classic == nullptr || out_target == nullptr || initial_volume > 127u) {
    return HAL_EINVAL;
  }
  *out_target = nullptr;
  if (!jh_bluetooth_classic_handle_valid(classic)) {
    return HAL_EUNINIT;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_avrcp.classic != nullptr) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  hal_status_t status = ensure_handle_pool_locked();
  void *handle = nullptr;
  if (status == HAL_OK) {
    status = jh_handle_allocate(&s_avrcp.handle_pool, &s_avrcp, &handle);
  }
  if (status == HAL_OK) {
    s_avrcp.classic = classic;
    memset(&s_avrcp.info, 0, sizeof(s_avrcp.info));
    s_avrcp.info.state = HAL_BLUETOOTH_AVRCP_TARGET_STATE_READY;
    s_avrcp.info.last_status = HAL_NONE;
    s_avrcp.info.volume = initial_volume;
  }
  hal_mutex_unlock(mutex);
  if (status == HAL_OK) {
    status = jh_bluetooth_classic_avrcp_attach(classic, initial_volume);
  }
  if (status != HAL_OK) {
    hal_mutex_lock(mutex);
    if (handle != nullptr) {
      void *runtime = nullptr;
      (void)jh_handle_release(&s_avrcp.handle_pool, handle, &runtime);
    }
    s_avrcp.classic = nullptr;
    hal_mutex_unlock(mutex);
    return status;
  }
  *out_target = static_cast<hal_bluetooth_avrcp_target_t>(handle);
  return HAL_OK;
}

hal_status_t
hal_bluetooth_avrcp_target_close(hal_bluetooth_avrcp_target_t target) {
  hal_bluetooth_classic_t classic = nullptr;
  hal_status_t status = resolve(target, &classic);
  if (status != HAL_OK) {
    return status;
  }
  status = jh_bluetooth_classic_avrcp_detach(classic);
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_avrcp.mutex);
  void *runtime = nullptr;
  const hal_status_t release_status =
      jh_handle_release(&s_avrcp.handle_pool, target, &runtime);
  memset(&s_avrcp.info, 0, sizeof(s_avrcp.info));
  s_avrcp.classic = nullptr;
  hal_mutex_unlock(s_avrcp.mutex);
  return release_status;
}

hal_status_t hal_bluetooth_avrcp_target_get_info(
    hal_bluetooth_avrcp_target_t target,
    hal_bluetooth_avrcp_target_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(target)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  *out_info = s_avrcp.info;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t
hal_bluetooth_avrcp_target_volume_next(hal_bluetooth_avrcp_target_t target,
                                       uint8_t *out_absolute_volume) {
  if (out_absolute_volume == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(target)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (!s_avrcp.info.volume_pending) {
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  *out_absolute_volume = s_avrcp.info.volume;
  s_avrcp.info.volume_pending = false;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t
hal_bluetooth_avrcp_target_set_volume(hal_bluetooth_avrcp_target_t target,
                                      uint8_t absolute_volume) {
  if (absolute_volume > 127u) {
    return HAL_EINVAL;
  }
  hal_bluetooth_classic_t classic = nullptr;
  hal_status_t status = resolve(target, &classic);
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_avrcp.mutex);
  s_avrcp.info.volume = absolute_volume;
  const bool connected =
      s_avrcp.info.state == HAL_BLUETOOTH_AVRCP_TARGET_STATE_CONNECTED;
  hal_mutex_unlock(s_avrcp.mutex);
  return connected
             ? jh_bluetooth_classic_avrcp_volume_set(classic, absolute_volume)
             : HAL_OK;
}

void jh_bluetooth_avrcp_target_backend_event(
    const jh_bluetooth_classic_backend_event_t *event) {
  if (event == nullptr) {
    return;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_avrcp.classic == nullptr) {
    hal_mutex_unlock(mutex);
    return;
  }
  switch (event->type) {
  case JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_CONNECTED:
    s_avrcp.info.state = HAL_BLUETOOTH_AVRCP_TARGET_STATE_CONNECTED;
    s_avrcp.info.last_status = event->status;
    s_avrcp.info.peer_address = event->address;
    ++s_avrcp.info.generation;
    if (s_avrcp.info.generation == 0u) {
      s_avrcp.info.generation = 1u;
    }
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_DISCONNECTED:
    s_avrcp.info.state = HAL_BLUETOOTH_AVRCP_TARGET_STATE_READY;
    s_avrcp.info.last_status = event->status;
    s_avrcp.info.volume_pending = false;
    memset(&s_avrcp.info.peer_address, 0, sizeof(s_avrcp.info.peer_address));
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_VOLUME:
    if (s_avrcp.info.volume_pending) {
      ++s_avrcp.info.overwritten_volume_changes;
    }
    s_avrcp.info.volume = event->absolute_volume;
    s_avrcp.info.volume_pending = true;
    ++s_avrcp.info.volume_changes;
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_ERROR:
    if (event->fatal) {
      s_avrcp.info.state = HAL_BLUETOOTH_AVRCP_TARGET_STATE_FAILED;
      s_avrcp.info.last_status = event->status;
      s_avrcp.info.volume_pending = false;
    }
    break;
  default:
    break;
  }
  hal_mutex_unlock(mutex);
}

#if HAL_TARGET_IS_MOCK
void hal_mock_bluetooth_avrcp_runtime_full_reset(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_avrcp.handle_pool_initialized) {
    jh_handle_invalidate_all(&s_avrcp.handle_pool);
  }
  memset(&s_avrcp.info, 0, sizeof(s_avrcp.info));
  s_avrcp.classic = nullptr;
  hal_mutex_unlock(mutex);
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_AVRCP_TARGET */
