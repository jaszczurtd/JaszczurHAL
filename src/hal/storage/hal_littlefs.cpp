#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "hal/core/hal_mutex_once.h"
#include "hal/serial/hal_serial.h"
#include "hal/storage/hal_littlefs.h"
#include "hal/storage/jh_littlefs_provider.h"
#include "hal/system/hal_sync.h"

namespace {

hal_mutex_t s_littlefs_mutex = nullptr;
const jh_littlefs_provider_t *s_provider = nullptr;
bool s_mounted = false;

hal_mutex_t littlefs_mutex() {
  return jh_hal_mutex_create_once(&s_littlefs_mutex);
}

bool provider_is_complete(const jh_littlefs_provider_t *provider) {
  return provider != nullptr && provider->ops != nullptr &&
         provider->ops->set_progress_callback != nullptr &&
         provider->ops->mount != nullptr && provider->ops->unmount != nullptr &&
         provider->ops->format != nullptr && provider->ops->exists != nullptr &&
         provider->ops->remove != nullptr &&
         provider->ops->total_bytes != nullptr &&
         provider->ops->used_bytes != nullptr;
}

const jh_littlefs_provider_t *provider_locked() {
  if (s_provider == nullptr) {
    const jh_littlefs_provider_t *provider = jh_littlefs_provider_get();
    if (provider_is_complete(provider)) {
      s_provider = provider;
    }
  }
  return s_provider;
}

hal_status_t lock_state(hal_mutex_t *out_mutex) {
  if (out_mutex == nullptr) {
    return HAL_EINVAL;
  }
  *out_mutex = littlefs_mutex();
  if (*out_mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(*out_mutex);
  return HAL_OK;
}

hal_status_t validate_path(const char *path, const char *function_name) {
  if (path == nullptr || path[0] == '\0') {
    hal_derr("%s: path is NULL/empty", function_name);
    return HAL_EINVAL;
  }
  return HAL_OK;
}

} // namespace

hal_status_t
hal_littlefs_set_progress_callback(hal_littlefs_progress_callback_t callback,
                                   void *ctx) {
  hal_mutex_t mutex = nullptr;
  hal_status_t status = lock_state(&mutex);
  if (status != HAL_OK) {
    return status;
  }
  const jh_littlefs_provider_t *provider = provider_locked();
  status = provider != nullptr ? provider->ops->set_progress_callback(
                                     provider->context, callback, ctx)
                               : HAL_ECONFIG;
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_littlefs_begin_ex(void) {
  hal_mutex_t mutex = nullptr;
  hal_status_t status = lock_state(&mutex);
  if (status != HAL_OK) {
    return status;
  }

  const jh_littlefs_provider_t *provider = provider_locked();
  if (provider == nullptr) {
    status = HAL_ECONFIG;
  } else if (s_mounted) {
    status = HAL_OK;
  } else {
    status = provider->ops->mount(provider->context);
    s_mounted = status == HAL_OK;
  }

  hal_mutex_unlock(mutex);
  if (status != HAL_OK) {
    hal_derr("hal_littlefs_begin: mount failed with status %d", (int)status);
  }
  return status;
}

bool hal_littlefs_begin(void) {
  return hal_status_to_bool(hal_littlefs_begin_ex());
}

hal_status_t hal_littlefs_end(void) {
  hal_mutex_t mutex = nullptr;
  hal_status_t status = lock_state(&mutex);
  if (status != HAL_OK) {
    return status;
  }

  const jh_littlefs_provider_t *provider = provider_locked();
  if (provider == nullptr) {
    status = HAL_ECONFIG;
  } else if (s_mounted) {
    status = provider->ops->unmount(provider->context);
  } else {
    status = HAL_OK;
  }
  s_mounted = false;

  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_littlefs_format_ex(void) {
  hal_mutex_t mutex = nullptr;
  hal_status_t status = lock_state(&mutex);
  if (status != HAL_OK) {
    return status;
  }

  const jh_littlefs_provider_t *provider = provider_locked();
  const bool was_mounted = s_mounted;
  if (provider == nullptr) {
    status = HAL_ECONFIG;
  } else {
    if (was_mounted) {
      status = provider->ops->unmount(provider->context);
      s_mounted = false;
    }
    if (!was_mounted || status == HAL_OK) {
      status = provider->ops->format(provider->context);
    }
    if (status != HAL_OK && was_mounted) {
      s_mounted = provider->ops->mount(provider->context) == HAL_OK;
    }
  }

  hal_mutex_unlock(mutex);
  if (status != HAL_OK) {
    hal_derr("hal_littlefs_format: format failed with status %d", (int)status);
  }
  return status;
}

bool hal_littlefs_format(void) {
  return hal_status_to_bool(hal_littlefs_format_ex());
}

bool hal_littlefs_is_mounted(void) {
  hal_mutex_t mutex = nullptr;
  if (lock_state(&mutex) != HAL_OK) {
    return false;
  }
  const bool mounted = s_mounted;
  hal_mutex_unlock(mutex);
  return mounted;
}

hal_status_t hal_littlefs_exists_ex(const char *path) {
  hal_status_t status = validate_path(path, "hal_littlefs_exists");
  if (status != HAL_OK) {
    return status;
  }

  hal_mutex_t mutex = nullptr;
  status = lock_state(&mutex);
  if (status != HAL_OK) {
    return status;
  }
  const jh_littlefs_provider_t *provider = provider_locked();
  if (provider == nullptr) {
    status = HAL_ECONFIG;
  } else if (!s_mounted) {
    status = HAL_EUNINIT;
  } else {
    status = provider->ops->exists(provider->context, path);
  }
  hal_mutex_unlock(mutex);
  if (status == HAL_EUNINIT) {
    hal_derr("hal_littlefs_exists: filesystem is not mounted");
  }
  return status;
}

bool hal_littlefs_exists(const char *path) {
  return hal_status_to_bool(hal_littlefs_exists_ex(path));
}

hal_status_t hal_littlefs_remove_ex(const char *path) {
  hal_status_t status = validate_path(path, "hal_littlefs_remove");
  if (status != HAL_OK) {
    return status;
  }

  hal_mutex_t mutex = nullptr;
  status = lock_state(&mutex);
  if (status != HAL_OK) {
    return status;
  }
  const jh_littlefs_provider_t *provider = provider_locked();
  if (provider == nullptr) {
    status = HAL_ECONFIG;
  } else if (!s_mounted) {
    status = HAL_EUNINIT;
  } else {
    status = provider->ops->remove(provider->context, path);
  }
  hal_mutex_unlock(mutex);
  if (status == HAL_EUNINIT) {
    hal_derr("hal_littlefs_remove: filesystem is not mounted");
  }
  return status;
}

bool hal_littlefs_remove(const char *path) {
  return hal_status_to_bool(hal_littlefs_remove_ex(path));
}

hal_status_t hal_littlefs_total_bytes_ex(size_t *out_bytes) {
  if (out_bytes == nullptr) {
    return HAL_EINVAL;
  }
  *out_bytes = 0u;

  hal_mutex_t mutex = nullptr;
  hal_status_t status = lock_state(&mutex);
  if (status != HAL_OK) {
    return status;
  }
  const jh_littlefs_provider_t *provider = provider_locked();
  if (provider == nullptr) {
    status = HAL_ECONFIG;
  } else if (!s_mounted) {
    status = HAL_EUNINIT;
  } else {
    status = provider->ops->total_bytes(provider->context, out_bytes);
  }
  hal_mutex_unlock(mutex);
  return status;
}

size_t hal_littlefs_total_bytes(void) {
  size_t bytes = 0u;
  (void)hal_littlefs_total_bytes_ex(&bytes);
  return bytes;
}

hal_status_t hal_littlefs_used_bytes_ex(size_t *out_bytes) {
  if (out_bytes == nullptr) {
    return HAL_EINVAL;
  }
  *out_bytes = 0u;

  hal_mutex_t mutex = nullptr;
  hal_status_t status = lock_state(&mutex);
  if (status != HAL_OK) {
    return status;
  }
  const jh_littlefs_provider_t *provider = provider_locked();
  if (provider == nullptr) {
    status = HAL_ECONFIG;
  } else if (!s_mounted) {
    status = HAL_EUNINIT;
  } else {
    status = provider->ops->used_bytes(provider->context, out_bytes);
  }
  hal_mutex_unlock(mutex);
  return status;
}

size_t hal_littlefs_used_bytes(void) {
  size_t bytes = 0u;
  (void)hal_littlefs_used_bytes_ex(&bytes);
  return bytes;
}

void jh_littlefs_mock_reset_facade(void) {
  hal_mutex_t mutex = nullptr;
  if (lock_state(&mutex) != HAL_OK) {
    return;
  }
  s_mounted = false;
  s_provider = nullptr;
  hal_mutex_unlock(mutex);
}

#endif /* HAL_ENABLE_LITTLEFS */
