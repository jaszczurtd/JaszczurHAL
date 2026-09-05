#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_BLUETOOTH_CLASSIC)

#include "hal/bluetooth/jh_bluetooth_classic_backend.h"
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
#include "hal/bluetooth/jh_bluetooth_a2dp_sbc.h"
#endif
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

namespace {

struct mock_classic_t {
  jh_bluetooth_classic_backend_event_fn event_handler;
  void *event_context;
  hal_bluetooth_classic_address_t hid_address;
  hal_status_t service_status;
  hal_status_t peer_restore_status;
  uint32_t peer_restore_calls;
  bool started;
  bool scanning;
  bool pairing_pending;
  bool connectable;
  bool discoverable;
  bool pairing_allowed;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  bool hid_connecting;
  bool hid_connected;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  hal_bluetooth_a2dp_sbc_format_t a2dp_format;
  hal_bluetooth_classic_address_t a2dp_address;
  bool a2dp_attached;
  bool a2dp_streaming;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  hal_bluetooth_classic_address_t avrcp_address;
  uint8_t avrcp_volume;
  bool avrcp_attached;
  bool avrcp_connected;
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
  s_mock.peer_restore_status = HAL_OK;
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
  s_mock.hid_connecting = false;
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
  if (address == nullptr || link_key == nullptr) {
    return HAL_EINVAL;
  }
  ++s_mock.peer_restore_calls;
  return s_mock.peer_restore_status;
}

hal_status_t mock_peer_forget(void *,
                              const hal_bluetooth_classic_address_t *address) {
  return !s_mock.started ? HAL_EUNINIT
                         : (address == nullptr ? HAL_EINVAL : HAL_OK);
}

hal_status_t
mock_identity_set(void *, const hal_bluetooth_classic_identity_t *identity) {
  return !s_mock.started ? HAL_EUNINIT
                         : (identity == nullptr ? HAL_EINVAL : HAL_OK);
}

hal_status_t mock_visibility_set(void *, bool connectable, bool discoverable,
                                 bool pairing_allowed) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  s_mock.connectable = connectable;
  s_mock.discoverable = discoverable;
  s_mock.pairing_allowed = pairing_allowed;
  return HAL_OK;
}

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
hal_status_t mock_hid_connect(void *,
                              const hal_bluetooth_classic_address_t *address) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (address == nullptr || s_mock.hid_connecting || s_mock.hid_connected) {
    return address == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  s_mock.hid_address = *address;
  s_mock.hid_connecting = true;
  return HAL_OK;
}

hal_status_t mock_hid_disconnect(void *) {
  if (!s_mock.started) {
    return HAL_EUNINIT;
  }
  if (!s_mock.hid_connecting && !s_mock.hid_connected) {
    return HAL_ESTATE;
  }
  s_mock.hid_connecting = false;
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

#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
hal_status_t mock_a2dp_attach(void *) {
  if (!s_mock.started || s_mock.a2dp_attached) {
    return !s_mock.started ? HAL_EUNINIT : HAL_EBUSY;
  }
  s_mock.a2dp_attached = true;
  return HAL_OK;
}

hal_status_t mock_a2dp_detach(void *) {
  if (!s_mock.started || !s_mock.a2dp_attached) {
    return HAL_EUNINIT;
  }
  s_mock.a2dp_attached = false;
  s_mock.a2dp_streaming = false;
  return HAL_OK;
}

hal_status_t mock_a2dp_decode(void *, const uint8_t *data, size_t length) {
  if (!s_mock.started || !s_mock.a2dp_attached || !s_mock.a2dp_streaming) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  jh_bluetooth_a2dp_sbc_frame_t frame{};
  hal_status_t status = jh_bluetooth_a2dp_sbc_frame_parse(data, length, &frame);
  if (status != HAL_OK) {
    return status;
  }
  int16_t pcm[128u * 2u]{};
  for (size_t index = 0u; index < frame.pcm_frames; ++index) {
    pcm[index * 2u] = (int16_t)(1000 + (int16_t)index);
    pcm[index * 2u + 1u] = (int16_t)(-500 - (int16_t)index);
  }
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_A2DP_PCM;
  event.status = HAL_OK;
  event.address = s_mock.a2dp_address;
  event.pcm_data = pcm;
  event.pcm_frames = frame.pcm_frames;
  event.pcm_channels = 2u;
  event.pcm_sample_rate_hz = frame.format.sample_rate_hz;
  emit(event);
  return HAL_OK;
}
#endif

#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
hal_status_t mock_avrcp_attach(void *, uint8_t initial_volume) {
  if (!s_mock.started || s_mock.avrcp_attached) {
    return !s_mock.started ? HAL_EUNINIT : HAL_EBUSY;
  }
  s_mock.avrcp_attached = true;
  s_mock.avrcp_volume = initial_volume;
  return HAL_OK;
}

hal_status_t mock_avrcp_detach(void *) {
  if (!s_mock.started || !s_mock.avrcp_attached) {
    return HAL_EUNINIT;
  }
  s_mock.avrcp_attached = false;
  s_mock.avrcp_connected = false;
  return HAL_OK;
}

hal_status_t mock_avrcp_volume_set(void *, uint8_t absolute_volume) {
  if (!s_mock.started || !s_mock.avrcp_attached) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.avrcp_volume = absolute_volume;
  return HAL_OK;
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
    .identity_set = mock_identity_set,
    .visibility_set = mock_visibility_set,
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
    .hid_connect = mock_hid_connect,
    .hid_disconnect = mock_hid_disconnect,
    .hid_report_send = mock_hid_report_send,
    .hid_report_request = mock_hid_report_request,
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
    .a2dp_attach = mock_a2dp_attach,
    .a2dp_detach = mock_a2dp_detach,
    .a2dp_decode = mock_a2dp_decode,
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
    .avrcp_attach = mock_avrcp_attach,
    .avrcp_detach = mock_avrcp_detach,
    .avrcp_volume_set = mock_avrcp_volume_set,
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

void hal_mock_bluetooth_classic_set_peer_restore_status(hal_status_t status) {
  s_mock.peer_restore_status = status;
}

uint32_t hal_mock_bluetooth_classic_peer_restore_calls(void) {
  return s_mock.peer_restore_calls;
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
hal_status_t hal_mock_bluetooth_hid_inject_connecting(
    const hal_bluetooth_classic_address_t *address) {
  if (address == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || s_mock.hid_connecting || s_mock.hid_connected) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.hid_address = *address;
  s_mock.hid_connecting = true;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_CONNECTING;
  event.status = HAL_OK;
  event.address = *address;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_hid_inject_connected(
    const hal_bluetooth_classic_address_t *address) {
  if (address == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || s_mock.hid_connected) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.hid_address = *address;
  s_mock.hid_connecting = false;
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
  if (!s_mock.started || (!s_mock.hid_connecting && !s_mock.hid_connected)) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.hid_connecting = false;
  s_mock.hid_connected = false;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_DISCONNECTED;
  event.status = status;
  event.address = s_mock.hid_address;
  emit(event);
  return HAL_OK;
}
#endif

#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
hal_status_t hal_mock_bluetooth_a2dp_inject_connected(
    const hal_bluetooth_classic_address_t *address) {
  if (address == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || !s_mock.a2dp_attached) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.a2dp_address = *address;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_A2DP_CONNECTED;
  event.status = HAL_OK;
  event.address = *address;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_a2dp_inject_format(
    const hal_bluetooth_a2dp_sbc_format_t *format) {
  if (format == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || !s_mock.a2dp_attached) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.a2dp_format = *format;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_A2DP_FORMAT;
  event.status = HAL_OK;
  event.address = s_mock.a2dp_address;
  event.a2dp_format = *format;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_a2dp_inject_started(void) {
  if (!s_mock.started || !s_mock.a2dp_attached || s_mock.a2dp_streaming) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.a2dp_streaming = true;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_A2DP_STARTED;
  event.status = HAL_OK;
  event.address = s_mock.a2dp_address;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_a2dp_inject_media(const uint8_t *data,
                                                  size_t length) {
  if (data == nullptr || length == 0u) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || !s_mock.a2dp_streaming) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_A2DP_MEDIA;
  event.status = HAL_OK;
  event.address = s_mock.a2dp_address;
  event.media_data = data;
  event.media_length = length;
  emit(event);
  return HAL_OK;
}

static hal_status_t
mock_a2dp_transition(jh_bluetooth_classic_backend_event_type_t type,
                     bool streaming) {
  if (!s_mock.started || !s_mock.a2dp_attached) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.a2dp_streaming = streaming;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = type;
  event.status = HAL_OK;
  event.address = s_mock.a2dp_address;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_a2dp_inject_suspended(void) {
  return mock_a2dp_transition(JH_BLUETOOTH_CLASSIC_EVENT_A2DP_SUSPENDED, false);
}

hal_status_t hal_mock_bluetooth_a2dp_inject_stopped(void) {
  return mock_a2dp_transition(JH_BLUETOOTH_CLASSIC_EVENT_A2DP_STOPPED, false);
}

hal_status_t hal_mock_bluetooth_a2dp_inject_disconnected(hal_status_t status) {
  if (!s_mock.started || !s_mock.a2dp_attached) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.a2dp_streaming = false;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_A2DP_DISCONNECTED;
  event.status = status;
  event.address = s_mock.a2dp_address;
  emit(event);
  return HAL_OK;
}
#endif

#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
hal_status_t hal_mock_bluetooth_avrcp_inject_connected(
    const hal_bluetooth_classic_address_t *address) {
  if (address == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || !s_mock.avrcp_attached) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.avrcp_connected = true;
  s_mock.avrcp_address = *address;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_CONNECTED;
  event.status = HAL_OK;
  event.address = *address;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_avrcp_inject_volume(uint8_t volume) {
  if (volume > 127u) {
    return HAL_EINVAL;
  }
  if (!s_mock.started || !s_mock.avrcp_connected) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.avrcp_volume = volume;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_VOLUME;
  event.status = HAL_OK;
  event.address = s_mock.avrcp_address;
  event.absolute_volume = volume;
  emit(event);
  return HAL_OK;
}

hal_status_t hal_mock_bluetooth_avrcp_inject_disconnected(void) {
  if (!s_mock.started || !s_mock.avrcp_connected) {
    return !s_mock.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  s_mock.avrcp_connected = false;
  jh_bluetooth_classic_backend_event_t event{};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_DISCONNECTED;
  event.status = HAL_OK;
  event.address = s_mock.avrcp_address;
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
