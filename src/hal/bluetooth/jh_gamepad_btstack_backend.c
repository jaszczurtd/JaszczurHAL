#include "hal/core/hal_target.h"

#if !HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_BLUETOOTH_GAMEPAD)

#include "jh_bluetooth_classic_hid_probe.h"
#include "jh_gamepad_backend.h"

#include <string.h>

_Static_assert((unsigned)HAL_GAMEPAD_AXIS_COUNT ==
                   (unsigned)JH_BLUETOOTH_GAMEPAD_AXIS_COUNT,
               "public and parser axis counts must match");
_Static_assert((unsigned)HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH ==
                   (unsigned)JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY,
               "public and parser queue depths must match");

static void copy_snapshot(const jh_bluetooth_gamepad_snapshot_t *source,
                          hal_gamepad_snapshot_t *destination) {
  destination->generation = source->generation;
  destination->buttons = source->buttons;
  memcpy(destination->axes, source->axes, sizeof(destination->axes));
  destination->axes_present = source->axes_present;
  destination->dpad = source->dpad;
  destination->connected = source->connected;
}

static hal_gamepad_state_t
public_state(const jh_bluetooth_classic_hid_probe_snapshot_t *snapshot) {
  if (snapshot->transport_status == HAL_EHW ||
      snapshot->transport_status == HAL_EIO) {
    return HAL_GAMEPAD_STATE_FAILED;
  }
  switch (snapshot->phase) {
  case JH_CLASSIC_HID_PHASE_IDLE:
    return snapshot->started ? HAL_GAMEPAD_STATE_STARTING
                             : HAL_GAMEPAD_STATE_UNINITIALIZED;
  case JH_CLASSIC_HID_PHASE_READY:
  case JH_CLASSIC_HID_PHASE_KNOWN_IDLE:
    return HAL_GAMEPAD_STATE_READY;
  case JH_CLASSIC_HID_PHASE_INQUIRY:
  case JH_CLASSIC_HID_PHASE_REMOTE_NAME:
  case JH_CLASSIC_HID_PHASE_SDP_HID:
  case JH_CLASSIC_HID_PHASE_SDP_PNP:
    return HAL_GAMEPAD_STATE_DISCOVERING;
  case JH_CLASSIC_HID_PHASE_CONNECTING:
    return HAL_GAMEPAD_STATE_CONNECTING;
  case JH_CLASSIC_HID_PHASE_CONNECTED:
    return HAL_GAMEPAD_STATE_CONNECTED;
  default:
    return HAL_GAMEPAD_STATE_FAILED;
  }
}

static hal_status_t backend_start(void *context) {
  (void)context;
  jh_bluetooth_classic_hid_probe_retain_gamepad_queue(true);
  const hal_status_t status = jh_bluetooth_classic_hid_probe_start();
  if (status != HAL_OK) {
    jh_bluetooth_classic_hid_probe_retain_gamepad_queue(false);
  }
  return status;
}

static hal_status_t backend_stop(void *context) {
  (void)context;
  const hal_status_t status = jh_bluetooth_classic_hid_probe_stop();
  jh_bluetooth_classic_hid_probe_retain_gamepad_queue(false);
  return status;
}

static hal_status_t backend_service(void *context) {
  (void)context;
  return jh_bluetooth_classic_hid_probe_service();
}

static hal_status_t backend_get_info(void *context,
                                     hal_gamepad_info_t *out_info) {
  (void)context;
  if (out_info == NULL) {
    return HAL_EINVAL;
  }
  jh_bluetooth_classic_hid_probe_snapshot_t probe;
  jh_bluetooth_classic_hid_probe_snapshot(&probe);
  memset(out_info, 0, sizeof(*out_info));
  out_info->state = public_state(&probe);
  out_info->last_status = probe.last_status;
  jh_bluetooth_gamepad_snapshot_t current;
  if (jh_bluetooth_classic_hid_probe_gamepad_snapshot(&current) == HAL_OK) {
    out_info->generation = current.generation;
  }
  out_info->dropped_snapshots = probe.parser.dropped_snapshots;
  out_info->pending_snapshots =
      jh_bluetooth_classic_hid_probe_gamepad_pending();
  out_info->pairing_window_open = probe.discovery_open;
  out_info->pairing_pending = probe.pairing_pending;
  out_info->known_device = probe.known_device;
  return HAL_OK;
}

static hal_status_t backend_snapshot(void *context,
                                     hal_gamepad_snapshot_t *out_snapshot) {
  (void)context;
  if (out_snapshot == NULL) {
    return HAL_EINVAL;
  }
  jh_bluetooth_gamepad_snapshot_t snapshot;
  const hal_status_t status =
      jh_bluetooth_classic_hid_probe_gamepad_snapshot(&snapshot);
  if (status == HAL_OK) {
    copy_snapshot(&snapshot, out_snapshot);
  }
  return status;
}

static hal_status_t
backend_snapshot_next(void *context, hal_gamepad_snapshot_t *out_snapshot) {
  (void)context;
  if (out_snapshot == NULL) {
    return HAL_EINVAL;
  }
  jh_bluetooth_gamepad_snapshot_t snapshot;
  const hal_status_t status =
      jh_bluetooth_classic_hid_probe_gamepad_next(&snapshot);
  if (status == HAL_OK) {
    copy_snapshot(&snapshot, out_snapshot);
  }
  return status;
}

static hal_status_t backend_pairing_open(void *context) {
  (void)context;
  return jh_bluetooth_classic_hid_probe_open_pairing_window();
}

static hal_status_t backend_pairing_authorize(void *context) {
  (void)context;
  return jh_bluetooth_classic_hid_probe_authorize_pairing();
}

static hal_status_t backend_reconnect(void *context) {
  (void)context;
  return jh_bluetooth_classic_hid_probe_reconnect();
}

static hal_status_t backend_disconnect(void *context) {
  (void)context;
  return jh_bluetooth_classic_hid_probe_disconnect();
}

static const jh_gamepad_backend_t s_backend = {
    .context = NULL,
    .start = backend_start,
    .stop = backend_stop,
    .service = backend_service,
    .get_info = backend_get_info,
    .snapshot = backend_snapshot,
    .snapshot_next = backend_snapshot_next,
    .pairing_open = backend_pairing_open,
    .pairing_authorize = backend_pairing_authorize,
    .reconnect = backend_reconnect,
    .disconnect = backend_disconnect,
};

const jh_gamepad_backend_t *jh_gamepad_backend_instance(void) {
  return &s_backend;
}

#endif /* !HAL_TARGET_IS_MOCK && HAL_ENABLE_BLUETOOTH_GAMEPAD */
