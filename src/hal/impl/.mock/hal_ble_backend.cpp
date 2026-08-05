#include "../../hal_target.h"

#if HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_BLE)

#include "../../hal_ble.h"
#include "../shared/bluetooth/jh_ble_backend.h"
#include "hal_mock.h"

#include <string.h>

namespace {

struct mock_ble_t {
  jh_ble_backend_event_fn event_handler;
  void *event_context;
  hal_ble_advertising_config_t advertising;
  hal_ble_scan_config_t scan;
  hal_status_t service_status;
  uint16_t connection;
  bool started;
  bool ready;
  bool advertising_requested;
  bool advertising_enabled;
  bool advertising_pending;
  bool scan_requested;
  bool scan_enabled;
  bool scan_pending;
  bool disconnect_pending;
};

mock_ble_t s_mock{};

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
