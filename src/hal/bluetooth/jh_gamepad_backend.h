#pragma once

#include "hal/bluetooth/hal_gamepad.h"

#ifdef HAL_ENABLE_BLUETOOTH_GAMEPAD

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *context;
  hal_status_t (*start)(void *context);
  hal_status_t (*stop)(void *context);
  hal_status_t (*service)(void *context);
  hal_status_t (*get_info)(void *context, hal_gamepad_info_t *out_info);
  hal_status_t (*snapshot)(void *context, hal_gamepad_snapshot_t *out_snapshot);
  hal_status_t (*snapshot_next)(void *context,
                                hal_gamepad_snapshot_t *out_snapshot);
  hal_status_t (*pairing_open)(void *context);
  hal_status_t (*pairing_authorize)(void *context);
  hal_status_t (*reconnect)(void *context);
  hal_status_t (*disconnect)(void *context);
} jh_gamepad_backend_t;

const jh_gamepad_backend_t *jh_gamepad_backend_instance(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_GAMEPAD */
