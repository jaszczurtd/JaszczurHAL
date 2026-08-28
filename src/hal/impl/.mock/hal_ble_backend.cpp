#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_BLE)

#include "hal/bluetooth/hal_ble.h"
#include "hal/bluetooth/jh_ble_backend.h"
#include "hal_mock.h"

#include <atomic>
#include <string.h>
#include <thread>

namespace {

struct mock_ble_t {
  jh_ble_backend_event_fn event_handler;
  void *event_context;
  hal_ble_advertising_config_t advertising;
  hal_ble_scan_config_t scan;
  hal_status_t service_status;
  uint16_t connection;
  uint16_t next_connection;
  uint16_t mtu;
  bool started;
  bool ready;
  bool advertising_requested;
  bool advertising_enabled;
  bool advertising_pending;
  bool scan_requested;
  bool scan_enabled;
  bool scan_pending;
  bool disconnect_pending;
#ifdef HAL_ENABLE_BLE_STREAM
  hal_status_t notify_status;
  hal_status_t discard_status;
  hal_status_t publish_status;
  hal_status_t unpublish_status;
  uint8_t protocol_version;
  uint16_t capabilities;
  bool published;
  bool subscribed;
  bool notification_pending;
  bool notification_deferred;
  bool notification_in_progress;
  bool notification_completion_dispatching;
  uint8_t pending_frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t pending_frame_length;
  uint8_t last_frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t last_frame_length;
  size_t notify_count;
#endif
};

mock_ble_t s_mock{};

struct blocking_operation_t {
  std::atomic<bool> blocked{false};
  std::atomic<bool> entered{false};
};

blocking_operation_t s_advertising_start_operation{};
#ifdef HAL_ENABLE_BLE_STREAM
blocking_operation_t s_stream_publish_operation{};
blocking_operation_t s_stream_unpublish_operation{};
#endif

void wait_if_blocked(blocking_operation_t &operation) {
  operation.entered.store(true, std::memory_order_release);
  while (operation.blocked.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

void set_blocked(blocking_operation_t &operation, bool blocked) {
  operation.blocked.store(blocked, std::memory_order_release);
  if (blocked) {
    operation.entered.store(false, std::memory_order_release);
  }
}

void reset_blocking_operation(blocking_operation_t &operation) {
  operation.blocked.store(false, std::memory_order_release);
  operation.entered.store(false, std::memory_order_release);
}

#ifdef HAL_ENABLE_BLE_STREAM
void clear_pending_notification(void) {
  memset(s_mock.pending_frame, 0, sizeof(s_mock.pending_frame));
  s_mock.pending_frame_length = 0u;
  s_mock.notification_pending = false;
  s_mock.notification_in_progress = false;
  s_mock.notification_completion_dispatching = false;
}
#endif

void emit(const jh_ble_backend_event_t &event) {
  if (s_mock.event_handler != nullptr) {
    s_mock.event_handler(s_mock.event_context, &event);
  }
}

hal_status_t mock_start(void *, jh_ble_backend_event_fn event_handler,
                        void *event_context) {
  if (event_handler == nullptr) {
    return HAL_EINVAL;
  }
  if (s_mock.started) {
    return HAL_EBUSY;
  }
  s_mock.event_handler = event_handler;
  s_mock.event_context = event_context;
  s_mock.service_status = HAL_OK;
#ifdef HAL_ENABLE_BLE_STREAM
  s_mock.notify_status = HAL_OK;
  s_mock.discard_status = HAL_OK;
  s_mock.publish_status = HAL_OK;
  s_mock.unpublish_status = HAL_OK;
#endif
  s_mock.started = true;
  return HAL_OK;
}

hal_status_t mock_stop(void *) {
  if (!s_mock.started) {
    return HAL_OK;
  }
  s_mock.started = false;
  s_mock.ready = false;
  s_mock.advertising_requested = false;
  s_mock.advertising_enabled = false;
  s_mock.advertising_pending = false;
  s_mock.scan_requested = false;
  s_mock.scan_enabled = false;
  s_mock.scan_pending = false;
  s_mock.disconnect_pending = false;
  s_mock.connection = 0u;
#ifdef HAL_ENABLE_BLE_STREAM
  s_mock.subscribed = false;
  clear_pending_notification();
#endif
  s_mock.event_handler = nullptr;
  s_mock.event_context = nullptr;
  return HAL_OK;
}

hal_status_t mock_service(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (s_mock.service_status != HAL_OK) {
    return s_mock.service_status;
  }
  if (s_mock.disconnect_pending && s_mock.connection != 0u) {
    s_mock.disconnect_pending = false;
    const uint16_t connection = s_mock.connection;
    s_mock.connection = 0u;
#ifdef HAL_ENABLE_BLE_STREAM
    s_mock.subscribed = false;
    clear_pending_notification();
#endif
    jh_ble_backend_event_t event{};
    event.type = JH_BLE_BACKEND_EVENT_DISCONNECTED;
    event.status = HAL_OK;
    event.native_connection = connection;
    event.disconnect_reason = 0x16u;
    emit(event);
    if (s_mock.advertising_requested) {
      s_mock.advertising_pending = true;
    }
  }
  if (s_mock.advertising_pending && s_mock.ready && s_mock.connection == 0u) {
    s_mock.advertising_pending = false;
    s_mock.advertising_enabled = s_mock.advertising_requested;
    jh_ble_backend_event_t event{};
    event.type = s_mock.advertising_enabled
                     ? JH_BLE_BACKEND_EVENT_ADVERTISING_STARTED
                     : JH_BLE_BACKEND_EVENT_ADVERTISING_STOPPED;
    event.status = HAL_OK;
    emit(event);
  } else if (s_mock.advertising_pending && !s_mock.advertising_requested) {
    s_mock.advertising_pending = false;
    s_mock.advertising_enabled = false;
    jh_ble_backend_event_t event{};
    event.type = JH_BLE_BACKEND_EVENT_ADVERTISING_STOPPED;
    event.status = HAL_OK;
    emit(event);
  }
  if (s_mock.scan_pending && (s_mock.ready || !s_mock.scan_requested)) {
    s_mock.scan_pending = false;
    s_mock.scan_enabled = s_mock.scan_requested;
    jh_ble_backend_event_t event{};
    event.type = s_mock.scan_enabled ? JH_BLE_BACKEND_EVENT_SCAN_STARTED
                                     : JH_BLE_BACKEND_EVENT_SCAN_STOPPED;
    event.status = HAL_OK;
    emit(event);
  }
  return HAL_OK;
}

hal_status_t
mock_advertising_start(void *, const hal_ble_advertising_config_t *config) {
  wait_if_blocked(s_advertising_start_operation);
  if (!s_mock.started || config == nullptr) {
    return s_mock.started ? HAL_EINVAL : HAL_EUNINIT;
  }
  s_mock.advertising = *config;
  s_mock.advertising_requested = true;
  s_mock.advertising_pending = true;
  return HAL_OK;
}

hal_status_t mock_advertising_stop(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  s_mock.advertising_requested = false;
  s_mock.advertising_pending = true;
  return HAL_OK;
}

hal_status_t mock_disconnect(void *, uint16_t native_connection) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (native_connection == 0u || native_connection != s_mock.connection) {
    return HAL_ENOENT;
  }
  s_mock.disconnect_pending = true;
  return HAL_OK;
}

hal_status_t mock_scan_start(void *, const hal_ble_scan_config_t *config) {
  if (!s_mock.started || config == nullptr) {
    return s_mock.started ? HAL_EINVAL : HAL_EUNINIT;
  }
  s_mock.scan = *config;
  s_mock.scan_requested = true;
  s_mock.scan_pending = true;
  return HAL_OK;
}

hal_status_t mock_scan_stop(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  s_mock.scan_requested = false;
  s_mock.scan_pending = true;
  return HAL_OK;
}

#ifdef HAL_ENABLE_BLE_STREAM
hal_status_t mock_stream_notify(void *, uint16_t native_connection,
                                const uint8_t *frame, size_t length) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (frame == nullptr || length == 0u ||
      length > HAL_BLE_STREAM_MAX_FRAME_LEN) {
    return HAL_EINVAL;
  }
  if (native_connection == 0u || native_connection != s_mock.connection) {
    return HAL_ENOENT;
  }
  if (!s_mock.published || !s_mock.subscribed) {
    return HAL_ESTATE;
  }
  if (s_mock.mtu < HAL_BLE_STREAM_ATT_OVERHEAD ||
      length > (size_t)(s_mock.mtu - HAL_BLE_STREAM_ATT_OVERHEAD)) {
    return HAL_EOVERFLOW;
  }
  if (s_mock.notify_status != HAL_OK) {
    return s_mock.notify_status;
  }
  if (s_mock.notification_pending || s_mock.notification_in_progress) {
    return HAL_EAGAIN;
  }
  memcpy(s_mock.pending_frame, frame, length);
  s_mock.pending_frame_length = length;
  s_mock.notification_pending = true;
  if (!s_mock.notification_deferred) {
    memcpy(s_mock.last_frame, frame, length);
    s_mock.last_frame_length = length;
    ++s_mock.notify_count;
  }
  return HAL_OK;
}

hal_status_t mock_stream_discard_pending(void *, uint16_t native_connection) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (native_connection == 0u || native_connection != s_mock.connection) {
    return HAL_ENOENT;
  }
  if (s_mock.discard_status != HAL_OK) {
    return s_mock.discard_status;
  }
  if (s_mock.notification_in_progress ||
      s_mock.notification_completion_dispatching) {
    return HAL_EBUSY;
  }
  clear_pending_notification();
  return HAL_OK;
}

hal_status_t mock_stream_publish(void *, uint8_t protocol_version,
                                 uint16_t capabilities) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (s_mock.publish_status != HAL_OK) {
    return s_mock.publish_status;
  }
  if (s_mock.notification_pending || s_mock.notification_in_progress ||
      s_mock.notification_completion_dispatching) {
    return HAL_EBUSY;
  }
  s_mock.protocol_version = protocol_version;
  s_mock.capabilities = capabilities;
  s_mock.published = true;
  wait_if_blocked(s_stream_publish_operation);
  return HAL_OK;
}

hal_status_t mock_stream_unpublish(void *) {
  wait_if_blocked(s_stream_unpublish_operation);
  if (s_mock.unpublish_status != HAL_OK) {
    return s_mock.unpublish_status;
  }
  s_mock.published = false;
  s_mock.subscribed = false;
  s_mock.protocol_version = 0u;
  s_mock.capabilities = 0u;
  clear_pending_notification();
  memset(s_mock.last_frame, 0, sizeof(s_mock.last_frame));
  s_mock.last_frame_length = 0u;
  return HAL_OK;
}
#endif

const jh_ble_backend_t s_backend = {
    .context = nullptr,
    .start = mock_start,
    .stop = mock_stop,
    .service = mock_service,
    .advertising_start = mock_advertising_start,
    .advertising_stop = mock_advertising_stop,
    .disconnect = mock_disconnect,
    .scan_start = mock_scan_start,
    .scan_stop = mock_scan_stop,
#ifdef HAL_ENABLE_BLE_STREAM
    .stream_notify = mock_stream_notify,
    .stream_discard_pending = mock_stream_discard_pending,
    .stream_publish = mock_stream_publish,
    .stream_unpublish = mock_stream_unpublish,
#endif
};

} // namespace

extern "C" const jh_ble_backend_t *jh_ble_backend_instance(void) {
  return &s_backend;
}

void hal_mock_ble_reset(void) {
  jh_ble_backend_event_fn event_handler = s_mock.event_handler;
  void *event_context = s_mock.event_context;
  memset(&s_mock, 0, sizeof(s_mock));
  s_mock.event_handler = event_handler;
  s_mock.event_context = event_context;
  s_mock.service_status = HAL_OK;
#ifdef HAL_ENABLE_BLE_STREAM
  s_mock.notify_status = HAL_OK;
  s_mock.discard_status = HAL_OK;
  s_mock.publish_status = HAL_OK;
  s_mock.unpublish_status = HAL_OK;
#endif
  reset_blocking_operation(s_advertising_start_operation);
#ifdef HAL_ENABLE_BLE_STREAM
  reset_blocking_operation(s_stream_publish_operation);
  reset_blocking_operation(s_stream_unpublish_operation);
#endif
}

hal_status_t hal_mock_ble_inject_ready(const hal_ble_address_t *address) {
  if (!s_mock.started || address == nullptr) {
    return s_mock.started ? HAL_EINVAL : HAL_EUNINIT;
  }
  s_mock.ready = true;
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_READY;
  event.status = HAL_OK;
  event.address = *address;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_ble_inject_connection(const hal_ble_address_t *peer) {
  if (!s_mock.started || !s_mock.ready || peer == nullptr) {
    return !s_mock.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  if (s_mock.connection != 0u) {
    return HAL_EBUSY;
  }
  ++s_mock.next_connection;
  if (s_mock.next_connection == 0u) {
    ++s_mock.next_connection;
  }
  s_mock.connection = s_mock.next_connection;
  s_mock.advertising_enabled = false;
  s_mock.mtu = HAL_BLE_DEFAULT_ATT_MTU;
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_CONNECTED;
  event.status = HAL_OK;
  event.native_connection = s_mock.connection;
  event.address = *peer;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_ble_inject_disconnect(uint8_t reason) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (s_mock.connection == 0u) {
    return HAL_ENOENT;
  }
  const uint16_t connection = s_mock.connection;
  s_mock.connection = 0u;
  s_mock.mtu = 0u;
#ifdef HAL_ENABLE_BLE_STREAM
  s_mock.subscribed = false;
  clear_pending_notification();
#endif
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_DISCONNECTED;
  event.status = HAL_OK;
  event.native_connection = connection;
  event.disconnect_reason = reason;
  emit(event);
  if (s_mock.advertising_requested) {
    s_mock.advertising_pending = true;
  }
  return HAL_OK;
}

uint16_t hal_mock_ble_native_connection(void) { return s_mock.connection; }

hal_status_t hal_mock_ble_inject_delayed_disconnect(uint16_t native_connection,
                                                    uint8_t reason) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (native_connection == 0u) {
    return HAL_EINVAL;
  }
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_DISCONNECTED;
  event.status = HAL_OK;
  event.native_connection = native_connection;
  event.disconnect_reason = reason;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_ble_inject_advertising_stopped(void) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_ADVERTISING_STOPPED;
  event.status = HAL_OK;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_ble_inject_scan_stopped(void) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_SCAN_STOPPED;
  event.status = HAL_OK;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_ble_inject_mtu(uint16_t mtu) {
  if (!s_mock.started || s_mock.connection == 0u) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ENOENT;
  }
  if (mtu < HAL_BLE_DEFAULT_ATT_MTU) {
    return HAL_EINVAL;
  }
  s_mock.mtu = mtu;
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_MTU_UPDATED;
  event.status = HAL_OK;
  event.native_connection = s_mock.connection;
  event.mtu = mtu;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_ble_inject_failure(hal_status_t status) {
  if (!s_mock.started || status >= HAL_NONE) {
    return !s_mock.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_ERROR;
  event.status = status;
  event.fatal = true;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_ble_inject_advertising_report(
    const hal_ble_advertising_report_t *report) {
  if (!s_mock.started || !s_mock.scan_enabled || report == nullptr) {
    return !s_mock.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  if (report->data_length > HAL_BLE_LEGACY_ADV_MAX_DATA_LEN) {
    return HAL_EINVAL;
  }
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_ADVERTISING_REPORT;
  event.status = HAL_OK;
  event.advertising_report = *report;
  emit(event);
  return HAL_OK;
}

void hal_mock_ble_set_service_status(hal_status_t status) {
  s_mock.service_status = status;
}

void hal_mock_ble_block_advertising_start(bool blocked) {
  set_blocked(s_advertising_start_operation, blocked);
}

bool hal_mock_ble_advertising_start_entered(void) {
  return s_advertising_start_operation.entered.load(std::memory_order_acquire);
}

hal_status_t
hal_mock_ble_get_advertising(hal_ble_advertising_config_t *out_config,
                             bool *out_enabled) {
  if (out_config == nullptr || out_enabled == nullptr) {
    return HAL_EINVAL;
  }
  *out_config = s_mock.advertising;
  *out_enabled = s_mock.advertising_enabled;
  return HAL_OK;
}

#ifdef HAL_ENABLE_BLE_STREAM
hal_status_t hal_mock_ble_inject_stream_subscription(bool subscribed) {
  if (!s_mock.started || s_mock.connection == 0u) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ENOENT;
  }
  if (!s_mock.published) {
    return HAL_ESTATE;
  }
  s_mock.subscribed = subscribed;
  if (!subscribed) {
    clear_pending_notification();
  }
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_STREAM_SUBSCRIPTION;
  event.status = HAL_OK;
  event.native_connection = s_mock.connection;
  event.mtu = s_mock.mtu;
  event.stream_subscribed = subscribed;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_ble_inject_stream_frame(const uint8_t *frame,
                                              size_t length) {
  if (!s_mock.started || s_mock.connection == 0u) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ENOENT;
  }
  if (!s_mock.published) {
    return HAL_ESTATE;
  }
  if (frame == nullptr || length == 0u ||
      length > HAL_BLE_STREAM_MAX_FRAME_LEN) {
    return HAL_EINVAL;
  }
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_STREAM_WRITE;
  event.status = HAL_OK;
  event.native_connection = s_mock.connection;
  event.mtu = s_mock.mtu;
  memcpy(event.stream_frame, frame, length);
  event.stream_frame_length = (uint8_t)length;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_ble_inject_stream_can_send(void) {
  if (!s_mock.started || s_mock.connection == 0u) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ENOENT;
  }
  jh_ble_backend_event_t event{};
  event.type = JH_BLE_BACKEND_EVENT_STREAM_CAN_SEND;
  event.status = HAL_OK;
  event.native_connection = s_mock.connection;
  const bool completing = s_mock.notification_pending;
  if (completing) {
    s_mock.notification_in_progress = true;
    if (s_mock.notification_deferred) {
      memcpy(s_mock.last_frame, s_mock.pending_frame,
             s_mock.pending_frame_length);
      s_mock.last_frame_length = s_mock.pending_frame_length;
      ++s_mock.notify_count;
    }
    memset(s_mock.pending_frame, 0, sizeof(s_mock.pending_frame));
    s_mock.pending_frame_length = 0u;
    s_mock.notification_pending = false;
    s_mock.notification_in_progress = false;
    /* Match the backend window: callbacks may stage the next frame, while a
       concurrent discard remains unsafe until the callback returns. */
    s_mock.notification_completion_dispatching = true;
  }
  emit(event);
  s_mock.notification_completion_dispatching = false;
  return HAL_OK;
}

void hal_mock_ble_set_stream_notify_status(hal_status_t status) {
  s_mock.notify_status = status;
}

void hal_mock_ble_set_stream_discard_status(hal_status_t status) {
  s_mock.discard_status = status;
}

void hal_mock_ble_set_stream_notifications_deferred(bool deferred) {
  s_mock.notification_deferred = deferred;
}

void hal_mock_ble_set_stream_notification_in_progress(bool in_progress) {
  s_mock.notification_in_progress = in_progress;
}

void hal_mock_ble_set_stream_publish_status(hal_status_t status) {
  s_mock.publish_status = status;
}

void hal_mock_ble_set_stream_unpublish_status(hal_status_t status) {
  s_mock.unpublish_status = status;
}

void hal_mock_ble_block_stream_publish(bool blocked) {
  set_blocked(s_stream_publish_operation, blocked);
}

bool hal_mock_ble_stream_publish_entered(void) {
  return s_stream_publish_operation.entered.load(std::memory_order_acquire);
}

void hal_mock_ble_block_stream_unpublish(bool blocked) {
  set_blocked(s_stream_unpublish_operation, blocked);
}

bool hal_mock_ble_stream_unpublish_entered(void) {
  return s_stream_unpublish_operation.entered.load(std::memory_order_acquire);
}

bool hal_mock_ble_stream_notification_pending(void) {
  return s_mock.notification_pending;
}

hal_status_t hal_mock_ble_get_stream_frame(uint8_t *out_frame, size_t capacity,
                                           size_t *out_length) {
  if (out_frame == nullptr || out_length == nullptr) {
    return HAL_EINVAL;
  }
  if (s_mock.last_frame_length == 0u) {
    return HAL_EAGAIN;
  }
  if (capacity < s_mock.last_frame_length) {
    return HAL_EOVERFLOW;
  }
  memcpy(out_frame, s_mock.last_frame, s_mock.last_frame_length);
  *out_length = s_mock.last_frame_length;
  return HAL_OK;
}

size_t hal_mock_ble_stream_notify_count(void) { return s_mock.notify_count; }

hal_status_t hal_mock_ble_get_stream_published(uint8_t *out_version,
                                               uint16_t *out_capabilities) {
  if (out_version == nullptr || out_capabilities == nullptr) {
    return HAL_EINVAL;
  }
  *out_version = s_mock.protocol_version;
  *out_capabilities = s_mock.capabilities;
  return HAL_OK;
}
#endif

hal_status_t hal_mock_ble_get_scan(hal_ble_scan_config_t *out_config,
                                   bool *out_enabled) {
  if (out_config == nullptr || out_enabled == nullptr) {
    return HAL_EINVAL;
  }
  *out_config = s_mock.scan;
  *out_enabled = s_mock.scan_enabled;
  return HAL_OK;
}

#endif
