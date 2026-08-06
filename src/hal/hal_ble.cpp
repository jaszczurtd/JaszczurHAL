#include "hal_ble.h"

#ifdef HAL_ENABLE_BLE

#include "hal_sync.h"
#include "impl/shared/bluetooth/jh_ble_backend.h"
#include "impl/shared/bluetooth/jh_ble_runtime.h"
#ifdef HAL_ENABLE_BLE_STREAM
#include "impl/shared/bluetooth/jh_ble_stream_runtime.h"
#endif
#include "impl/shared/hal_mutex_once.h"
#include "impl/shared/jh_board_runtime.h"

#include <stdio.h>
#include <string.h>

namespace {

struct ble_runtime_t {
  hal_mutex_t mutex;
  const jh_ble_backend_t *backend;
  hal_ble_state_t state;
  hal_status_t last_status;
  hal_ble_address_t local_address;
  hal_ble_address_t peer_address;
  hal_ble_connection_handle_t connection;
  hal_ble_advertising_handle_t advertising;
  uint16_t native_connection;
  uint16_t mtu;
  uint32_t generation;
  uint32_t next_handle;
  uint32_t dropped_events;
  uint32_t dropped_scan_reports;
  hal_ble_event_t events[HAL_BLE_EVENT_QUEUE_DEPTH];
  size_t event_head;
  size_t event_count;
  hal_ble_advertising_report_t scan_reports[HAL_BLE_SCAN_REPORT_QUEUE_DEPTH];
  size_t scan_report_head;
  size_t scan_report_count;
  hal_ble_event_callback_t callback;
  void *callback_context;
  bool initialized;
  bool advertising_requested;
  bool scan_requested;
  bool operation_active;
  bool poll_active;
  bool dispatch_active;
  bool overflow_pending;
  bool scan_report_overflow_pending;
};

ble_runtime_t s_ble{};

hal_mutex_t runtime_mutex(void) {
  return jh_hal_mutex_create_once(&s_ble.mutex);
}

uint32_t next_nonzero(uint32_t value) {
  ++value;
  return value == HAL_BLE_INVALID_HANDLE ? 1u : value;
}

hal_ble_address_type_t address_type_from_backend(hal_ble_address_type_t type) {
  switch (type) {
  case HAL_BLE_ADDRESS_PUBLIC:
  case HAL_BLE_ADDRESS_RANDOM:
  case HAL_BLE_ADDRESS_PUBLIC_IDENTITY:
  case HAL_BLE_ADDRESS_RANDOM_IDENTITY:
    return type;
  default:
    return HAL_BLE_ADDRESS_UNKNOWN;
  }
}

void reset_queue_locked(void) {
  s_ble.event_head = 0u;
  s_ble.event_count = 0u;
  s_ble.overflow_pending = false;
  memset(s_ble.events, 0, sizeof(s_ble.events));
}

void reset_scan_queue_locked(void) {
  s_ble.scan_report_head = 0u;
  s_ble.scan_report_count = 0u;
  s_ble.scan_report_overflow_pending = false;
  memset(s_ble.scan_reports, 0, sizeof(s_ble.scan_reports));
}

void queue_event_locked(const hal_ble_event_t &event) {
  if (s_ble.event_count == HAL_BLE_EVENT_QUEUE_DEPTH) {
    ++s_ble.dropped_events;
    s_ble.overflow_pending = true;
    return;
  }
  const size_t tail =
      (s_ble.event_head + s_ble.event_count) % HAL_BLE_EVENT_QUEUE_DEPTH;
  s_ble.events[tail] = event;
  ++s_ble.event_count;
}

void queue_error_locked(hal_status_t status) {
  hal_ble_event_t event{};
  event.type = HAL_BLE_EVENT_ERROR;
  event.status = status;
  event.connection = s_ble.connection;
  event.advertising = s_ble.advertising;
  queue_event_locked(event);
}

void queue_scan_report_locked(const hal_ble_advertising_report_t &report) {
  if (s_ble.scan_report_count == HAL_BLE_SCAN_REPORT_QUEUE_DEPTH) {
    ++s_ble.dropped_scan_reports;
    s_ble.scan_report_overflow_pending = true;
    return;
  }
  const bool notify = s_ble.scan_report_count == 0u;
  const size_t tail = (s_ble.scan_report_head + s_ble.scan_report_count) %
                      HAL_BLE_SCAN_REPORT_QUEUE_DEPTH;
  s_ble.scan_reports[tail] = report;
  ++s_ble.scan_report_count;
  if (notify) {
    hal_ble_event_t event{};
    event.type = HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE;
    event.status = HAL_OK;
    queue_event_locked(event);
  }
}

void backend_event(void *, const jh_ble_backend_event_t *backend_event) {
  if (backend_event == nullptr) {
    return;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  bool publish_available = false;
  bool publish_failed = false;
#ifdef HAL_ENABLE_BLE_STREAM
  bool forward_stream = false;
  bool stream_link_lost = false;
  uint32_t stream_generation = 0u;
#endif
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return;
  }

  hal_ble_event_t event{};
  event.status = backend_event->status;
  event.advertising = s_ble.advertising;
  switch (backend_event->type) {
  case JH_BLE_BACKEND_EVENT_READY:
    s_ble.local_address = backend_event->address;
    s_ble.local_address.type =
        address_type_from_backend(s_ble.local_address.type);
    s_ble.state = HAL_BLE_STATE_READY;
    s_ble.last_status = HAL_OK;
    event.type = HAL_BLE_EVENT_CONTROLLER_READY;
    event.status = HAL_OK;
    queue_event_locked(event);
    publish_available = true;
    break;
  case JH_BLE_BACKEND_EVENT_ADVERTISING_STARTED:
    if (s_ble.advertising_requested &&
        s_ble.advertising != HAL_BLE_INVALID_HANDLE) {
      if (s_ble.connection == HAL_BLE_INVALID_HANDLE) {
        s_ble.state = HAL_BLE_STATE_ADVERTISING;
      }
      event.type = HAL_BLE_EVENT_ADVERTISING_STARTED;
      event.status = HAL_OK;
      queue_event_locked(event);
    }
    break;
  case JH_BLE_BACKEND_EVENT_ADVERTISING_STOPPED:
    event.type = HAL_BLE_EVENT_ADVERTISING_STOPPED;
    event.status = HAL_OK;
    event.advertising = s_ble.advertising;
    queue_event_locked(event);
    if (!s_ble.advertising_requested) {
      s_ble.advertising = HAL_BLE_INVALID_HANDLE;
    }
    if (s_ble.connection == HAL_BLE_INVALID_HANDLE) {
      s_ble.state = HAL_BLE_STATE_READY;
    }
    break;
  case JH_BLE_BACKEND_EVENT_CONNECTED:
    if (s_ble.connection != HAL_BLE_INVALID_HANDLE) {
      queue_error_locked(HAL_EOVERFLOW);
      break;
    }
    s_ble.next_handle = next_nonzero(s_ble.next_handle);
    s_ble.connection = s_ble.next_handle;
    s_ble.native_connection = backend_event->native_connection;
    s_ble.peer_address = backend_event->address;
    s_ble.peer_address.type =
        address_type_from_backend(s_ble.peer_address.type);
    s_ble.mtu = HAL_BLE_DEFAULT_ATT_MTU;
    s_ble.state = HAL_BLE_STATE_CONNECTED;
    event.type = HAL_BLE_EVENT_CONNECTED;
    event.status = HAL_OK;
    event.connection = s_ble.connection;
    event.peer_address = s_ble.peer_address;
    event.mtu = s_ble.mtu;
    queue_event_locked(event);
    break;
  case JH_BLE_BACKEND_EVENT_DISCONNECTED:
    if (s_ble.connection == HAL_BLE_INVALID_HANDLE ||
        s_ble.native_connection != backend_event->native_connection) {
      break;
    }
    event.type = HAL_BLE_EVENT_DISCONNECTED;
    event.status = HAL_OK;
    event.connection = s_ble.connection;
    event.peer_address = s_ble.peer_address;
    event.mtu = s_ble.mtu;
    event.disconnect_reason = backend_event->disconnect_reason;
    queue_event_locked(event);
    s_ble.connection = HAL_BLE_INVALID_HANDLE;
    s_ble.native_connection = 0u;
    s_ble.mtu = 0u;
    memset(&s_ble.peer_address, 0, sizeof(s_ble.peer_address));
    s_ble.state = HAL_BLE_STATE_READY;
    break;
  case JH_BLE_BACKEND_EVENT_MTU_UPDATED:
    if (s_ble.connection == HAL_BLE_INVALID_HANDLE ||
        s_ble.native_connection != backend_event->native_connection ||
        backend_event->mtu < HAL_BLE_DEFAULT_ATT_MTU) {
      break;
    }
    s_ble.mtu = backend_event->mtu;
    event.type = HAL_BLE_EVENT_MTU_UPDATED;
    event.status = HAL_OK;
    event.connection = s_ble.connection;
    event.mtu = s_ble.mtu;
    queue_event_locked(event);
#ifdef HAL_ENABLE_BLE_STREAM
    forward_stream = true;
#endif
    break;
  case JH_BLE_BACKEND_EVENT_SCAN_STARTED:
    if (s_ble.scan_requested) {
      s_ble.state = HAL_BLE_STATE_SCANNING;
      event.type = HAL_BLE_EVENT_SCAN_STARTED;
      event.status = HAL_OK;
      queue_event_locked(event);
    }
    break;
  case JH_BLE_BACKEND_EVENT_SCAN_STOPPED:
    if (s_ble.connection == HAL_BLE_INVALID_HANDLE &&
        !s_ble.advertising_requested) {
      s_ble.state = HAL_BLE_STATE_READY;
    }
    event.type = HAL_BLE_EVENT_SCAN_STOPPED;
    event.status = HAL_OK;
    queue_event_locked(event);
    break;
  case JH_BLE_BACKEND_EVENT_ADVERTISING_REPORT:
    if (s_ble.scan_requested) {
      queue_scan_report_locked(backend_event->advertising_report);
    }
    break;
  case JH_BLE_BACKEND_EVENT_ERROR:
    s_ble.last_status = backend_event->status;
    queue_error_locked(backend_event->status);
    if (backend_event->fatal) {
      s_ble.state = HAL_BLE_STATE_FAILED;
      s_ble.connection = HAL_BLE_INVALID_HANDLE;
      s_ble.advertising = HAL_BLE_INVALID_HANDLE;
      s_ble.advertising_requested = false;
      s_ble.scan_requested = false;
      s_ble.generation = next_nonzero(s_ble.generation);
      publish_failed = true;
#ifdef HAL_ENABLE_BLE_STREAM
      stream_link_lost = true;
      stream_generation = s_ble.generation;
#endif
    }
    break;
#ifdef HAL_ENABLE_BLE_STREAM
  case JH_BLE_BACKEND_EVENT_STREAM_WRITE:
  case JH_BLE_BACKEND_EVENT_STREAM_SUBSCRIPTION:
  case JH_BLE_BACKEND_EVENT_STREAM_CAN_SEND:
    forward_stream =
        s_ble.connection != HAL_BLE_INVALID_HANDLE &&
        s_ble.native_connection == backend_event->native_connection;
    break;
#endif
  }
#ifdef HAL_ENABLE_BLE_STREAM
  if (backend_event->type == JH_BLE_BACKEND_EVENT_DISCONNECTED) {
    stream_link_lost = true;
    stream_generation = s_ble.generation;
  }
#endif
  hal_mutex_unlock(mutex);

#ifdef HAL_ENABLE_BLE_STREAM
  /* Stream handling runs outside the radio lock. */
  if (stream_link_lost) {
    jh_ble_stream_on_link_lost(stream_generation);
  } else if (forward_stream) {
    jh_ble_stream_on_backend_event(backend_event);
  }
#endif

  if (publish_available) {
    (void)jh_board_runtime_set_available(HAL_BOARD_CAP_BLUETOOTH_CONTROLLER);
  } else if (publish_failed) {
    (void)jh_board_runtime_set_failed(HAL_BOARD_CAP_BLUETOOTH_CONTROLLER);
  }
}

hal_status_t pop_event(hal_ble_event_t *out_event) {
  if (out_event == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_ble.event_count == 0u) {
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  *out_event = s_ble.events[s_ble.event_head];
  s_ble.event_head = (s_ble.event_head + 1u) % HAL_BLE_EVENT_QUEUE_DEPTH;
  --s_ble.event_count;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t dispatch_callbacks(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_ble.callback == nullptr || s_ble.dispatch_active) {
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }
  s_ble.dispatch_active = true;
  hal_mutex_unlock(mutex);

  for (;;) {
    hal_ble_event_callback_t callback = nullptr;
    void *callback_context = nullptr;
    hal_ble_event_t event{};
    hal_mutex_lock(mutex);
    if (s_ble.callback != nullptr && s_ble.event_count != 0u) {
      callback = s_ble.callback;
      callback_context = s_ble.callback_context;
      event = s_ble.events[s_ble.event_head];
      s_ble.event_head = (s_ble.event_head + 1u) % HAL_BLE_EVENT_QUEUE_DEPTH;
      --s_ble.event_count;
    }
    hal_mutex_unlock(mutex);
    if (callback == nullptr) {
      break;
    }
    callback(&event, callback_context);
  }

  hal_mutex_lock(mutex);
  s_ble.dispatch_active = false;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

bool advertising_config_valid(const hal_ble_advertising_config_t *config) {
  return config != nullptr && config->data_length != 0u &&
         config->data_length <= HAL_BLE_LEGACY_ADV_MAX_DATA_LEN &&
         config->interval_min >= HAL_BLE_ADVERTISING_INTERVAL_MIN &&
         config->interval_min <= HAL_BLE_ADVERTISING_INTERVAL_MAX &&
         config->interval_max >= config->interval_min &&
         config->interval_max <= HAL_BLE_ADVERTISING_INTERVAL_MAX;
}

bool scan_config_valid(const hal_ble_scan_config_t *config) {
  return config != nullptr && config->interval >= HAL_BLE_SCAN_INTERVAL_MIN &&
         config->interval <= HAL_BLE_SCAN_INTERVAL_MAX &&
         config->window >= HAL_BLE_SCAN_INTERVAL_MIN &&
         config->window <= config->interval;
}

} // namespace

hal_status_t hal_ble_initialize(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  const hal_status_t hardware_status = jh_ble_require_hardware();
  if (hardware_status != HAL_OK) {
    return hardware_status;
  }
  const jh_ble_backend_t *backend = jh_ble_backend_instance();
  if (backend == nullptr || backend->start == nullptr ||
      backend->stop == nullptr || backend->service == nullptr ||
      backend->advertising_start == nullptr ||
      backend->advertising_stop == nullptr || backend->disconnect == nullptr ||
      backend->scan_start == nullptr || backend->scan_stop == nullptr) {
    return HAL_ECONFIG;
  }

  hal_mutex_lock(mutex);
  if (s_ble.initialized) {
    const hal_status_t status = s_ble.operation_active ? HAL_EBUSY
                                : s_ble.state == HAL_BLE_STATE_FAILED
                                    ? HAL_ESTATE
                                    : HAL_OK;
    hal_mutex_unlock(mutex);
    return status;
  }
  s_ble.backend = backend;
  s_ble.initialized = true;
  s_ble.state = HAL_BLE_STATE_STARTING;
  s_ble.last_status = HAL_NONE;
  s_ble.generation = next_nonzero(s_ble.generation);
  s_ble.connection = HAL_BLE_INVALID_HANDLE;
  s_ble.advertising = HAL_BLE_INVALID_HANDLE;
  s_ble.native_connection = 0u;
  s_ble.mtu = 0u;
  s_ble.dropped_events = 0u;
  s_ble.dropped_scan_reports = 0u;
  s_ble.advertising_requested = false;
  s_ble.scan_requested = false;
  s_ble.operation_active = true;
  s_ble.poll_active = false;
  s_ble.dispatch_active = false;
  memset(&s_ble.local_address, 0, sizeof(s_ble.local_address));
  memset(&s_ble.peer_address, 0, sizeof(s_ble.peer_address));
  reset_queue_locked();
  reset_scan_queue_locked();
  hal_mutex_unlock(mutex);

  const hal_status_t status =
      backend->start(backend->context, backend_event, nullptr);
  hal_mutex_lock(mutex);
  s_ble.operation_active = false;
  if (status == HAL_OK) {
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }
  s_ble.initialized = false;
  s_ble.backend = nullptr;
  s_ble.state = HAL_BLE_STATE_UNINITIALIZED;
  s_ble.last_status = status;
  hal_mutex_unlock(mutex);
  if (status == HAL_EHW || status == HAL_EIO) {
    (void)jh_board_runtime_set_failed(HAL_BOARD_CAP_BLUETOOTH_CONTROLLER);
  }
  return status;
}

hal_status_t hal_ble_deinitialize(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }
  if (s_ble.operation_active || s_ble.poll_active || s_ble.dispatch_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  const jh_ble_backend_t *backend = s_ble.backend;
  s_ble.operation_active = true;
  hal_mutex_unlock(mutex);

  const hal_status_t status = backend->stop(backend->context);
  hal_mutex_lock(mutex);
  s_ble.operation_active = false;
  s_ble.initialized = false;
  s_ble.backend = nullptr;
  s_ble.state = HAL_BLE_STATE_UNINITIALIZED;
  s_ble.last_status = status;
  s_ble.generation = next_nonzero(s_ble.generation);
  s_ble.connection = HAL_BLE_INVALID_HANDLE;
  s_ble.advertising = HAL_BLE_INVALID_HANDLE;
  s_ble.native_connection = 0u;
  s_ble.mtu = 0u;
  s_ble.advertising_requested = false;
  s_ble.scan_requested = false;
  s_ble.callback = nullptr;
  s_ble.callback_context = nullptr;
  reset_queue_locked();
  reset_scan_queue_locked();
  hal_mutex_unlock(mutex);
  (void)(status == HAL_OK
             ? jh_board_runtime_set_inactive(HAL_BOARD_CAP_BLUETOOTH_CONTROLLER)
             : jh_board_runtime_set_failed(HAL_BOARD_CAP_BLUETOOTH_CONTROLLER));
  return status;
}

hal_status_t hal_ble_poll(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_ble.operation_active || s_ble.poll_active || s_ble.dispatch_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (s_ble.state == HAL_BLE_STATE_FAILED) {
    const hal_status_t status = s_ble.last_status;
    hal_mutex_unlock(mutex);
    (void)dispatch_callbacks();
    return status < HAL_NONE ? status : HAL_EHW;
  }
  s_ble.poll_active = true;
  const jh_ble_backend_t *backend = s_ble.backend;
  hal_mutex_unlock(mutex);

  hal_status_t status = backend->service(backend->context);
  hal_mutex_lock(mutex);
  s_ble.poll_active = false;
  if (status != HAL_OK) {
    s_ble.last_status = status;
    queue_error_locked(status);
    if (status == HAL_EHW || status == HAL_EIO) {
      s_ble.state = HAL_BLE_STATE_FAILED;
    }
  }
  const bool overflow =
      s_ble.overflow_pending || s_ble.scan_report_overflow_pending;
  s_ble.overflow_pending = false;
  hal_mutex_unlock(mutex);

  const hal_status_t dispatch_status = dispatch_callbacks();
#ifdef HAL_ENABLE_BLE_STREAM
  /* Outside every lock, so the stream keeps its own serialization. */
  jh_ble_stream_on_poll();
#endif
  if (status == HAL_EHW || status == HAL_EIO) {
    (void)jh_board_runtime_set_failed(HAL_BOARD_CAP_BLUETOOTH_CONTROLLER);
  }
  if (status != HAL_OK) {
    return status;
  }
  if (dispatch_status != HAL_OK) {
    return dispatch_status;
  }
  return overflow ? HAL_EOVERFLOW : HAL_OK;
}

hal_status_t hal_ble_get_info(hal_ble_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  out_info->state = s_ble.state;
  out_info->last_status = s_ble.last_status;
  out_info->local_address = s_ble.local_address;
  out_info->connection = s_ble.connection;
  out_info->advertising = s_ble.advertising;
  out_info->mtu = s_ble.mtu;
  out_info->generation = s_ble.generation;
  out_info->dropped_events = s_ble.dropped_events;
  out_info->dropped_scan_reports = s_ble.dropped_scan_reports;
  out_info->pending_scan_reports = s_ble.scan_report_count;
  out_info->advertising_requested = s_ble.advertising_requested;
  out_info->scan_requested = s_ble.scan_requested;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_ble_get_local_address(hal_ble_address_t *out_address) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_ble.state == HAL_BLE_STATE_STARTING) {
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  if (s_ble.state == HAL_BLE_STATE_FAILED) {
    hal_mutex_unlock(mutex);
    return HAL_EHW;
  }
  *out_address = s_ble.local_address;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_ble_format_address(const hal_ble_address_t *address, char *out,
                                    size_t out_size) {
  if (address == nullptr || out == nullptr ||
      out_size < HAL_BLE_ADDRESS_TEXT_SIZE) {
    return HAL_EINVAL;
  }
  const int written =
      snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X",
               (unsigned)address->bytes[0], (unsigned)address->bytes[1],
               (unsigned)address->bytes[2], (unsigned)address->bytes[3],
               (unsigned)address->bytes[4], (unsigned)address->bytes[5]);
  return written == (int)(HAL_BLE_ADDRESS_TEXT_SIZE - 1u) ? HAL_OK
                                                          : HAL_EOVERFLOW;
}

hal_status_t
hal_ble_advertising_start(const hal_ble_advertising_config_t *config,
                          hal_ble_advertising_handle_t *out_handle) {
  if (!advertising_config_valid(config) || out_handle == nullptr) {
    return HAL_EINVAL;
  }
  *out_handle = HAL_BLE_INVALID_HANDLE;
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_ble.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (s_ble.state == HAL_BLE_STATE_FAILED) {
    hal_mutex_unlock(mutex);
    return HAL_EHW;
  }
  if (s_ble.advertising_requested || s_ble.scan_requested ||
      s_ble.connection != HAL_BLE_INVALID_HANDLE) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_ble.next_handle = next_nonzero(s_ble.next_handle);
  const hal_ble_advertising_handle_t handle = s_ble.next_handle;
  const jh_ble_backend_t *backend = s_ble.backend;
  s_ble.operation_active = true;
  hal_mutex_unlock(mutex);

  const hal_status_t status =
      backend->advertising_start(backend->context, config);
  hal_mutex_lock(mutex);
  s_ble.operation_active = false;
  if (status != HAL_OK) {
    hal_mutex_unlock(mutex);
    return status;
  }
  s_ble.advertising = handle;
  s_ble.advertising_requested = true;
  hal_mutex_unlock(mutex);
  *out_handle = handle;
  return HAL_OK;
}

hal_status_t
hal_ble_advertising_stop(hal_ble_advertising_handle_t advertising) {
  if (advertising == HAL_BLE_INVALID_HANDLE) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_ble.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (!s_ble.advertising_requested || s_ble.advertising != advertising) {
    hal_mutex_unlock(mutex);
    return HAL_ENOENT;
  }
  const jh_ble_backend_t *backend = s_ble.backend;
  s_ble.operation_active = true;
  hal_mutex_unlock(mutex);

  const hal_status_t status = backend->advertising_stop(backend->context);
  hal_mutex_lock(mutex);
  s_ble.operation_active = false;
  if (status == HAL_OK) {
    s_ble.advertising_requested = false;
  }
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_ble_disconnect(hal_ble_connection_handle_t connection) {
  if (connection == HAL_BLE_INVALID_HANDLE) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_ble.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (s_ble.connection != connection) {
    hal_mutex_unlock(mutex);
    return HAL_ENOENT;
  }
  const uint16_t native_connection = s_ble.native_connection;
  const jh_ble_backend_t *backend = s_ble.backend;
  s_ble.operation_active = true;
  hal_mutex_unlock(mutex);
  const hal_status_t status =
      backend->disconnect(backend->context, native_connection);
  hal_mutex_lock(mutex);
  s_ble.operation_active = false;
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_ble_get_mtu(hal_ble_connection_handle_t connection,
                             uint16_t *out_mtu) {
  if (connection == HAL_BLE_INVALID_HANDLE || out_mtu == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_ble.connection != connection) {
    hal_mutex_unlock(mutex);
    return HAL_ENOENT;
  }
  *out_mtu = s_ble.mtu;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_ble_scan_start(const hal_ble_scan_config_t *config) {
  if (!scan_config_valid(config)) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_ble.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (s_ble.state == HAL_BLE_STATE_FAILED) {
    hal_mutex_unlock(mutex);
    return HAL_EHW;
  }
  if (s_ble.scan_requested || s_ble.advertising_requested ||
      s_ble.connection != HAL_BLE_INVALID_HANDLE) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  const jh_ble_backend_t *backend = s_ble.backend;
  s_ble.operation_active = true;
  hal_mutex_unlock(mutex);

  const hal_status_t status = backend->scan_start(backend->context, config);
  hal_mutex_lock(mutex);
  s_ble.operation_active = false;
  if (status == HAL_OK) {
    s_ble.scan_requested = true;
    reset_scan_queue_locked();
  }
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_ble_scan_stop(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_ble.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (!s_ble.scan_requested) {
    hal_mutex_unlock(mutex);
    return HAL_ENOENT;
  }
  const jh_ble_backend_t *backend = s_ble.backend;
  s_ble.operation_active = true;
  hal_mutex_unlock(mutex);

  const hal_status_t status = backend->scan_stop(backend->context);
  hal_mutex_lock(mutex);
  s_ble.operation_active = false;
  if (status == HAL_OK) {
    s_ble.scan_requested = false;
  }
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t
hal_ble_scan_report_next(hal_ble_advertising_report_t *out_report) {
  if (out_report == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_ble.scan_report_overflow_pending) {
    s_ble.scan_report_overflow_pending = false;
    hal_mutex_unlock(mutex);
    return HAL_EOVERFLOW;
  }
  if (s_ble.scan_report_count == 0u) {
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  *out_report = s_ble.scan_reports[s_ble.scan_report_head];
  s_ble.scan_report_head =
      (s_ble.scan_report_head + 1u) % HAL_BLE_SCAN_REPORT_QUEUE_DEPTH;
  --s_ble.scan_report_count;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t
hal_ble_advertising_field_next(const hal_ble_advertising_report_t *report,
                               size_t *offset,
                               hal_ble_advertising_field_t *out_field) {
  if (report == nullptr || offset == nullptr || out_field == nullptr ||
      report->data_length > HAL_BLE_LEGACY_ADV_MAX_DATA_LEN ||
      *offset > report->data_length) {
    return HAL_EINVAL;
  }
  if (*offset == report->data_length) {
    return HAL_EAGAIN;
  }

  const size_t field_start = *offset;
  const uint8_t field_length = report->data[field_start];
  if (field_length == 0u) {
    *offset = report->data_length;
    return HAL_EAGAIN;
  }
  const size_t encoded_size = (size_t)field_length + 1u;
  if (encoded_size > report->data_length - field_start) {
    return HAL_EIO;
  }
  out_field->type = report->data[field_start + 1u];
  out_field->data_length = field_length - 1u;
  out_field->data = &report->data[field_start + 2u];
  *offset += encoded_size;
  return HAL_OK;
}

hal_status_t hal_ble_event_next(hal_ble_event_t *out_event) {
  return pop_event(out_event);
}

hal_status_t hal_ble_set_event_callback(hal_ble_event_callback_t callback,
                                        void *context) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_ble.dispatch_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_ble.callback = callback;
  s_ble.callback_context = callback == nullptr ? nullptr : context;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

#endif /* HAL_ENABLE_BLE */
