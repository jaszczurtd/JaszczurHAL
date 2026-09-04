#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"

#if !HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_BLUETOOTH_CLASSIC) &&            \
    defined(JH_BLUETOOTH_BTSTACK)

#include "hal/bluetooth/jh_bluetooth_classic_address.h"
#include "hal/bluetooth/jh_bluetooth_classic_backend.h"

#include "bluetooth_sdp.h"
#include "btstack_event.h"
#include "classic/btstack_link_key_db_memory.h"
#include "classic/sdp_client.h"
#include "classic/sdp_util.h"
#include "gap.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"
#include "hci.h"
#include "jh_btstack_host.h"
#include "l2cap.h"
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
#include "classic/hid_host.h"
#endif

#include <stddef.h>
#include <string.h>

extern void sdp_parser_init_service_search(void);

enum {
  JH_CLASSIC_INQUIRY_DURATION_1280_MS = 8u,
  JH_CLASSIC_SCAN_CACHE_DEPTH = HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH,
  JH_CLASSIC_SDP_SERVICE_COUNT = 5u,
  JH_CLASSIC_HID_REPORT_TRACE_LIMIT = 32u,
  JH_CLASSIC_HID_INCOMING_ACL_TIMEOUT_MS = 10000u,
};

typedef struct {
  hal_bluetooth_classic_scan_result_t result;
  bool used;
} jh_classic_scan_cache_entry_t;

typedef struct {
  jh_bluetooth_classic_backend_event_fn event_handler;
  void *event_context;
  btstack_packet_callback_registration_t hci_events;
  jh_bluetooth_host_reference_t host_reference;
  jh_classic_scan_cache_entry_t scan_cache[JH_CLASSIC_SCAN_CACHE_DEPTH];
  hal_bluetooth_classic_address_t pairing_address;
  hal_bluetooth_classic_pairing_method_t pairing_method;
  hal_bluetooth_classic_address_t sdp_address;
  hal_bluetooth_classic_scan_result_t sdp_result;
  hal_bluetooth_classic_address_t
      restored_peers[HAL_BLUETOOTH_CLASSIC_MAX_PEERS];
  bool restored_peer_used[HAL_BLUETOOTH_CLASSIC_MAX_PEERS];
  uint32_t scan_deadline_ms;
  uint32_t sdp_retry_after_ms;
  uint32_t sdp_services;
  uint8_t sdp_service_index;
  bool started;
  bool controller_ready;
  bool scan_active;
  bool sdp_active;
  bool sdp_match;
  bool sdp_retry_pending;
  bool pairing_pending;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  uint8_t hid_descriptor[HAL_BLUETOOTH_HID_DESCRIPTOR_MAX_LEN];
  hal_bluetooth_classic_address_t hid_address;
  hal_bluetooth_classic_address_t hid_incoming_address;
  uint16_t hid_cid;
  hci_con_handle_t hid_con_handle;
  uint32_t hid_incoming_started_ms;
  hal_bluetooth_hid_report_type_t requested_report_type;
  uint32_t last_hid_report_hash;
  uint16_t last_hid_report_length;
  uint8_t hid_report_trace_count;
  bool hid_connected;
  bool hid_outgoing;
  bool hid_incoming_acl_pending;
  bool hid_acl_disconnect_pending;
  bool last_hid_report_valid;
  bool hid_report_trace_suppressed;
#endif
} jh_classic_btstack_t;

static jh_classic_btstack_t s_backend;

static const uint16_t s_sdp_uuids[JH_CLASSIC_SDP_SERVICE_COUNT] = {
    BLUETOOTH_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE_SERVICE,
    BLUETOOTH_SERVICE_CLASS_PNP_INFORMATION,
    BLUETOOTH_SERVICE_CLASS_SERIAL_PORT,
    BLUETOOTH_SERVICE_CLASS_AUDIO_SOURCE,
    BLUETOOTH_SERVICE_CLASS_AUDIO_SINK,
};

static const uint32_t s_sdp_service_bits[JH_CLASSIC_SDP_SERVICE_COUNT] = {
    HAL_BLUETOOTH_CLASSIC_SERVICE_HID,
    HAL_BLUETOOTH_CLASSIC_SERVICE_PNP,
    HAL_BLUETOOTH_CLASSIC_SERVICE_SERIAL_PORT,
    HAL_BLUETOOTH_CLASSIC_SERVICE_AUDIO_SOURCE,
    HAL_BLUETOOTH_CLASSIC_SERVICE_AUDIO_SINK,
};

static void emit(const jh_bluetooth_classic_backend_event_t *event) {
  if (s_backend.started && s_backend.event_handler != NULL && event != NULL) {
    s_backend.event_handler(s_backend.event_context, event);
  }
}

static void emit_simple(jh_bluetooth_classic_backend_event_type_t type,
                        hal_status_t status) {
  jh_bluetooth_classic_backend_event_t event = {0};
  event.type = type;
  event.status = status;
  emit(&event);
}

static hal_status_t status_from_btstack(int status) {
  return status == ERROR_CODE_SUCCESS ? HAL_OK : HAL_EIO;
}

static hal_status_t hid_connection_status_from_btstack(uint8_t status) {
  switch (status) {
  case ERROR_CODE_SUCCESS:
    return HAL_OK;
  case ERROR_CODE_AUTHENTICATION_FAILURE:
  case ERROR_CODE_PIN_OR_KEY_MISSING:
  case ERROR_CODE_CONNECTION_REJECTED_DUE_TO_SECURITY_REASONS:
  case ERROR_CODE_REPEATED_ATTEMPTS:
  case ERROR_CODE_PAIRING_NOT_ALLOWED:
  case ERROR_CODE_CONNECTION_TERMINATED_DUE_TO_MIC_FAILURE:
    return HAL_EAUTH;
  case ERROR_CODE_PAGE_TIMEOUT:
  case ERROR_CODE_CONNECTION_TIMEOUT:
  case ERROR_CODE_CONNECTION_ACCEPT_TIMEOUT_EXCEEDED:
    return HAL_ETIMEOUT;
  case ERROR_CODE_MEMORY_CAPACITY_EXCEEDED:
  case ERROR_CODE_CONNECTION_LIMIT_EXCEEDED:
  case ERROR_CODE_CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES:
  case ERROR_CODE_ACL_CONNECTION_ALREADY_EXISTS:
    return HAL_EBUSY;
  case ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE:
    return HAL_EUNSUPPORTED;
  default:
    return HAL_EIO;
  }
}

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
static uint32_t fnv1a32(const uint8_t *data, size_t length) {
  uint32_t hash = UINT32_C(2166136261);
  for (size_t index = 0u; index < length; ++index) {
    hash ^= data[index];
    hash *= UINT32_C(16777619);
  }
  return hash;
}

static void trace_hid_report(const uint8_t *data, size_t length) {
  const uint32_t hash = fnv1a32(data, length);
  if (s_backend.last_hid_report_valid &&
      s_backend.last_hid_report_length == length &&
      s_backend.last_hid_report_hash == hash) {
    return;
  }
  s_backend.last_hid_report_hash = hash;
  s_backend.last_hid_report_length = (uint16_t)length;
  s_backend.last_hid_report_valid = true;
  if (s_backend.hid_report_trace_count >= JH_CLASSIC_HID_REPORT_TRACE_LIMIT) {
    if (!s_backend.hid_report_trace_suppressed) {
      hal_deb("Bluetooth Classic HID report trace limit reached");
      s_backend.hid_report_trace_suppressed = true;
    }
    return;
  }
  static const char digits[] = "0123456789ABCDEF";
  char prefix[3u * 16u + 1u];
  const size_t shown = length < 16u ? length : 16u;
  size_t cursor = 0u;
  for (size_t index = 0u; index < shown; ++index) {
    if (index != 0u) {
      prefix[cursor++] = ' ';
    }
    prefix[cursor++] = digits[data[index] >> 4u];
    prefix[cursor++] = digits[data[index] & 0x0fu];
  }
  prefix[cursor] = '\0';
  hal_deb("Bluetooth Classic HID report len=%u hash=0x%08lx data=%s%s",
          (unsigned int)length, (unsigned long)hash, prefix,
          length > shown ? " ..." : "");
  ++s_backend.hid_report_trace_count;
}
#endif

static jh_classic_scan_cache_entry_t *
scan_cache_find(const hal_bluetooth_classic_address_t *address) {
  for (size_t index = 0u; index < JH_CLASSIC_SCAN_CACHE_DEPTH; ++index) {
    jh_classic_scan_cache_entry_t *entry = &s_backend.scan_cache[index];
    if (entry->used &&
        jh_bluetooth_classic_address_equal(&entry->result.address, address)) {
      return entry;
    }
  }
  return NULL;
}

static void
scan_cache_store(const hal_bluetooth_classic_scan_result_t *result) {
  jh_classic_scan_cache_entry_t *entry = scan_cache_find(&result->address);
  if (entry == NULL) {
    for (size_t index = 0u; index < JH_CLASSIC_SCAN_CACHE_DEPTH; ++index) {
      if (!s_backend.scan_cache[index].used) {
        entry = &s_backend.scan_cache[index];
        break;
      }
    }
  }
  if (entry == NULL) {
    entry = &s_backend.scan_cache[0];
  }
  entry->result = *result;
  entry->used = true;
}

static bool peer_restored(const hal_bluetooth_classic_address_t *address) {
  for (size_t index = 0u; index < HAL_BLUETOOTH_CLASSIC_MAX_PEERS; ++index) {
    if (s_backend.restored_peer_used[index] &&
        jh_bluetooth_classic_address_equal(&s_backend.restored_peers[index],
                                           address)) {
      return true;
    }
  }
  return false;
}

static void
emit_scan_result(const hal_bluetooth_classic_scan_result_t *result) {
  jh_bluetooth_classic_backend_event_t event = {0};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_SCAN_RESULT;
  event.status = HAL_OK;
  event.scan_result = *result;
  event.address = result->address;
  emit(&event);
}

static void start_inquiry(void) {
  const int status = gap_inquiry_start(JH_CLASSIC_INQUIRY_DURATION_1280_MS);
  if (status != ERROR_CODE_SUCCESS) {
    s_backend.scan_active = false;
    jh_bluetooth_classic_backend_event_t event = {0};
    event.type = JH_BLUETOOTH_CLASSIC_EVENT_ERROR;
    event.status = HAL_EIO;
    emit(&event);
    emit_simple(JH_BLUETOOTH_CLASSIC_EVENT_SCAN_STOPPED, HAL_EIO);
  }
}

static void finish_scan(hal_status_t status) {
  if (!s_backend.scan_active) {
    return;
  }
  s_backend.scan_active = false;
  (void)gap_inquiry_stop();
  emit_simple(JH_BLUETOOTH_CLASSIC_EVENT_SCAN_STOPPED, status);
}

static void handle_inquiry_result(const uint8_t *packet) {
  hal_bluetooth_classic_scan_result_t result = {0};
  gap_event_inquiry_result_get_bd_addr(packet, result.address.bytes);
  result.class_of_device = gap_event_inquiry_result_get_class_of_device(packet);
  result.rssi_valid = gap_event_inquiry_result_get_rssi_available(packet) != 0u;
  if (result.rssi_valid) {
    result.rssi = (int8_t)gap_event_inquiry_result_get_rssi(packet);
  }
  if (gap_event_inquiry_result_get_name_available(packet) != 0u) {
    const size_t available = gap_event_inquiry_result_get_name_len(packet);
    const size_t copied = available <= HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN
                              ? available
                              : HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN;
    const uint8_t *name = gap_event_inquiry_result_get_name(packet);
    if (name != NULL && copied != 0u) {
      memcpy(result.name, name, copied);
    }
    result.name[copied] = '\0';
    result.name_length = (uint8_t)copied;
  }
  hal_deb("Bluetooth Classic inquiry class=0x%06lx rssi=%d valid=%u "
          "name_length=%u",
          (unsigned long)result.class_of_device, (int)result.rssi,
          result.rssi_valid ? 1u : 0u, (unsigned int)result.name_length);
  scan_cache_store(&result);
  emit_scan_result(&result);
}

static void sdp_query_next(void);

static void sdp_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET || packet == NULL ||
      !s_backend.sdp_active) {
    return;
  }
  switch (hci_event_packet_get_type(packet)) {
  case SDP_EVENT_QUERY_SERVICE_RECORD_HANDLE:
    s_backend.sdp_match = true;
    break;
  case SDP_EVENT_QUERY_COMPLETE: {
    const uint8_t status = sdp_event_query_complete_get_status(packet);
    hal_deb("Bluetooth Classic SDP uuid=0x%04x status=0x%02x matched=%u",
            (unsigned int)s_sdp_uuids[s_backend.sdp_service_index],
            (unsigned int)status, s_backend.sdp_match ? 1u : 0u);
    if (status == ERROR_CODE_SUCCESS && s_backend.sdp_match) {
      s_backend.sdp_services |= s_sdp_service_bits[s_backend.sdp_service_index];
    }
    ++s_backend.sdp_service_index;
    sdp_query_next();
    break;
  }
  default:
    break;
  }
}

static void sdp_query_next(void) {
  if (s_backend.sdp_service_index >= JH_CLASSIC_SDP_SERVICE_COUNT) {
    s_backend.sdp_active = false;
    s_backend.sdp_retry_pending = false;
    s_backend.sdp_result.services = s_backend.sdp_services;
    s_backend.sdp_result.services_resolved = true;
    scan_cache_store(&s_backend.sdp_result);
    emit_scan_result(&s_backend.sdp_result);
    return;
  }
  s_backend.sdp_match = false;
  const uint8_t status =
      sdp_client_service_search(sdp_packet_handler, s_backend.sdp_address.bytes,
                                sdp_service_search_pattern_for_uuid16(
                                    s_sdp_uuids[s_backend.sdp_service_index]));
  hal_deb("Bluetooth Classic SDP start uuid=0x%04x status=0x%02x",
          (unsigned int)s_sdp_uuids[s_backend.sdp_service_index],
          (unsigned int)status);
  if (status == SDP_QUERY_BUSY) {
    s_backend.sdp_retry_pending = true;
    s_backend.sdp_retry_after_ms = hal_millis() + 50u;
    return;
  }
  if (status != ERROR_CODE_SUCCESS) {
    s_backend.sdp_active = false;
    s_backend.sdp_retry_pending = false;
    jh_bluetooth_classic_backend_event_t event = {0};
    event.type = JH_BLUETOOTH_CLASSIC_EVENT_ERROR;
    event.status = HAL_EIO;
    emit(&event);
    return;
  }
  s_backend.sdp_retry_pending = false;
  sdp_parser_init_service_search();
}

static bool address_is_active(const hal_bluetooth_classic_address_t *address) {
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  if (jh_bluetooth_classic_address_equal(address, &s_backend.hid_address)) {
    return true;
  }
#endif
  return jh_bluetooth_classic_address_equal(address,
                                            &s_backend.pairing_address);
}

static void set_pairing_request(const hal_bluetooth_classic_address_t *address,
                                hal_bluetooth_classic_pairing_method_t method) {
  if (!address_is_active(address)) {
    return;
  }
  s_backend.pairing_address = *address;
  s_backend.pairing_method = method;
  s_backend.pairing_pending = true;
  jh_bluetooth_classic_backend_event_t event = {0};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_PAIRING_REQUEST;
  event.status = HAL_OK;
  event.address = *address;
  event.pairing_method = method;
  emit(&event);
}

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
static void emit_hid_report(hal_bluetooth_hid_report_type_t type,
                            const uint8_t *data, size_t length) {
  if (data == NULL || length > HAL_BLUETOOTH_HID_REPORT_MAX_LEN) {
    jh_bluetooth_classic_backend_event_t error = {0};
    error.type = JH_BLUETOOTH_CLASSIC_EVENT_ERROR;
    error.status = HAL_EOVERFLOW;
    emit(&error);
    return;
  }
  jh_bluetooth_classic_backend_event_t event = {0};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_REPORT;
  event.status = HAL_OK;
  event.address = s_backend.hid_address;
  event.hid_report.type = type;
  event.hid_report.length = (uint8_t)length;
  if (length != 0u) {
    memcpy(event.hid_report.data, data, length);
    event.hid_report.report_id = data[0];
  }
  emit(&event);
}

static void disconnect_hid_acl(void) {
  if (s_backend.hid_con_handle == HCI_CON_HANDLE_INVALID) {
    return;
  }
  const uint8_t status = gap_disconnect(s_backend.hid_con_handle);
  s_backend.hid_acl_disconnect_pending =
      status == ERROR_CODE_SUCCESS || status == ERROR_CODE_COMMAND_DISALLOWED;
  hal_deb("Bluetooth Classic HID ACL disconnect handle=0x%04x "
          "status=0x%02x pending=%u",
          (unsigned int)s_backend.hid_con_handle, (unsigned int)status,
          s_backend.hid_acl_disconnect_pending ? 1u : 0u);
  if (!s_backend.hid_acl_disconnect_pending) {
    s_backend.hid_con_handle = HCI_CON_HANDLE_INVALID;
  }
}

static void
mark_hid_incoming_acl(const hal_bluetooth_classic_address_t *address) {
  if (address == NULL) {
    return;
  }
  if (s_backend.hid_incoming_acl_pending &&
      jh_bluetooth_classic_address_equal(address,
                                         &s_backend.hid_incoming_address)) {
    return;
  }
  s_backend.hid_incoming_address = *address;
  s_backend.hid_incoming_started_ms = hal_millis();
  s_backend.hid_incoming_acl_pending = true;
  jh_bluetooth_classic_backend_event_t event = {0};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_CONNECTING;
  event.status = HAL_OK;
  event.address = *address;
  emit(&event);
  hal_deb("Bluetooth Classic HID incoming ACL pending");
}

static void handle_hid_event(const uint8_t *packet) {
  const uint8_t subevent = hci_event_hid_meta_get_subevent_code(packet);
  if (subevent != HID_SUBEVENT_REPORT) {
    hal_deb("Bluetooth Classic HID subevent=0x%02x", (unsigned int)subevent);
  }
  switch (subevent) {
  case HID_SUBEVENT_INCOMING_CONNECTION: {
    jh_bluetooth_classic_backend_event_t event = {0};
    hid_subevent_incoming_connection_get_address(packet, event.address.bytes);
    const uint16_t hid_cid =
        hid_subevent_incoming_connection_get_hid_cid(packet);
    const bool restored = peer_restored(&event.address);
    hal_deb("Bluetooth Classic HID incoming restored=%u connected=%u "
            "active_cid=%u incoming_cid=%u",
            restored ? 1u : 0u, s_backend.hid_connected ? 1u : 0u,
            (unsigned int)s_backend.hid_cid, (unsigned int)hid_cid);
    if (restored && !s_backend.hid_connected && s_backend.hid_cid == 0u &&
        !s_backend.hid_acl_disconnect_pending) {
      const uint8_t status =
          hid_host_accept_connection(hid_cid, HID_PROTOCOL_MODE_REPORT);
      if (status == ERROR_CODE_SUCCESS) {
        s_backend.hid_cid = hid_cid;
        s_backend.hid_address = event.address;
        s_backend.hid_outgoing = false;
        s_backend.hid_incoming_acl_pending = false;
        s_backend.hid_con_handle =
            hid_subevent_incoming_connection_get_handle(packet);
        event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_CONNECTING;
        event.status = HAL_OK;
        emit(&event);
        hal_deb("Bluetooth Classic HID incoming accepted cid=%u",
                (unsigned int)hid_cid);
        break;
      }
      hal_deb("Bluetooth Classic HID incoming accept status=0x%02x",
              (unsigned int)status);
    }
    (void)hid_host_decline_connection(hid_cid);
    hal_deb("Bluetooth Classic HID incoming declined cid=%u",
            (unsigned int)hid_cid);
    break;
  }
  case HID_SUBEVENT_CONNECTION_OPENED: {
    const uint16_t hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
    const uint8_t status = hid_subevent_connection_opened_get_status(packet);
    if (status != ERROR_CODE_SUCCESS || hid_cid != s_backend.hid_cid) {
      if (hid_cid == s_backend.hid_cid) {
        const hal_status_t mapped = hid_connection_status_from_btstack(status);
        hal_deb("Bluetooth Classic HID connection status=0x%02x (%s)",
                (unsigned int)status, hal_status_to_string(mapped));
        jh_bluetooth_classic_backend_event_t event = {0};
        event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_DISCONNECTED;
        event.status = mapped;
        event.address = s_backend.hid_address;
        s_backend.hid_connected = false;
        s_backend.hid_outgoing = false;
        s_backend.hid_cid = 0u;
        if (s_backend.hid_incoming_acl_pending) {
          s_backend.hid_address = s_backend.hid_incoming_address;
          hal_deb("Bluetooth Classic HID outgoing collision yielded to "
                  "incoming ACL");
          break;
        }
        memset(&s_backend.hid_address, 0, sizeof(s_backend.hid_address));
        disconnect_hid_acl();
        emit(&event);
      }
      break;
    }
    hid_subevent_connection_opened_get_bd_addr(packet,
                                               s_backend.hid_address.bytes);
    s_backend.hid_connected = true;
    s_backend.hid_outgoing = false;
    s_backend.hid_incoming_acl_pending = false;
    s_backend.hid_con_handle =
        hid_subevent_connection_opened_get_con_handle(packet);
    s_backend.hid_acl_disconnect_pending = false;
    s_backend.last_hid_report_valid = false;
    s_backend.hid_report_trace_count = 0u;
    s_backend.hid_report_trace_suppressed = false;
    jh_bluetooth_classic_backend_event_t event = {0};
    event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_CONNECTED;
    event.status = HAL_OK;
    event.address = s_backend.hid_address;
    emit(&event);
    break;
  }
  case HID_SUBEVENT_DESCRIPTOR_AVAILABLE: {
    const uint8_t status = hid_subevent_descriptor_available_get_status(packet);
    if (status != ERROR_CODE_SUCCESS) {
      break;
    }
    const uint16_t length =
        hid_descriptor_storage_get_descriptor_len(s_backend.hid_cid);
    const uint8_t *descriptor =
        hid_descriptor_storage_get_descriptor_data(s_backend.hid_cid);
    if (descriptor == NULL || length > sizeof(s_backend.hid_descriptor)) {
      jh_bluetooth_classic_backend_event_t error = {0};
      error.type = JH_BLUETOOTH_CLASSIC_EVENT_ERROR;
      error.status = descriptor == NULL ? HAL_EPROTO : HAL_EOVERFLOW;
      emit(&error);
      break;
    }
    memcpy(s_backend.hid_descriptor, descriptor, length);
    hal_deb("Bluetooth Classic HID descriptor len=%u hash=0x%08lx",
            (unsigned int)length, (unsigned long)fnv1a32(descriptor, length));
    jh_bluetooth_classic_backend_event_t event = {0};
    event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_DESCRIPTOR;
    event.status = HAL_OK;
    event.address = s_backend.hid_address;
    memcpy(event.descriptor, descriptor, length);
    event.descriptor_length = length;
    emit(&event);
    (void)hid_host_send_set_protocol_mode(s_backend.hid_cid,
                                          HID_PROTOCOL_MODE_REPORT);
    break;
  }
  case HID_SUBEVENT_REPORT: {
    const uint8_t *report = hid_subevent_report_get_report(packet);
    size_t length = hid_subevent_report_get_report_len(packet);
    if (report != NULL && length != 0u && report[0] == 0xa1u) {
      ++report;
      --length;
    }
    trace_hid_report(report, length);
    emit_hid_report(HAL_BLUETOOTH_HID_REPORT_INPUT, report, length);
    break;
  }
  case HID_SUBEVENT_GET_REPORT_RESPONSE: {
    if (hid_subevent_get_report_response_get_handshake_status(packet) ==
        HID_HANDSHAKE_PARAM_TYPE_SUCCESSFUL) {
      emit_hid_report(s_backend.requested_report_type,
                      hid_subevent_get_report_response_get_report(packet),
                      hid_subevent_get_report_response_get_report_len(packet));
    }
    break;
  }
  case HID_SUBEVENT_CONNECTION_CLOSED: {
    jh_bluetooth_classic_backend_event_t event = {0};
    event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_DISCONNECTED;
    event.status = HAL_OK;
    event.address = s_backend.hid_address;
    s_backend.hid_connected = false;
    s_backend.hid_outgoing = false;
    s_backend.hid_incoming_acl_pending = false;
    s_backend.last_hid_report_valid = false;
    s_backend.hid_cid = 0u;
    memset(&s_backend.hid_address, 0, sizeof(s_backend.hid_address));
    disconnect_hid_acl();
    emit(&event);
    break;
  }
  default:
    break;
  }
}
#endif

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET || packet == NULL) {
    return;
  }
  const uint8_t type = hci_event_packet_get_type(packet);
  hal_bluetooth_classic_address_t address = {0};
  switch (type) {
  case BTSTACK_EVENT_STATE:
    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING &&
        !s_backend.controller_ready) {
      s_backend.controller_ready = true;
      emit_simple(JH_BLUETOOTH_CLASSIC_EVENT_READY, HAL_OK);
    }
    break;
  case GAP_EVENT_INQUIRY_RESULT:
    if (s_backend.scan_active) {
      handle_inquiry_result(packet);
    }
    break;
  case GAP_EVENT_INQUIRY_COMPLETE:
    hal_deb("Bluetooth Classic HCI inquiry complete");
    if (s_backend.scan_active) {
      if ((int32_t)(hal_millis() - s_backend.scan_deadline_ms) >= 0) {
        finish_scan(HAL_OK);
      } else {
        start_inquiry();
      }
    }
    break;
  case HCI_EVENT_CONNECTION_REQUEST:
    hci_event_connection_request_get_bd_addr(packet, address.bytes);
    hal_deb(
        "Bluetooth Classic HCI connection request class=0x%06lx "
        "link=0x%02x known=%u",
        (unsigned long)hci_event_connection_request_get_class_of_device(packet),
        (unsigned int)hci_event_connection_request_get_link_type(packet),
        peer_restored(&address) ? 1u : 0u);
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
    if (hci_event_connection_request_get_link_type(packet) ==
            HCI_LINK_TYPE_ACL &&
        !s_backend.hid_connected && peer_restored(&address)) {
      mark_hid_incoming_acl(&address);
    }
#endif
    break;
  case HCI_EVENT_CONNECTION_COMPLETE: {
    hci_event_connection_complete_get_bd_addr(packet, address.bytes);
    const uint8_t status = hci_event_connection_complete_get_status(packet);
    const hci_con_handle_t handle =
        hci_event_connection_complete_get_connection_handle(packet);
    hal_deb("Bluetooth Classic HCI connection complete status=0x%02x "
            "handle=0x%04x link=0x%02x encrypted=%u known=%u",
            (unsigned int)status, (unsigned int)handle,
            (unsigned int)hci_event_connection_complete_get_link_type(packet),
            (unsigned int)hci_event_connection_complete_get_encryption_enabled(
                packet),
            peer_restored(&address) ? 1u : 0u);
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
    if (status == ERROR_CODE_SUCCESS &&
        (jh_bluetooth_classic_address_equal(&address, &s_backend.hid_address) ||
         (s_backend.hid_incoming_acl_pending &&
          jh_bluetooth_classic_address_equal(
              &address, &s_backend.hid_incoming_address)))) {
      s_backend.hid_con_handle = handle;
    }
#endif
    break;
  }
  case HCI_EVENT_DISCONNECTION_COMPLETE: {
    const hci_con_handle_t handle =
        hci_event_disconnection_complete_get_connection_handle(packet);
    hal_deb("Bluetooth Classic HCI disconnection complete status=0x%02x "
            "handle=0x%04x reason=0x%02x",
            (unsigned int)hci_event_disconnection_complete_get_status(packet),
            (unsigned int)handle,
            (unsigned int)hci_event_disconnection_complete_get_reason(packet));
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
    if (handle == s_backend.hid_con_handle) {
      s_backend.hid_con_handle = HCI_CON_HANDLE_INVALID;
      s_backend.hid_acl_disconnect_pending = false;
    }
#endif
    break;
  }
  case HCI_EVENT_COMMAND_STATUS: {
    const uint16_t opcode = hci_event_command_status_get_command_opcode(packet);
    if (opcode == HCI_OPCODE_HCI_CREATE_CONNECTION ||
        opcode == HCI_OPCODE_HCI_DISCONNECT ||
        opcode == HCI_OPCODE_HCI_ACCEPT_CONNECTION_REQUEST) {
      hal_deb("Bluetooth Classic HCI command status opcode=0x%04x "
              "status=0x%02x",
              (unsigned int)opcode,
              (unsigned int)hci_event_command_status_get_status(packet));
    }
    break;
  }
  case HCI_EVENT_USER_CONFIRMATION_REQUEST:
    hal_deb("Bluetooth Classic HCI pairing request method=just-works");
    hci_event_user_confirmation_request_get_bd_addr(packet, address.bytes);
    set_pairing_request(&address, HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS);
    break;
  case HCI_EVENT_PIN_CODE_REQUEST:
    hal_deb("Bluetooth Classic HCI pairing request method=legacy-pin");
    hci_event_pin_code_request_get_bd_addr(packet, address.bytes);
    set_pairing_request(&address, HAL_BLUETOOTH_CLASSIC_PAIRING_PIN);
    break;
  case HCI_EVENT_USER_PASSKEY_REQUEST:
    hal_deb("Bluetooth Classic HCI pairing request method=passkey");
    hci_event_user_passkey_request_get_bd_addr(packet, address.bytes);
    set_pairing_request(&address, HAL_BLUETOOTH_CLASSIC_PAIRING_PASSKEY);
    (void)gap_ssp_passkey_negative(address.bytes);
    break;
  case HCI_EVENT_AUTHENTICATION_COMPLETE: {
    const uint8_t raw_status =
        hci_event_authentication_complete_get_status(packet);
    hal_deb(
        "Bluetooth Classic authentication status=0x%02x (%s)",
        (unsigned int)raw_status,
        hal_status_to_string(hid_connection_status_from_btstack(raw_status)));
    jh_bluetooth_classic_backend_event_t event = {0};
    event.type = JH_BLUETOOTH_CLASSIC_EVENT_AUTHENTICATION;
    event.status = hid_connection_status_from_btstack(raw_status);
    event.address = s_backend.pairing_address;
    s_backend.pairing_pending = false;
    emit(&event);
    break;
  }
  case HCI_EVENT_LINK_KEY_NOTIFICATION: {
    hal_deb("Bluetooth Classic HCI link-key notification type=0x%02x",
            (unsigned int)packet[24]);
    bool key_is_null = true;
    for (size_t index = 0u; index < 16u; ++index) {
      key_is_null = key_is_null && packet[8u + index] == 0u;
    }
    if (!key_is_null) {
      jh_bluetooth_classic_backend_event_t event = {0};
      event.type = JH_BLUETOOTH_CLASSIC_EVENT_LINK_KEY;
      event.status = HAL_OK;
      hci_event_link_key_request_get_bd_addr(packet, event.address.bytes);
      memcpy(event.link_key, &packet[8], sizeof(event.link_key));
      event.link_key_type = packet[24];
      emit(&event);
    }
    break;
  }
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  case HCI_EVENT_HID_META:
    handle_hid_event(packet);
    break;
#endif
  default:
    break;
  }
}

static hal_status_t profile_start(void *context) {
  (void)context;
  hci_set_link_key_db(btstack_link_key_db_memory_instance());
  sdp_client_init();
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  hid_host_init(s_backend.hid_descriptor, sizeof(s_backend.hid_descriptor));
  hid_host_register_packet_handler(packet_handler);
#endif
  memset(&s_backend.hci_events, 0, sizeof(s_backend.hci_events));
  s_backend.hci_events.callback = packet_handler;
  hci_add_event_handler(&s_backend.hci_events);
  hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
  gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE |
                                       LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
  hci_set_master_slave_policy(HCI_ROLE_MASTER);
  gap_set_bondable_mode(1);
  gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
  gap_ssp_set_authentication_requirement(
      SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_DEDICATED_BONDING);
  gap_ssp_set_auto_accept(0);
  return HAL_OK;
}

static void profile_stop(void *context) {
  (void)context;
  if (s_backend.scan_active) {
    (void)gap_inquiry_stop();
  }
  hci_remove_event_handler(&s_backend.hci_events);
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  hid_host_register_packet_handler(NULL);
  (void)l2cap_unregister_service(PSM_HID_INTERRUPT);
  (void)l2cap_unregister_service(PSM_HID_CONTROL);
  hid_host_deinit();
#endif
  sdp_client_deinit();
  hci_set_link_key_db(NULL);
  s_backend.controller_ready = false;
  s_backend.scan_active = false;
  s_backend.sdp_active = false;
}

static hal_status_t profile_service(void *context) {
  (void)context;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  if (s_backend.hid_incoming_acl_pending && !s_backend.hid_connected &&
      s_backend.hid_cid == 0u &&
      hal_millis_deadline_expired(s_backend.hid_incoming_started_ms,
                                  JH_CLASSIC_HID_INCOMING_ACL_TIMEOUT_MS)) {
    jh_bluetooth_classic_backend_event_t event = {0};
    event.type = JH_BLUETOOTH_CLASSIC_EVENT_HID_DISCONNECTED;
    event.status = HAL_ETIMEOUT;
    event.address = s_backend.hid_incoming_address;
    s_backend.hid_incoming_acl_pending = false;
    memset(&s_backend.hid_address, 0, sizeof(s_backend.hid_address));
    hal_deb("Bluetooth Classic HID incoming ACL timed out");
    emit(&event);
  }
#endif
  return HAL_OK;
}

static void profile_invalidated(void *context, uint32_t generation) {
  (void)context;
  (void)generation;
  s_backend.controller_ready = false;
  s_backend.scan_active = false;
  s_backend.sdp_active = false;
  jh_bluetooth_classic_backend_event_t event = {0};
  event.type = JH_BLUETOOTH_CLASSIC_EVENT_ERROR;
  event.status = HAL_EHW;
  event.fatal = true;
  emit(&event);
}

static const jh_bluetooth_host_profile_ops_t s_profile_ops = {
    .context = NULL,
    .start = profile_start,
    .stop = profile_stop,
    .service = profile_service,
    .invalidated = profile_invalidated,
};

static hal_status_t
backend_start(void *context,
              jh_bluetooth_classic_backend_event_fn event_handler,
              void *event_context) {
  (void)context;
  if (s_backend.started) {
    return HAL_EBUSY;
  }
  memset(&s_backend, 0, sizeof(s_backend));
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  s_backend.hid_con_handle = HCI_CON_HANDLE_INVALID;
#endif
  s_backend.event_handler = event_handler;
  s_backend.event_context = event_context;
  const hal_status_t status =
      jh_btstack_host_acquire(JH_BLUETOOTH_HOST_PROFILE_CLASSIC, &s_profile_ops,
                              &s_backend.host_reference);
  s_backend.started = status == HAL_OK;
  return status;
}

static hal_status_t backend_stop(void *context) {
  (void)context;
  if (!s_backend.started) {
    return HAL_EUNINIT;
  }
  const hal_status_t status =
      jh_btstack_host_release(&s_backend.host_reference);
  s_backend.started = false;
  return status;
}

static hal_status_t backend_service(void *context) {
  (void)context;
  if (!s_backend.started) {
    return HAL_EUNINIT;
  }
  const hal_status_t status =
      jh_btstack_host_service(&s_backend.host_reference);
  if (s_backend.sdp_active && s_backend.sdp_retry_pending &&
      (int32_t)(hal_millis() - s_backend.sdp_retry_after_ms) >= 0) {
    s_backend.sdp_retry_pending = false;
    sdp_query_next();
  }
  if (s_backend.scan_active &&
      (int32_t)(hal_millis() - s_backend.scan_deadline_ms) >= 0) {
    finish_scan(HAL_OK);
  }
  return status;
}

static hal_status_t backend_scan_start(void *context, uint32_t duration_ms) {
  (void)context;
  if (!s_backend.started || !s_backend.controller_ready) {
    return HAL_EUNINIT;
  }
  if (s_backend.scan_active || s_backend.sdp_active) {
    return HAL_ESTATE;
  }
  memset(s_backend.scan_cache, 0, sizeof(s_backend.scan_cache));
  s_backend.scan_active = true;
  s_backend.scan_deadline_ms = hal_millis() + duration_ms;
  start_inquiry();
  return s_backend.scan_active ? HAL_OK : HAL_EIO;
}

static hal_status_t backend_scan_stop(void *context) {
  (void)context;
  if (!s_backend.started) {
    return HAL_EUNINIT;
  }
  if (!s_backend.scan_active) {
    return HAL_ESTATE;
  }
  finish_scan(HAL_OK);
  return HAL_OK;
}

static hal_status_t
backend_sdp_query(void *context,
                  const hal_bluetooth_classic_address_t *address) {
  (void)context;
  if (!s_backend.started || !s_backend.controller_ready) {
    return HAL_EUNINIT;
  }
  if (address == NULL || s_backend.sdp_active) {
    return address == NULL ? HAL_EINVAL : HAL_EBUSY;
  }
  s_backend.sdp_address = *address;
  memset(&s_backend.sdp_result, 0, sizeof(s_backend.sdp_result));
  jh_classic_scan_cache_entry_t *cached = scan_cache_find(address);
  if (cached != NULL) {
    s_backend.sdp_result = cached->result;
  } else {
    s_backend.sdp_result.address = *address;
  }
  s_backend.sdp_result.services = 0u;
  s_backend.sdp_result.services_resolved = false;
  s_backend.sdp_services = 0u;
  s_backend.sdp_service_index = 0u;
  s_backend.sdp_active = true;
  sdp_query_next();
  return s_backend.sdp_active || s_backend.sdp_result.services_resolved
             ? HAL_OK
             : HAL_EIO;
}

static hal_status_t
backend_pair(void *context, const hal_bluetooth_classic_address_t *address) {
  (void)context;
  if (!s_backend.started || !s_backend.controller_ready) {
    return HAL_EUNINIT;
  }
  if (address == NULL || s_backend.pairing_pending) {
    return address == NULL ? HAL_EINVAL : HAL_EBUSY;
  }
  s_backend.pairing_address = *address;
  bd_addr_t native_address;
  memcpy(native_address, address->bytes, sizeof(native_address));
  return status_from_btstack(gap_dedicated_bonding(native_address, 0));
}

static hal_status_t backend_pairing_reply(void *context, bool accept) {
  (void)context;
  if (!s_backend.started) {
    return HAL_EUNINIT;
  }
  if (!s_backend.pairing_pending) {
    return HAL_ESTATE;
  }
  int status = ERROR_CODE_COMMAND_DISALLOWED;
  if (s_backend.pairing_method == HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS) {
    status =
        accept ? gap_ssp_confirmation_response(s_backend.pairing_address.bytes)
               : gap_ssp_confirmation_negative(s_backend.pairing_address.bytes);
  } else if (s_backend.pairing_method == HAL_BLUETOOTH_CLASSIC_PAIRING_PIN) {
    status =
        accept ? gap_pin_code_response(s_backend.pairing_address.bytes, "0000")
               : gap_pin_code_negative(s_backend.pairing_address.bytes);
  } else {
    return HAL_EUNSUPPORTED;
  }
  if (status == ERROR_CODE_SUCCESS) {
    s_backend.pairing_pending = false;
  }
  return status_from_btstack(status);
}

static hal_status_t
backend_peer_restore(void *context,
                     const hal_bluetooth_classic_address_t *address,
                     const uint8_t link_key[16], uint8_t link_key_type) {
  (void)context;
  if (!s_backend.started || address == NULL || link_key == NULL) {
    return !s_backend.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  size_t free_index = HAL_BLUETOOTH_CLASSIC_MAX_PEERS;
  for (size_t index = 0u; index < HAL_BLUETOOTH_CLASSIC_MAX_PEERS; ++index) {
    if (s_backend.restored_peer_used[index] &&
        jh_bluetooth_classic_address_equal(&s_backend.restored_peers[index],
                                           address)) {
      free_index = index;
      break;
    }
    if (!s_backend.restored_peer_used[index] &&
        free_index == HAL_BLUETOOTH_CLASSIC_MAX_PEERS) {
      free_index = index;
    }
  }
  if (free_index == HAL_BLUETOOTH_CLASSIC_MAX_PEERS) {
    return HAL_EOVERFLOW;
  }
  bd_addr_t native_address;
  link_key_t native_key;
  memcpy(native_address, address->bytes, sizeof(native_address));
  memcpy(native_key, link_key, sizeof(native_key));
  btstack_link_key_db_memory_instance()->put_link_key(
      native_address, native_key, (link_key_type_t)link_key_type);
  s_backend.restored_peers[free_index] = *address;
  s_backend.restored_peer_used[free_index] = true;
  hal_deb("Bluetooth Classic peer restored slot=%u", (unsigned int)free_index);
  return HAL_OK;
}

static hal_status_t
backend_peer_forget(void *context,
                    const hal_bluetooth_classic_address_t *address) {
  (void)context;
  if (!s_backend.started || address == NULL) {
    return !s_backend.started ? HAL_EUNINIT : HAL_EINVAL;
  }
  bd_addr_t native_address;
  memcpy(native_address, address->bytes, sizeof(native_address));
  btstack_link_key_db_memory_instance()->delete_link_key(native_address);
  for (size_t index = 0u; index < HAL_BLUETOOTH_CLASSIC_MAX_PEERS; ++index) {
    if (s_backend.restored_peer_used[index] &&
        jh_bluetooth_classic_address_equal(&s_backend.restored_peers[index],
                                           address)) {
      s_backend.restored_peer_used[index] = false;
      memset(&s_backend.restored_peers[index], 0,
             sizeof(s_backend.restored_peers[index]));
    }
  }
  return HAL_OK;
}

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
static hal_status_t
backend_hid_connect(void *context,
                    const hal_bluetooth_classic_address_t *address) {
  (void)context;
  if (!s_backend.started || !s_backend.controller_ready) {
    return HAL_EUNINIT;
  }
  if (address == NULL || s_backend.hid_cid != 0u || s_backend.sdp_active ||
      s_backend.hid_incoming_acl_pending ||
      s_backend.hid_acl_disconnect_pending) {
    return address == NULL ? HAL_EINVAL
                           : (s_backend.hid_incoming_acl_pending ||
                                      s_backend.hid_acl_disconnect_pending
                                  ? HAL_EBUSY
                                  : HAL_ESTATE);
  }
  uint16_t hid_cid = 0u;
  bd_addr_t native_address;
  memcpy(native_address, address->bytes, sizeof(native_address));
  const uint8_t status =
      hid_host_connect(native_address, HID_PROTOCOL_MODE_REPORT, &hid_cid);
  if (status != ERROR_CODE_SUCCESS) {
    hal_deb("Bluetooth Classic HID connect rejected status=0x%02x",
            (unsigned int)status);
    return hid_connection_status_from_btstack(status);
  }
  hal_deb("Bluetooth Classic HID connect started cid=%u",
          (unsigned int)hid_cid);
  s_backend.hid_address = *address;
  s_backend.pairing_address = *address;
  s_backend.hid_cid = hid_cid;
  s_backend.hid_outgoing = true;
  return HAL_OK;
}

static hal_status_t backend_hid_disconnect(void *context) {
  (void)context;
  if (!s_backend.started) {
    return HAL_EUNINIT;
  }
  if (!s_backend.hid_connected || s_backend.hid_cid == 0u) {
    return HAL_ESTATE;
  }
  hid_host_disconnect(s_backend.hid_cid);
  return HAL_OK;
}

static hal_status_t
backend_hid_report_send(void *context,
                        const hal_bluetooth_hid_report_t *report) {
  (void)context;
  if (!s_backend.started || !s_backend.hid_connected || report == NULL) {
    return !s_backend.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  const uint8_t status = hid_host_send_set_report(
      s_backend.hid_cid, (hid_report_type_t)report->type, report->report_id,
      report->data, report->length);
  return status_from_btstack(status);
}

static hal_status_t
backend_hid_report_request(void *context, hal_bluetooth_hid_report_type_t type,
                           uint8_t report_id) {
  (void)context;
  if (!s_backend.started || !s_backend.hid_connected) {
    return !s_backend.started ? HAL_EUNINIT : HAL_ESTATE;
  }
  const uint8_t status = hid_host_send_get_report(
      s_backend.hid_cid, (hid_report_type_t)type, report_id);
  if (status == ERROR_CODE_SUCCESS) {
    s_backend.requested_report_type = type;
  }
  return status_from_btstack(status);
}
#endif

static const jh_bluetooth_classic_backend_t s_backend_ops = {
    .context = NULL,
    .start = backend_start,
    .stop = backend_stop,
    .service = backend_service,
    .scan_start = backend_scan_start,
    .scan_stop = backend_scan_stop,
    .sdp_query = backend_sdp_query,
    .pair = backend_pair,
    .pairing_reply = backend_pairing_reply,
    .peer_restore = backend_peer_restore,
    .peer_forget = backend_peer_forget,
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
    .hid_connect = backend_hid_connect,
    .hid_disconnect = backend_hid_disconnect,
    .hid_report_send = backend_hid_report_send,
    .hid_report_request = backend_hid_report_request,
#endif
};

const jh_bluetooth_classic_backend_t *
jh_bluetooth_classic_backend_instance(void) {
  return &s_backend_ops;
}

#endif
