#include "../../hal_target.h"

#if HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_BLE)

#include "../../hal_ble.h"
#include "../shared/bluetooth/jh_ble_backend.h"
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
  uint8_t protocol_version;
  uint16_t capabilities;
  bool published;
  bool subscribed;
  uint8_t last_frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t last_frame_length;
  size_t notify_count;
#endif
};

mock_ble_t s_mock{};
std::atomic<bool> s_block_advertising_start{false};
std::atomic<bool> s_advertising_start_entered{false};

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
  s_advertising_start_entered.store(true, std::memory_order_release);
  while (s_block_advertising_start.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
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
  memcpy(s_mock.last_frame, frame, length);
  s_mock.last_frame_length = length;
  ++s_mock.notify_count;
  return HAL_OK;
}

hal_status_t mock_stream_publish(void *, uint8_t protocol_version,
                                 uint16_t capabilities) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  s_mock.protocol_version = protocol_version;
  s_mock.capabilities = capabilities;
  s_mock.published = true;
  return HAL_OK;
}

hal_status_t mock_stream_unpublish(void *) {
  s_mock.published = false;
  s_mock.subscribed = false;
  s_mock.protocol_version = 0u;
  s_mock.capabilities = 0u;
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
#endif
  s_block_advertising_start.store(false, std::memory_order_release);
  s_advertising_start_entered.store(false, std::memory_order_release);
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
  ++s_mock.connection;
  if (s_mock.connection == 0u) {
    ++s_mock.connection;
  }
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
  s_block_advertising_start.store(blocked, std::memory_order_release);
  if (blocked) {
    s_advertising_start_entered.store(false, std::memory_order_release);
  }
}

bool hal_mock_ble_advertising_start_entered(void) {
  return s_advertising_start_entered.load(std::memory_order_acquire);
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
  emit(event);
  return HAL_OK;
}

void hal_mock_ble_set_stream_notify_status(hal_status_t status) {
  s_mock.notify_status = status;
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
