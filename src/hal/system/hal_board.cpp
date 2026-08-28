#include "hal/system/hal_board.h"

#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"
#include "hal/system/jh_board_runtime.h"

namespace {

hal_mutex_t s_board_mutex;
hal_board_capabilities_t s_available;
hal_board_capabilities_t s_failed;

hal_mutex_t board_mutex(void) {
  return jh_hal_mutex_create_once(&s_board_mutex);
}

bool capabilities_valid(hal_board_capabilities_t capabilities) {
  return capabilities != 0u && (capabilities & ~HAL_BOARD_CAP_ALL) == 0u;
}

bool single_capability(hal_board_capabilities_t capability) {
  return capabilities_valid(capability) &&
         (capability & (capability - 1u)) == 0u;
}

hal_status_t set_capability_state(hal_board_capabilities_t capabilities,
                                  hal_board_capabilities_t available,
                                  hal_board_capabilities_t failed) {
  if (!capabilities_valid(capabilities)) {
    return HAL_EINVAL;
  }
  if ((capabilities & ~HAL_BOARD_DECLARED_CAPABILITIES) != 0u) {
    return HAL_EUNSUPPORTED;
  }
  hal_mutex_t mutex = board_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  s_available = (s_available & ~capabilities) | (available & capabilities);
  s_failed = (s_failed & ~capabilities) | (failed & capabilities);
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

} // namespace

hal_status_t hal_board_get_info(hal_board_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = board_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  out_info->profile = HAL_BOARD_PROFILE_ID;
  out_info->name = HAL_BOARD_PROFILE_NAME;
  out_info->declared = HAL_BOARD_DECLARED_CAPABILITIES;
  out_info->available = s_available;
  out_info->failed = s_failed;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t
hal_board_get_capability_state(hal_board_capabilities_t capability,
                               hal_board_capability_state_t *out_state) {
  if (!single_capability(capability) || out_state == nullptr) {
    return HAL_EINVAL;
  }
  hal_board_info_t info{};
  hal_status_t status = hal_board_get_info(&info);
  if (status != HAL_OK) {
    return status;
  }
  if ((info.declared & capability) == 0u) {
    *out_state = HAL_BOARD_CAP_NOT_PRESENT;
  } else if ((info.failed & capability) != 0u) {
    *out_state = HAL_BOARD_CAP_FAILED;
  } else if ((info.available & capability) != 0u) {
    *out_state = HAL_BOARD_CAP_AVAILABLE;
  } else {
    *out_state = HAL_BOARD_CAP_INACTIVE;
  }
  return HAL_OK;
}

hal_status_t
hal_board_require_capabilities(hal_board_capabilities_t capabilities) {
  if (!capabilities_valid(capabilities)) {
    return HAL_EINVAL;
  }
  hal_board_info_t info{};
  hal_status_t status = hal_board_get_info(&info);
  if (status != HAL_OK) {
    return status;
  }
  if ((capabilities & ~info.declared) != 0u) {
    return HAL_EUNSUPPORTED;
  }
  if ((capabilities & info.failed) != 0u) {
    return HAL_EHW;
  }
  if ((capabilities & ~info.available) != 0u) {
    return HAL_EUNINIT;
  }
  return HAL_OK;
}

hal_status_t
jh_board_runtime_set_available(hal_board_capabilities_t capabilities) {
  return set_capability_state(capabilities, capabilities, 0u);
}

hal_status_t
jh_board_runtime_set_failed(hal_board_capabilities_t capabilities) {
  return set_capability_state(capabilities, 0u, capabilities);
}

hal_status_t
jh_board_runtime_set_inactive(hal_board_capabilities_t capabilities) {
  return set_capability_state(capabilities, 0u, 0u);
}

#if HAL_TARGET_IS_MOCK
/* Test-only: force the singleton mutex through a real destroy so
 * Helgrind/DRD can observe the teardown path. Firmware never calls this -
 * the mutex is a process-lifetime singleton by design. */
void hal_mock_board_runtime_full_reset(void) {
  if (s_board_mutex != nullptr) {
    hal_mutex_destroy(s_board_mutex);
    s_board_mutex = nullptr;
  }
}
#endif /* HAL_TARGET_IS_MOCK */
