#pragma once

#include "hal/bluetooth/hal_gamepad.h"
#include "hal/bluetooth/jh_bluetooth_gamepad_parser.h"

#ifdef HAL_ENABLE_BLUETOOTH_GAMEPAD

#ifdef __cplusplus
extern "C" {
#endif

static inline void
jh_gamepad_copy_snapshot(const jh_bluetooth_gamepad_snapshot_t *source,
                         hal_gamepad_snapshot_t *destination) {
  destination->generation = source->generation;
  destination->buttons = source->buttons;
  for (size_t index = 0u; index < HAL_GAMEPAD_AXIS_COUNT; ++index) {
    destination->axes[index] = source->axes[index];
  }
  destination->axes_present = source->axes_present;
  destination->dpad = source->dpad;
  destination->connected = source->connected;
}

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
