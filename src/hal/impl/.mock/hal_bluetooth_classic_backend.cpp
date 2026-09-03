#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_BLUETOOTH_CLASSIC)

#include "hal/bluetooth/jh_bluetooth_classic_backend.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

namespace {

struct mock_classic_t {
  jh_bluetooth_classic_backend_event_fn event_handler;
  void *event_context;
  hal_bluetooth_classic_address_t hid_address;
  hal_status_t service_status;
  bool started;
  bool scanning;
  bool pairing_pending;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  bool hid_connected;
#endif
};

mock_classic_t s_mock{};

void emit(const jh_bluetooth_classic_backend_event_t &event) {
  if (s_mock.started && s_mock.event_handler != nullptr) {
    s_mock.event_handler(s_mock.event_context, &event);
  }
}

hal_status_t mock_start(void *,
                        jh_bluetooth_classic_backend_event_fn event_handler,
                        void *event_context) {
  if (s_mock.started) {
    return HAL_EBUSY;
  }
  memset(&s_mock, 0, sizeof(s_mock));
  s_mock.started = true;
  s_mock.service_status = HAL_OK;
  s_mock.event_handler = event_handler;
  s_mock.event_context = event_context;
  return HAL_OK;
}

hal_status_t mock_stop(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  s_mock.started = false;
  s_mock.scanning = false;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  s_mock.hid_connected = false;
#endif
  return HAL_OK;
}

hal_status_t mock_service(void *) {
  return s_mock.started ? s_mock.service_status : HAL_EUNINIT;
}

hal_status_t mock_scan_start(void *, uint32_t) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (s_mock.scanning) {
    return HAL_ESTATE;
  }
  s_mock.scanning = true;
  return HAL_OK;
}

hal_status_t mock_scan_stop(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (!s_mock.scanning) {
    return HAL_ESTATE;
  }
  s_mock.scanning = false;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_SCAN_STOPPED;
  event.status = HAL_OK;
  emit(event);
  return HAL_OK;
}

hal_status_t mock_sdp_query(void *,
                            const hal_bluetooth_classic_address_t *address) {
  return !s_mock.started ? HAL_EUNINIT
                         : (address == nullptr ? HAL_EINVAL : HAL_OK);
}

hal_status_t mock_pair(void *, const hal_bluetooth_classic_address_t *address) {
  return !s_mock.started ? HAL_EUNINIT
                         : (address == nullptr ? HAL_EINVAL : HAL_OK);
}

hal_status_t mock_pairing_reply(void *, bool accept) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (!s_mock.pairing_pending) {
    return HAL_ESTATE;
  }
  s_mock.pairing_pending = false;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_AUTHENTICATION;
  event.status = accept ? HAL_OK : HAL_EAUTH;
  emit(event);
  return HAL_OK;
}

hal_status_t mock_peer_restore(void *,
                               const hal_bluetooth_classic_address_t *address,
                               const uint8_t link_key[16], uint8_t) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  return address == nullptr || link_key == nullptr ? HAL_EINVAL : HAL_OK;
}

hal_status_t mock_peer_forget(void *,
                              const hal_bluetooth_classic_address_t *address) {
  return !s_mock.started ? HAL_EUNINIT
                         : (address == nullptr ? HAL_EINVAL : HAL_OK);
}

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
hal_status_t mock_hid_connect(void *,
                              const hal_bluetooth_classic_address_t *address) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (address == nullptr || s_mock.hid_connected) {
    return address == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  s_mock.hid_address = *address;
  return HAL_OK;
}

hal_status_t mock_hid_disconnect(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (!s_mock.hid_connected) {
    return HAL_ESTATE;
  }
  s_mock.hid_connected = false;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_DISCONNECTED;
  event.status = HAL_OK;
  event.address = s_mock.hid_address;
  emit(event);
  return HAL_OK;
}

hal_status_t mock_hid_report_send(void *,
                                  const hal_bluetooth_hid_report_t *report) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  return !s_mock.hid_connected || report == nullptr ? HAL_ESTATE : HAL_OK;
}

hal_status_t mock_hid_report_request(void *, hal_bluetooth_hid_report_type_t,
                                     uint8_t) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  return s_mock.hid_connected ? HAL_OK : HAL_ESTATE;
}
#endif

const jh_bluetooth_classic_backend_t s_backend = {
    .context = nullptr,
    .start = mock_start,
    .stop = mock_stop,
    .service = mock_service,
    .scan_start = mock_scan_start,
    .scan_stop = mock_scan_stop,
    .sdp_query = mock_sdp_query,
    .pair = mock_pair,
    .pairing_reply = mock_pairing_reply,
    .peer_restore = mock_peer_restore,
    .peer_forget = mock_peer_forget,
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
    .hid_connect = mock_hid_connect,
    .hid_disconnect = mock_hid_disconnect,
    .hid_report_send = mock_hid_report_send,
    .hid_report_request = mock_hid_report_request,
#endif
};

} // namespace

const jh_bluetooth_classic_backend_t *
jh_bluetooth_classic_backend_instance(void) {
  return &s_backend;
}

void hal_mock_bluetooth_classic_reset(void) {
  memset(&s_mock, 0, sizeof(s_mock));
}

hal_status_t hal_mock_bluetooth_classic_inject_ready(void) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_READY;
  event.status = HAL_OK;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_classic_inject_scan_result(
    const hal_bluetooth_classic_scan_result_t *result) {
  if (result == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || (!s_mock.scanning && !result->services_resolved)) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_SCAN_RESULT;
  event.status = HAL_OK;
  event.scan_result = *result;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_classic_inject_pairing_request(
    const hal_bluetooth_classic_address_t *address,
    hal_bluetooth_classic_pairing_method_t method) {
  if (address == nullptr || method == HAL_BLUETOOTH_CLASSIC_PAIRING_NONE) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || s_mock.pairing_pending) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.pairing_pending = true;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_PAIRING_REQUEST;
  event.status = HAL_OK;
  event.address = *address;
  event.pairing_method = method;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_classic_inject_link_key(
    const hal_bluetooth_classic_address_t *address, const uint8_t link_key[16],
    uint8_t link_key_type) {
  if (address == nullptr || link_key == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_LINK_KEY;
  event.status = HAL_OK;
  event.address = *address;
  memcpy(event.link_key, link_key, sizeof(event.link_key));
  event.link_key_type = link_key_type;
  emit(event);
  return HAL_OK;
}

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
hal_status_t hal_mock_bluetooth_hid_inject_connected(
    const hal_bluetooth_classic_address_t *address) {
  if (address == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || s_mock.hid_connected) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.hid_address = *address;
  s_mock.hid_connected = true;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_CONNECTED;
  event.status = HAL_OK;
  event.address = *address;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_hid_inject_descriptor(const uint8_t *descriptor,
                                                      size_t length) {
  if (descriptor == nullptr || length == 0u ||
      length > HAL_BLUETOOTH_HID_DESCRIPTOR_MAX_LEN) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || !s_mock.hid_connected) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_DESCRIPTOR;
  event.status = HAL_OK;
  event.address = s_mock.hid_address;
  memcpy(event.descriptor, descriptor, length);
  event.descriptor_length = length;
  emit(event);
  return HAL_OK;
}

hal_status_t
hal_mock_bluetooth_hid_inject_report(const hal_bluetooth_hid_report_t *report) {
  if (report == nullptr || report->length > sizeof(report->data)) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || !s_mock.hid_connected) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_REPORT;
  event.status = HAL_OK;
  event.address = s_mock.hid_address;
  event.hid_report = *report;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_hid_inject_disconnected(hal_status_t status) {
  if (!s_mock.started || !s_mock.hid_connected) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.hid_connected = false;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_DISCONNECTED;
  event.status = status;
  event.address = s_mock.hid_address;
  emit(event);
  return HAL_OK;
}
#endif

hal_status_t hal_mock_bluetooth_classic_inject_error(hal_status_t status,
                                                     bool fatal) {
  if (!s_mock.started || status >= HAL_NONE) {
    return !s_mock.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  s_mock.service_status = status;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_ERROR;
  event.status = status;
  event.fatal = fatal;
  emit(event);
  return HAL_OK;
}

#endif /* HAL_TARGET_IS_MOCK && HAL_ENABLE_BLUETOOTH_CLASSIC */
