#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_BLUETOOTH_GAMEPAD)

#include "hal/bluetooth/jh_gamepad_backend.h"
#include "hal_mock.h"

#include <string.h>

namespace {

static_assert(HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH >= 2u,
              "HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH must be at least 2");

struct mock_gamepad_t {
  hal_gamepad_info_t info;
  hal_gamepad_snapshot_t current;
  hal_gamepad_snapshot_t queue[HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH];
  size_t queue_head;
  size_t queue_count;
  hal_status_t service_status;
  bool started;
  bool overflow_pending;
  bool disconnect_pending;
};

mock_gamepad_t s_mock{};

void update_queue_info(void) {
  s_mock.info.pending_snapshots = s_mock.queue_count;
}

void enqueue(const hal_gamepad_snapshot_t &snapshot) {
  if (s_mock.queue_count == HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH) {
    s_mock.info.dropped_snapshots += (uint32_t)s_mock.queue_count;
    s_mock.queue_head = 0u;
    s_mock.queue_count = 0u;
    s_mock.overflow_pending = true;
  }
  const size_t tail = (s_mock.queue_head + s_mock.queue_count) %
                      HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH;
  s_mock.queue[tail] = snapshot;
  ++s_mock.queue_count;
  update_queue_info();
}

void release_connection(hal_status_t status) {
  s_mock.current.connected = false;
  s_mock.current.buttons = 0u;
  s_mock.current.axes_present = 0u;
  s_mock.current.dpad = HAL_GAMEPAD_DPAD_NONE;
  memset(s_mock.current.axes, 0, sizeof(s_mock.current.axes));
  enqueue(s_mock.current);
  s_mock.info.last_status = status;
  s_mock.info.state =
      status == HAL_OK ? HAL_GAMEPAD_STATE_READY : HAL_GAMEPAD_STATE_FAILED;
}

hal_status_t mock_start(void *) {
  if (s_mock.started) {
    return HAL_EBUSY;
  }
  memset(&s_mock, 0, sizeof(s_mock));
  s_mock.started = true;
  s_mock.service_status = HAL_OK;
  s_mock.info.state = HAL_GAMEPAD_STATE_STARTING;
  s_mock.info.last_status = HAL_NONE;
  return HAL_OK;
}

hal_status_t mock_stop(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  s_mock.started = false;
  s_mock.info.state = HAL_GAMEPAD_STATE_UNINITIALIZED;
  s_mock.info.last_status = HAL_OK;
  s_mock.info.pairing_window_open = false;
  s_mock.info.pairing_pending = false;
  s_mock.disconnect_pending = false;
  return HAL_OK;
}

hal_status_t mock_service(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (s_mock.service_status != HAL_OK) {
    if (s_mock.current.connected) {
      release_connection(s_mock.service_status);
    } else {
      s_mock.info.state = HAL_GAMEPAD_STATE_FAILED;
      s_mock.info.last_status = s_mock.service_status;
    }
    return s_mock.service_status;
  }
  if (s_mock.disconnect_pending) {
    s_mock.disconnect_pending = false;
    release_connection(HAL_OK);
  }
  return s_mock.overflow_pending ? HAL_EOVERFLOW : HAL_OK;
}

hal_status_t mock_get_info(void *, hal_gamepad_info_t *out_info) {
  if (!s_mock.started || out_info == nullptr) {
    return out_info == nullptr ? HAL_EINVAL : HAL_EUNINIT;
  }
  update_queue_info();
  *out_info = s_mock.info;
  return HAL_OK;
}

hal_status_t mock_snapshot(void *, hal_gamepad_snapshot_t *out_snapshot) {
  if (!s_mock.started || out_snapshot == nullptr) {
    return out_snapshot == nullptr ? HAL_EINVAL : HAL_EUNINIT;
  }
  if (s_mock.info.state == HAL_GAMEPAD_STATE_STARTING) {
    return HAL_EAGAIN;
  }
  *out_snapshot = s_mock.current;
  return HAL_OK;
}

hal_status_t mock_snapshot_next(void *, hal_gamepad_snapshot_t *out_snapshot) {
  if (!s_mock.started || out_snapshot == nullptr) {
    return out_snapshot == nullptr ? HAL_EINVAL : HAL_EUNINIT;
  }
  if (s_mock.overflow_pending) {
    s_mock.overflow_pending = false;
    return HAL_EOVERFLOW;
  }
  if (s_mock.queue_count == 0u) {
    return HAL_EAGAIN;
  }
  *out_snapshot = s_mock.queue[s_mock.queue_head];
  s_mock.queue_head =
      (s_mock.queue_head + 1u) % HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH;
  --s_mock.queue_count;
  update_queue_info();
  return HAL_OK;
}

hal_status_t mock_pairing_open(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (s_mock.info.state != HAL_GAMEPAD_STATE_READY ||
      s_mock.current.connected) {
    return HAL_ESTATE;
  }
  s_mock.info.state = HAL_GAMEPAD_STATE_DISCOVERING;
  s_mock.info.pairing_window_open = true;
  return HAL_OK;
}

hal_status_t mock_pairing_authorize(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (!s_mock.info.pairing_pending) {
    return HAL_ESTATE;
  }
  s_mock.info.pairing_pending = false;
  return HAL_OK;
}

hal_status_t mock_reconnect(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (!s_mock.info.known_device || s_mock.current.connected ||
      s_mock.info.state != HAL_GAMEPAD_STATE_READY) {
    return HAL_ESTATE;
  }
  s_mock.info.state = HAL_GAMEPAD_STATE_CONNECTING;
  return HAL_OK;
}

hal_status_t mock_disconnect(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (!s_mock.current.connected || s_mock.disconnect_pending) {
    return HAL_ESTATE;
  }
  s_mock.disconnect_pending = true;
  return HAL_OK;
}

const jh_gamepad_backend_t s_backend = {
    .context = nullptr,
    .start = mock_start,
    .stop = mock_stop,
    .service = mock_service,
    .get_info = mock_get_info,
    .snapshot = mock_snapshot,
    .snapshot_next = mock_snapshot_next,
    .pairing_open = mock_pairing_open,
    .pairing_authorize = mock_pairing_authorize,
    .reconnect = mock_reconnect,
    .disconnect = mock_disconnect,
};

} // namespace

const jh_gamepad_backend_t *jh_gamepad_backend_instance(void) {
  return &s_backend;
}

void hal_mock_gamepad_reset(void) { memset(&s_mock, 0, sizeof(s_mock)); }

hal_status_t hal_mock_gamepad_inject_ready(bool known_device) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  s_mock.info.state = HAL_GAMEPAD_STATE_READY;
  s_mock.info.last_status = HAL_OK;
  s_mock.info.known_device = known_device;
  s_mock.info.pairing_window_open = false;
  return HAL_OK;
}

hal_status_t hal_mock_gamepad_inject_pairing_request(void) {
  if (!s_mock.started || !s_mock.info.pairing_window_open) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.info.pairing_pending = true;
  return HAL_OK;
}

hal_status_t hal_mock_gamepad_inject_connect(void) {
  if (!s_mock.started || s_mock.current.connected) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  ++s_mock.info.generation;
  if (s_mock.info.generation == 0u) {
    s_mock.info.generation = 1u;
  }
  memset(&s_mock.current, 0, sizeof(s_mock.current));
  s_mock.current.generation = s_mock.info.generation;
  s_mock.current.connected = true;
  s_mock.info.state = HAL_GAMEPAD_STATE_CONNECTED;
  s_mock.info.last_status = HAL_OK;
  s_mock.info.known_device = true;
  s_mock.info.pairing_window_open = false;
  s_mock.info.pairing_pending = false;
  enqueue(s_mock.current);
  return HAL_OK;
}

hal_status_t
hal_mock_gamepad_inject_snapshot(const hal_gamepad_snapshot_t *snapshot) {
  if (!s_mock.started || !s_mock.current.connected || snapshot == nullptr) {
    if (snapshot == nullptr) {
      return HAL_EINVAL;
    }
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.current = *snapshot;
  s_mock.current.generation = s_mock.info.generation;
  s_mock.current.connected = true;
  enqueue(s_mock.current);
  return HAL_OK;
}

hal_status_t hal_mock_gamepad_inject_disconnect(void) {
  if (!s_mock.started || !s_mock.current.connected) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  release_connection(HAL_OK);
  return HAL_OK;
}

hal_status_t hal_mock_gamepad_inject_transport_error(hal_status_t status) {
  if (!s_mock.started || status >= HAL_NONE) {
    return !s_mock.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  s_mock.service_status = status;
  if (s_mock.current.connected) {
    release_connection(status);
  } else {
    s_mock.info.state = HAL_GAMEPAD_STATE_FAILED;
    s_mock.info.last_status = status;
  }
  return HAL_OK;
}

void hal_mock_gamepad_set_service_status(hal_status_t status) {
  s_mock.service_status = status;
}

#endif /* HAL_TARGET_IS_MOCK && HAL_ENABLE_BLUETOOTH_GAMEPAD */
