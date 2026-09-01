#include "hal/bluetooth/hal_gamepad.h"

#ifdef HAL_ENABLE_BLUETOOTH_GAMEPAD

#include "hal/bluetooth/jh_bluetooth_runtime.h"
#include "hal/bluetooth/jh_gamepad_backend.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/system/hal_sync.h"

#define JH_GAMEPAD_HANDLE_KIND 14u

namespace {

struct gamepad_runtime_t {
  hal_mutex_t mutex;
  const jh_gamepad_backend_t *backend;
  jh_handle_pool_t handle_pool;
  jh_handle_slot_t handle_slot;
  bool handle_pool_initialized;
  bool operation_active;
};

gamepad_runtime_t s_gamepad{};

hal_mutex_t runtime_mutex(void) {
  return jh_hal_mutex_create_once(&s_gamepad.mutex);
}

hal_status_t ensure_handle_pool_locked(void) {
  if (s_gamepad.handle_pool_initialized) {
    return HAL_OK;
  }
  const hal_status_t status =
      jh_handle_pool_init(&s_gamepad.handle_pool, &s_gamepad.handle_slot, 1u,
                          JH_GAMEPAD_HANDLE_KIND);
  s_gamepad.handle_pool_initialized = status == HAL_OK;
  return status;
}

bool handle_valid_locked(hal_gamepad_t gamepad) {
  if (!s_gamepad.handle_pool_initialized || s_gamepad.backend == nullptr) {
    return false;
  }
  void *runtime = nullptr;
  return jh_handle_resolve(&s_gamepad.handle_pool, gamepad, &runtime,
                           nullptr) == HAL_OK &&
         runtime == &s_gamepad;
}

hal_status_t release_handle_locked(const void *handle) {
  void *runtime = nullptr;
  hal_status_t status =
      jh_handle_release(&s_gamepad.handle_pool, handle, &runtime);
  if (status == HAL_OK && runtime != &s_gamepad) {
    status = HAL_ESTATE;
  }
  if (status != HAL_OK) {
    jh_handle_invalidate_all(&s_gamepad.handle_pool);
  }
  return status;
}

template <typename Operation>
hal_status_t run_operation(hal_gamepad_t gamepad, Operation operation) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(gamepad)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_gamepad.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_gamepad.operation_active = true;
  const jh_gamepad_backend_t *backend = s_gamepad.backend;
  hal_mutex_unlock(mutex);

  const hal_status_t status = operation(backend);

  hal_mutex_lock(mutex);
  s_gamepad.operation_active = false;
  hal_mutex_unlock(mutex);
  return status;
}

bool backend_valid(const jh_gamepad_backend_t *backend) {
  return backend != nullptr && backend->start != nullptr &&
         backend->stop != nullptr && backend->service != nullptr &&
         backend->get_info != nullptr && backend->snapshot != nullptr &&
         backend->snapshot_next != nullptr &&
         backend->pairing_open != nullptr &&
         backend->pairing_authorize != nullptr &&
         backend->reconnect != nullptr && backend->disconnect != nullptr;
}

} // namespace

hal_status_t hal_gamepad_open(hal_gamepad_t *out_gamepad) {
  if (out_gamepad == nullptr) {
    return HAL_EINVAL;
  }
  *out_gamepad = nullptr;
  const hal_status_t hardware_status = jh_bluetooth_require_classic_hardware();
  if (hardware_status != HAL_OK) {
    return hardware_status;
  }
  const jh_gamepad_backend_t *backend = jh_gamepad_backend_instance();
  if (!backend_valid(backend)) {
    return HAL_ECONFIG;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_gamepad.backend != nullptr || s_gamepad.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  hal_status_t status = ensure_handle_pool_locked();
  if (status != HAL_OK) {
    hal_mutex_unlock(mutex);
    return status;
  }
  void *handle = nullptr;
  status = jh_handle_allocate(&s_gamepad.handle_pool, &s_gamepad, &handle);
  if (status != HAL_OK) {
    hal_mutex_unlock(mutex);
    return status;
  }
  s_gamepad.operation_active = true;
  s_gamepad.backend = backend;
  hal_mutex_unlock(mutex);

  status = backend->start(backend->context);

  hal_mutex_lock(mutex);
  s_gamepad.operation_active = false;
  if (status == HAL_OK) {
    *out_gamepad = static_cast<hal_gamepad_t>(handle);
  } else {
    (void)release_handle_locked(handle);
    s_gamepad.backend = nullptr;
  }
  hal_mutex_unlock(mutex);
  if (status == HAL_OK) {
    jh_bluetooth_publish_available(HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER);
  } else if (status == HAL_EHW || status == HAL_EIO) {
    jh_bluetooth_publish_failed(HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER);
  }
  return status;
}

hal_status_t hal_gamepad_close(hal_gamepad_t gamepad) {
  return run_operation(gamepad, [gamepad](const jh_gamepad_backend_t *backend) {
    const hal_status_t backend_status = backend->stop(backend->context);
    hal_mutex_t mutex = runtime_mutex();
    hal_mutex_lock(mutex);
    const hal_status_t release_status = release_handle_locked(gamepad);
    s_gamepad.backend = nullptr;
    hal_mutex_unlock(mutex);
    const hal_status_t status =
        backend_status != HAL_OK ? backend_status : release_status;
    if (status == HAL_OK) {
      jh_bluetooth_publish_inactive(HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER);
    } else {
      jh_bluetooth_publish_failed(HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER);
    }
    return status;
  });
}

hal_status_t hal_gamepad_poll(hal_gamepad_t gamepad) {
  const hal_status_t status =
      run_operation(gamepad, [](const jh_gamepad_backend_t *backend) {
        return backend->service(backend->context);
      });
  if (status == HAL_EHW || status == HAL_EIO) {
    jh_bluetooth_publish_failed(HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER);
  }
  return status;
}

hal_status_t hal_gamepad_get_info(hal_gamepad_t gamepad,
                                  hal_gamepad_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  return run_operation(gamepad,
                       [out_info](const jh_gamepad_backend_t *backend) {
                         return backend->get_info(backend->context, out_info);
                       });
}

hal_status_t hal_gamepad_snapshot(hal_gamepad_t gamepad,
                                  hal_gamepad_snapshot_t *out_snapshot) {
  if (out_snapshot == nullptr) {
    return HAL_EINVAL;
  }
  return run_operation(
      gamepad, [out_snapshot](const jh_gamepad_backend_t *backend) {
        return backend->snapshot(backend->context, out_snapshot);
      });
}

hal_status_t hal_gamepad_snapshot_next(hal_gamepad_t gamepad,
                                       hal_gamepad_snapshot_t *out_snapshot) {
  if (out_snapshot == nullptr) {
    return HAL_EINVAL;
  }
  return run_operation(
      gamepad, [out_snapshot](const jh_gamepad_backend_t *backend) {
        return backend->snapshot_next(backend->context, out_snapshot);
      });
}

hal_status_t hal_gamepad_pairing_open(hal_gamepad_t gamepad) {
  return run_operation(gamepad, [](const jh_gamepad_backend_t *backend) {
    return backend->pairing_open(backend->context);
  });
}

hal_status_t hal_gamepad_pairing_authorize(hal_gamepad_t gamepad) {
  return run_operation(gamepad, [](const jh_gamepad_backend_t *backend) {
    return backend->pairing_authorize(backend->context);
  });
}

hal_status_t hal_gamepad_reconnect(hal_gamepad_t gamepad) {
  return run_operation(gamepad, [](const jh_gamepad_backend_t *backend) {
    return backend->reconnect(backend->context);
  });
}

hal_status_t hal_gamepad_disconnect(hal_gamepad_t gamepad) {
  return run_operation(gamepad, [](const jh_gamepad_backend_t *backend) {
    return backend->disconnect(backend->context);
  });
}

#if HAL_TARGET_IS_MOCK
void hal_mock_gamepad_runtime_full_reset(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_gamepad.handle_pool_initialized) {
    jh_handle_invalidate_all(&s_gamepad.handle_pool);
  }
  s_gamepad.backend = nullptr;
  s_gamepad.operation_active = false;
  hal_mutex_unlock(mutex);
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_GAMEPAD */
