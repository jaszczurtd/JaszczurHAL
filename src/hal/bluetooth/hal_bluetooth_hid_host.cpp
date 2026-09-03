#include "hal/bluetooth/hal_bluetooth_hid_host.h"

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST

#include "hal/bluetooth/jh_bluetooth_classic_runtime.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_target.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/system/hal_sync.h"

#define JH_BLUETOOTH_HID_HANDLE_KIND 16u

namespace {

struct hid_runtime_t {
  hal_mutex_t mutex;
  jh_handle_pool_t handle_pool;
  jh_handle_slot_t handle_slot;
  hal_bluetooth_classic_t classic;
  bool handle_pool_initialized;
};

hid_runtime_t s_hid{};

hal_mutex_t runtime_mutex() { return jh_hal_mutex_create_once(&s_hid.mutex); }

hal_status_t ensure_handle_pool_locked() {
  if (s_hid.handle_pool_initialized) {
    return HAL_OK;
  }
  const hal_status_t status = jh_handle_pool_init(
      &s_hid.handle_pool, &s_hid.handle_slot, 1u, JH_BLUETOOTH_HID_HANDLE_KIND);
  s_hid.handle_pool_initialized = status == HAL_OK;
  return status;
}

bool handle_valid_locked(hal_bluetooth_hid_host_t hid_host) {
  if (!s_hid.handle_pool_initialized || s_hid.classic == nullptr) {
    return false;
  }
  void *runtime = nullptr;
  return jh_handle_resolve(&s_hid.handle_pool, hid_host, &runtime, nullptr) ==
             HAL_OK &&
         runtime == &s_hid && jh_bluetooth_classic_handle_valid(s_hid.classic);
}

hal_status_t resolve(hal_bluetooth_hid_host_t hid_host,
                     hal_bluetooth_classic_t *out_classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(hid_host)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  *out_classic = s_hid.classic;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

} // namespace

hal_status_t
hal_bluetooth_hid_host_open(hal_bluetooth_classic_t classic,
                            hal_bluetooth_hid_host_t *out_hid_host) {
  if (classic == nullptr || out_hid_host == nullptr) {
    return HAL_EINVAL;
  }
  *out_hid_host = nullptr;
  if (!jh_bluetooth_classic_handle_valid(classic)) {
    return HAL_EUNINIT;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_hid.classic != nullptr) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  hal_status_t status = ensure_handle_pool_locked();
  void *handle = nullptr;
  if (status == HAL_OK) {
    status = jh_handle_allocate(&s_hid.handle_pool, &s_hid, &handle);
  }
  if (status == HAL_OK) {
    status = jh_bluetooth_classic_hid_attach(classic);
  }
  if (status == HAL_OK) {
    s_hid.classic = classic;
    *out_hid_host = static_cast<hal_bluetooth_hid_host_t>(handle);
  } else if (handle != nullptr) {
    void *runtime = nullptr;
    (void)jh_handle_release(&s_hid.handle_pool, handle, &runtime);
  }
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_bluetooth_hid_host_close(hal_bluetooth_hid_host_t hid_host) {
  hal_bluetooth_classic_t classic = nullptr;
  hal_status_t status = resolve(hid_host, &classic);
  if (status != HAL_OK) {
    return status;
  }
  hal_bluetooth_hid_info_t info{};
  status = jh_bluetooth_classic_hid_get_info(classic, &info);
  if (status != HAL_OK) {
    return status;
  }
  if (info.state == HAL_BLUETOOTH_HID_STATE_CONNECTING ||
      info.state == HAL_BLUETOOTH_HID_STATE_CONNECTED) {
    status = jh_bluetooth_classic_hid_disconnect(classic);
    if (status != HAL_OK && status != HAL_ESTATE) {
      return status;
    }
  }
  status = jh_bluetooth_classic_hid_detach(classic);
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_hid.mutex);
  void *runtime = nullptr;
  const hal_status_t release_status =
      jh_handle_release(&s_hid.handle_pool, hid_host, &runtime);
  s_hid.classic = nullptr;
  hal_mutex_unlock(s_hid.mutex);
  return release_status;
}

hal_status_t
hal_bluetooth_hid_host_get_info(hal_bluetooth_hid_host_t hid_host,
                                hal_bluetooth_hid_info_t *out_info) {
  hal_bluetooth_classic_t classic = nullptr;
  const hal_status_t status = resolve(hid_host, &classic);
  return status == HAL_OK ? jh_bluetooth_classic_hid_get_info(classic, out_info)
                          : status;
}

hal_status_t
hal_bluetooth_hid_host_connect(hal_bluetooth_hid_host_t hid_host,
                               const hal_bluetooth_classic_address_t *address) {
  hal_bluetooth_classic_t classic = nullptr;
  const hal_status_t status = resolve(hid_host, &classic);
  return status == HAL_OK ? jh_bluetooth_classic_hid_connect(classic, address)
                          : status;
}

hal_status_t
hal_bluetooth_hid_host_disconnect(hal_bluetooth_hid_host_t hid_host) {
  hal_bluetooth_classic_t classic = nullptr;
  const hal_status_t status = resolve(hid_host, &classic);
  return status == HAL_OK ? jh_bluetooth_classic_hid_disconnect(classic)
                          : status;
}

hal_status_t
hal_bluetooth_hid_host_descriptor(hal_bluetooth_hid_host_t hid_host,
                                  uint8_t *out_descriptor, size_t capacity,
                                  size_t *out_length) {
  hal_bluetooth_classic_t classic = nullptr;
  const hal_status_t status = resolve(hid_host, &classic);
  return status == HAL_OK ? jh_bluetooth_classic_hid_descriptor(
                                classic, out_descriptor, capacity, out_length)
                          : status;
}

hal_status_t
hal_bluetooth_hid_host_report_next(hal_bluetooth_hid_host_t hid_host,
                                   hal_bluetooth_hid_report_t *out_report) {
  hal_bluetooth_classic_t classic = nullptr;
  const hal_status_t status = resolve(hid_host, &classic);
  return status == HAL_OK
             ? jh_bluetooth_classic_hid_report_next(classic, out_report)
             : status;
}

hal_status_t
hal_bluetooth_hid_host_report_send(hal_bluetooth_hid_host_t hid_host,
                                   const hal_bluetooth_hid_report_t *report) {
  hal_bluetooth_classic_t classic = nullptr;
  const hal_status_t status = resolve(hid_host, &classic);
  return status == HAL_OK
             ? jh_bluetooth_classic_hid_report_send(classic, report)
             : status;
}

hal_status_t
hal_bluetooth_hid_host_report_request(hal_bluetooth_hid_host_t hid_host,
                                      hal_bluetooth_hid_report_type_t type,
                                      uint8_t report_id) {
  hal_bluetooth_classic_t classic = nullptr;
  const hal_status_t status = resolve(hid_host, &classic);
  return status == HAL_OK
             ? jh_bluetooth_classic_hid_report_request(classic, type, report_id)
             : status;
}

#if HAL_TARGET_IS_MOCK
void hal_mock_bluetooth_hid_runtime_full_reset(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_hid.handle_pool_initialized) {
    jh_handle_invalidate_all(&s_hid.handle_pool);
  }
  s_hid.classic = nullptr;
  hal_mutex_unlock(mutex);
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_HID_HOST */
