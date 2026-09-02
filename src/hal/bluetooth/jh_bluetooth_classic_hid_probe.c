#include "jh_bluetooth_classic_hid_probe.h"

#include "bluetooth_sdp.h"
#include "btstack_event.h"
#include "btstack_version.h"
#include "classic/btstack_link_key_db_memory.h"
#include "classic/hid_host.h"
#include "classic/sdp_client.h"
#include "classic/sdp_util.h"
#include "gap.h"
#include "hal/core/hal_config.h"
#include "hal/system/hal_system.h"
#include "hci.h"
#include "jh_bluetooth_classic_hid_lifecycle.h"
#include "jh_bluetooth_classic_hid_probe_logic.h"
#include "jh_bluetooth_gamepad_parser.h"
#include "jh_btstack_hci_transport_cyw43.h"
#include "jh_btstack_host.h"
#include "l2cap.h"

#include <stddef.h>
#include <string.h>

extern void sdp_parser_init_service_search(void);

enum {
  JH_CLASSIC_HID_INQUIRY_DURATION_1280_MS = 8u,
  JH_CLASSIC_HID_SDP_SETTLE_MS = 1000u,
  JH_CLASS_OF_DEVICE_MAJOR_MASK = 0x1f00u,
  JH_CLASS_OF_DEVICE_MAJOR_PERIPHERAL = 0x0500u,
  JH_CLASSIC_HID_CAPTURE_DESCRIPTOR_LENGTH = 136u,
};

static const uint32_t JH_CLASSIC_HID_CAPTURE_DESCRIPTOR_HASH = 0xe51f7bbeu;

static uint8_t s_hid_descriptor[JH_BLUETOOTH_GAMEPAD_DESCRIPTOR_MAX];
static jh_bluetooth_gamepad_parser_t s_gamepad_parser;
static btstack_packet_callback_registration_t s_hci_events;
static jh_bluetooth_classic_hid_lifecycle_t s_lifecycle;
static jh_bluetooth_classic_hid_probe_logic_t s_logic;
static jh_bluetooth_classic_hid_probe_snapshot_t s_snapshot;
static jh_bluetooth_host_reference_t s_host_reference;
static bd_addr_t s_candidate_address;
static bd_addr_t s_known_address;
static bd_addr_t s_pairing_address;
static uint8_t s_candidate_page_scan_mode;
static uint16_t s_candidate_clock_offset;
static uint32_t s_candidate_class_of_device;
static uint16_t s_hid_cid;
static uint32_t s_connected_since_ms;
static uint32_t s_connected_accumulated_ms;
static uint32_t s_connect_at_ms;
static bool s_candidate_available;
static bool s_candidate_name_verified;
static bool s_hid_record_found;
static bool s_identity_validated;
static bool s_reconnect_pending;
static bool s_connect_pending;
static const hal_gamepad_bond_provider_t *s_bond_provider;
static uint32_t s_bond_sequence;
static bool s_retain_gamepad_queue;

static void start_inquiry_cycle(void);
static void start_identity_sdp_query(void);
static void start_pnp_service_search(void);

static void sync_logic_snapshot(void) {
  s_snapshot.discovery_open = s_logic.discovery_open;
  s_snapshot.connected = s_logic.connected;
  s_snapshot.reports = s_logic.reports;
  s_snapshot.report_bytes = s_logic.report_bytes;
  s_snapshot.release_all_events = s_logic.release_all_events;
  s_snapshot.active_controls_mask = s_logic.active_controls_mask;
  s_snapshot.seen_controls_mask = s_logic.seen_controls_mask;
  s_snapshot.report_length_high_water = s_logic.report_length_high_water;
  jh_bluetooth_gamepad_parser_diagnostics(&s_gamepad_parser,
                                          &s_snapshot.parser);
  s_snapshot.invalid_reports = s_snapshot.parser.reports_rejected;
}

static void drain_gamepad_queue(void) {
  if (s_retain_gamepad_queue) {
    return;
  }
  jh_bluetooth_gamepad_snapshot_t snapshot;
  hal_status_t status = HAL_OK;
  do {
    status = jh_bluetooth_gamepad_parser_next(&s_gamepad_parser, &snapshot);
  } while (status == HAL_OK || status == HAL_EOVERFLOW);
}

static hal_status_t gamepad_connection_opened(void) {
  jh_bluetooth_gamepad_snapshot_t snapshot;
  const hal_status_t snapshot_status =
      jh_bluetooth_gamepad_parser_snapshot(&s_gamepad_parser, &snapshot);
  if (snapshot_status != HAL_OK) {
    return snapshot_status;
  }
  if (snapshot.connected) {
    return HAL_OK;
  }
  const hal_status_t status =
      jh_bluetooth_gamepad_parser_connection_opened(&s_gamepad_parser);
  if (status == HAL_OK) {
    drain_gamepad_queue();
  }
  return status;
}

static void gamepad_connection_closed(void) {
  jh_bluetooth_gamepad_snapshot_t snapshot;
  if (jh_bluetooth_gamepad_parser_snapshot(&s_gamepad_parser, &snapshot) ==
          HAL_OK &&
      snapshot.connected &&
      jh_bluetooth_gamepad_parser_connection_closed(&s_gamepad_parser) ==
          HAL_OK) {
    drain_gamepad_queue();
  }
}

static uint32_t fnv1a32(const uint8_t *data, size_t length) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0u; index < length; ++index) {
    hash = (hash ^ data[index]) * 16777619u;
  }
  return hash;
}

static bool address_is_known(const bd_addr_t address) {
  return s_snapshot.known_device && bd_addr_cmp(address, s_known_address) == 0;
}

static void reset_candidate(void) {
  memset(s_candidate_address, 0, sizeof(s_candidate_address));
  s_candidate_page_scan_mode = 0u;
  s_candidate_clock_offset = 0u;
  s_candidate_class_of_device = 0u;
  s_candidate_available = false;
  s_candidate_name_verified = false;
}

static void reject_candidate_and_continue(void) {
  ++s_snapshot.identity_rejections;
  reset_candidate();
  if (s_logic.discovery_open) {
    start_inquiry_cycle();
  } else {
    s_snapshot.phase = s_snapshot.known_device ? JH_CLASSIC_HID_PHASE_KNOWN_IDLE
                                               : JH_CLASSIC_HID_PHASE_READY;
  }
}

static void start_inquiry_cycle(void) {
  if (!s_logic.discovery_open) {
    return;
  }
  const int status = gap_inquiry_start(JH_CLASSIC_HID_INQUIRY_DURATION_1280_MS);
  s_snapshot.last_btstack_status = (uint8_t)status;
  if (status != ERROR_CODE_SUCCESS) {
    s_snapshot.last_status = HAL_EIO;
    jh_bluetooth_classic_hid_probe_logic_close_discovery(&s_logic);
    sync_logic_snapshot();
    s_snapshot.phase = s_snapshot.known_device ? JH_CLASSIC_HID_PHASE_KNOWN_IDLE
                                               : JH_CLASSIC_HID_PHASE_READY;
    return;
  }
  ++s_snapshot.inquiry_cycles;
  s_snapshot.phase = JH_CLASSIC_HID_PHASE_INQUIRY;
}

static void start_remote_name_query(void) {
  const int status =
      gap_remote_name_request(s_candidate_address, s_candidate_page_scan_mode,
                              (uint16_t)(s_candidate_clock_offset | 0x8000u));
  s_snapshot.last_btstack_status = (uint8_t)status;
  if (status != ERROR_CODE_SUCCESS) {
    reject_candidate_and_continue();
    return;
  }
  s_snapshot.phase = JH_CLASSIC_HID_PHASE_REMOTE_NAME;
}

static hal_status_t connect_candidate(bool reconnect) {
  uint16_t hid_cid = 0u;
  const uint8_t status = hid_host_connect(
      s_candidate_address, HID_PROTOCOL_MODE_REPORT_WITH_FALLBACK_TO_BOOT,
      &hid_cid);
  s_snapshot.last_btstack_status = status;
  if (status != ERROR_CODE_SUCCESS) {
    ++s_snapshot.connection_failures;
    s_snapshot.last_status = HAL_EIO;
    s_snapshot.phase = JH_CLASSIC_HID_PHASE_KNOWN_IDLE;
    return HAL_EIO;
  }
  s_hid_cid = hid_cid;
  s_reconnect_pending = reconnect;
  if (reconnect) {
    ++s_snapshot.reconnect_attempts;
  }
  s_snapshot.phase = JH_CLASSIC_HID_PHASE_CONNECTING;
  return HAL_OK;
}

static void identity_sdp_packet_handler(uint8_t packet_type, uint16_t channel,
                                        uint8_t *packet, uint16_t size) {
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET || packet == NULL) {
    return;
  }
  switch (hci_event_packet_get_type(packet)) {
  case SDP_EVENT_QUERY_ATTRIBUTE_VALUE:
    s_hid_record_found = true;
    break;
  case SDP_EVENT_QUERY_COMPLETE: {
    const uint8_t status = sdp_event_query_complete_get_status(packet);
    s_snapshot.last_btstack_status = status;
    const bool identity_matches =
        status == ERROR_CODE_SUCCESS && s_hid_record_found;
    if (!identity_matches || !s_logic.discovery_open) {
      reject_candidate_and_continue();
      return;
    }
    ++s_snapshot.sdp_hid_matches;
    start_pnp_service_search();
    break;
  }
  default:
    break;
  }
}

static void start_identity_sdp_query(void) {
  if (!s_logic.discovery_open || !s_candidate_available ||
      !s_candidate_name_verified) {
    reject_candidate_and_continue();
    return;
  }
  s_hid_record_found = false;
  const uint8_t status = sdp_client_query_uuid16(
      identity_sdp_packet_handler, s_candidate_address,
      BLUETOOTH_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE_SERVICE);
  s_snapshot.last_btstack_status = status;
  if (status != ERROR_CODE_SUCCESS) {
    reject_candidate_and_continue();
    return;
  }
  s_snapshot.phase = JH_CLASSIC_HID_PHASE_SDP_HID;
}

static void finalize_hid_connection(void) {
  jh_bluetooth_classic_hid_probe_logic_connected(&s_logic);
  const hal_status_t parser_status = gamepad_connection_opened();
  if (parser_status != HAL_OK && parser_status != HAL_EUNINIT) {
    s_snapshot.last_status = parser_status;
  }
  sync_logic_snapshot();
  s_snapshot.phase = JH_CLASSIC_HID_PHASE_CONNECTED;
  if (s_snapshot.protocol == JH_CLASSIC_HID_PROTOCOL_UNKNOWN) {
    s_snapshot.protocol = JH_CLASSIC_HID_PROTOCOL_REPORT;
  }
  s_connected_since_ms = hal_millis();
  ++s_snapshot.connections;
  if (s_reconnect_pending) {
    ++s_snapshot.reconnect_successes;
    s_reconnect_pending = false;
  }
}

static void reject_connected_candidate(void) {
  ++s_snapshot.identity_rejections;
  s_snapshot.known_device = false;
  memset(s_known_address, 0, sizeof(s_known_address));
  s_identity_validated = false;
  jh_bluetooth_classic_hid_probe_logic_reset_bond_progress(&s_logic);
  if (s_hid_cid != 0u) {
    hid_host_disconnect(s_hid_cid);
  }
}

static void pnp_service_search_packet_handler(uint8_t packet_type,
                                              uint16_t channel, uint8_t *packet,
                                              uint16_t size) {
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET || packet == NULL) {
    return;
  }
  switch (hci_event_packet_get_type(packet)) {
  case SDP_EVENT_QUERY_SERVICE_RECORD_HANDLE:
    ++s_snapshot.pnp_service_records;
    if (s_snapshot.pnp_record_handle == 0u) {
      s_snapshot.pnp_record_handle =
          sdp_event_query_service_record_handle_get_record_handle(packet);
    }
    break;
  case SDP_EVENT_QUERY_COMPLETE: {
    const uint8_t status = sdp_event_query_complete_get_status(packet);
    s_snapshot.last_btstack_status = status;
    if (status != ERROR_CODE_SUCCESS || s_snapshot.pnp_service_records != 1u ||
        s_snapshot.pnp_record_handle == 0u) {
      reject_candidate_and_continue();
      return;
    }
    ++s_snapshot.pnp_identity_matches;
    s_identity_validated = true;
    jh_bluetooth_classic_hid_probe_logic_identity_validated(&s_logic);
    s_snapshot.known_device = true;
    memcpy(s_known_address, s_candidate_address, sizeof(s_known_address));
    jh_bluetooth_classic_hid_probe_logic_close_discovery(&s_logic);
    sync_logic_snapshot();
    s_connect_at_ms = hal_millis() + JH_CLASSIC_HID_SDP_SETTLE_MS;
    s_connect_pending = true;
    s_snapshot.phase = JH_CLASSIC_HID_PHASE_SDP_PNP;
    break;
  }
  default:
    break;
  }
}

static void start_pnp_service_search(void) {
  s_snapshot.pnp_service_records = 0u;
  s_snapshot.pnp_record_handle = 0u;
  const uint8_t status = sdp_client_service_search(
      pnp_service_search_packet_handler, s_candidate_address,
      sdp_service_search_pattern_for_uuid16(
          BLUETOOTH_SERVICE_CLASS_PNP_INFORMATION));
  s_snapshot.last_btstack_status = status;
  if (status != ERROR_CODE_SUCCESS) {
    reject_candidate_and_continue();
    return;
  }
  sdp_parser_init_service_search();
  s_snapshot.phase = JH_CLASSIC_HID_PHASE_SDP_PNP;
}

static void handle_inquiry_result(const uint8_t *packet) {
  ++s_snapshot.inquiry_results;
  const uint32_t class_of_device =
      gap_event_inquiry_result_get_class_of_device(packet);
  if ((class_of_device & JH_CLASS_OF_DEVICE_MAJOR_MASK) !=
      JH_CLASS_OF_DEVICE_MAJOR_PERIPHERAL) {
    return;
  }
  ++s_snapshot.peripheral_candidates;

  const bool name_available =
      gap_event_inquiry_result_get_name_available(packet) != 0u;
  const uint8_t *name =
      name_available ? gap_event_inquiry_result_get_name(packet) : NULL;
  const size_t name_length =
      name_available ? gap_event_inquiry_result_get_name_len(packet) : 0u;
  if (name_available && !jh_bluetooth_classic_hid_probe_logic_candidate_matches(
                            class_of_device, name, name_length)) {
    ++s_snapshot.identity_rejections;
    return;
  }

  if (!s_candidate_available || name_available) {
    gap_event_inquiry_result_get_bd_addr(packet, s_candidate_address);
    s_candidate_page_scan_mode =
        gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
    s_candidate_clock_offset =
        gap_event_inquiry_result_get_clock_offset(packet);
    s_candidate_class_of_device = class_of_device;
    s_candidate_available = true;
    s_candidate_name_verified = name_available;
  }
  if (name_available) {
    (void)gap_inquiry_stop();
  }
}

static void handle_inquiry_complete(void) {
  if (!s_logic.discovery_open) {
    s_snapshot.phase = s_snapshot.known_device ? JH_CLASSIC_HID_PHASE_KNOWN_IDLE
                                               : JH_CLASSIC_HID_PHASE_READY;
    return;
  }
  if (!s_candidate_available) {
    start_inquiry_cycle();
    return;
  }
  if (s_candidate_name_verified) {
    start_identity_sdp_query();
    return;
  }
  start_remote_name_query();
}

static void handle_remote_name(const uint8_t *packet, uint16_t size) {
  bd_addr_t address;
  hci_event_remote_name_request_complete_get_bd_addr(packet, address);
  if (!s_candidate_available ||
      bd_addr_cmp(address, s_candidate_address) != 0 ||
      hci_event_remote_name_request_complete_get_status(packet) !=
          ERROR_CODE_SUCCESS ||
      size <= 9u) {
    reject_candidate_and_continue();
    return;
  }

  const uint8_t *name =
      (const uint8_t *)hci_event_remote_name_request_complete_get_remote_name(
          packet);
  size_t name_length = 0u;
  const size_t available = size - 9u;
  while (name_length < available && name[name_length] != 0u) {
    ++name_length;
  }
  if (!jh_bluetooth_classic_hid_probe_logic_candidate_matches(
          s_candidate_class_of_device, name, name_length)) {
    reject_candidate_and_continue();
    return;
  }
  s_candidate_name_verified = true;
  start_identity_sdp_query();
}

static void
set_pairing_request(jh_bluetooth_classic_hid_pairing_method_t method,
                    const bd_addr_t address) {
  if (!address_is_known(address)) {
    return;
  }
  memcpy(s_pairing_address, address, sizeof(s_pairing_address));
  s_snapshot.pairing_method = method;
  s_snapshot.pairing_pending =
      method != JH_CLASSIC_HID_PAIRING_UNSUPPORTED_PASSKEY;
  ++s_snapshot.pairing_requests;
}

static void hid_input_payload(const uint8_t **report, uint16_t *length) {
  if (*report != NULL && *length > 0u && (*report)[0] == 0xa1u) {
    ++*report;
    --*length;
  }
}

static void handle_hid_event(const uint8_t *packet) {
  ++s_snapshot.hid_events;
  switch (hci_event_hid_meta_get_subevent_code(packet)) {
  case HID_SUBEVENT_INCOMING_CONNECTION: {
    bd_addr_t address;
    hid_subevent_incoming_connection_get_address(packet, address);
    const uint16_t hid_cid =
        hid_subevent_incoming_connection_get_hid_cid(packet);
    if (address_is_known(address) && s_identity_validated &&
        s_snapshot.connections > 0u && !s_logic.connected &&
        !s_logic.discovery_open &&
        s_snapshot.phase != JH_CLASSIC_HID_PHASE_CONNECTING) {
      const uint8_t status =
          hid_host_accept_connection(hid_cid, HID_PROTOCOL_MODE_BOOT);
      s_snapshot.last_btstack_status = status;
      if (status == ERROR_CODE_SUCCESS) {
        s_hid_cid = hid_cid;
        s_snapshot.protocol = JH_CLASSIC_HID_PROTOCOL_BOOT_FALLBACK;
        s_snapshot.phase = JH_CLASSIC_HID_PHASE_CONNECTING;
        ++s_snapshot.accepted_incoming_connections;
        break;
      }
    }
    (void)hid_host_decline_connection(hid_cid);
    ++s_snapshot.rejected_incoming_connections;
    break;
  }
  case HID_SUBEVENT_CONNECTION_OPENED: {
    const uint16_t hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
    const uint8_t status = hid_subevent_connection_opened_get_status(packet);
    s_snapshot.last_btstack_status = status;
    if (status != ERROR_CODE_SUCCESS) {
      if (hid_cid != s_hid_cid) {
        break;
      }
      ++s_snapshot.connection_failures;
      if (!s_identity_validated) {
        s_snapshot.known_device = false;
      }
      s_snapshot.phase = s_snapshot.known_device
                             ? JH_CLASSIC_HID_PHASE_KNOWN_IDLE
                             : JH_CLASSIC_HID_PHASE_READY;
      s_reconnect_pending = false;
      s_hid_cid = 0u;
      break;
    }
    bd_addr_t address;
    hid_subevent_connection_opened_get_bd_addr(packet, address);
    s_hid_cid = hid_cid;
    if (!address_is_known(address)) {
      hid_host_disconnect(s_hid_cid);
      ++s_snapshot.rejected_incoming_connections;
      break;
    }
    if (!s_identity_validated) {
      reject_connected_candidate();
      break;
    }
    finalize_hid_connection();
    break;
  }
  case HID_SUBEVENT_DESCRIPTOR_AVAILABLE: {
    const uint8_t status = hid_subevent_descriptor_available_get_status(packet);
    s_snapshot.last_btstack_status = status;
    if (status != ERROR_CODE_SUCCESS) {
      s_snapshot.descriptor_available =
          s_identity_validated && s_snapshot.descriptor_matches_capture;
      if (s_snapshot.descriptor_available) {
        const uint8_t protocol_status = hid_host_send_set_protocol_mode(
            s_hid_cid, HID_PROTOCOL_MODE_REPORT);
        s_snapshot.last_btstack_status = protocol_status;
      } else {
        s_snapshot.last_status = HAL_EPROTO;
        reject_connected_candidate();
      }
      break;
    }
    const uint16_t descriptor_length =
        hid_descriptor_storage_get_descriptor_len(s_hid_cid);
    const uint8_t *descriptor =
        hid_descriptor_storage_get_descriptor_data(s_hid_cid);
    s_snapshot.descriptor_available = descriptor != NULL;
    s_snapshot.descriptor_length = descriptor_length;
    if (descriptor_length > s_snapshot.descriptor_length_high_water) {
      s_snapshot.descriptor_length_high_water = descriptor_length;
    }
    s_snapshot.descriptor_hash =
        descriptor != NULL ? fnv1a32(descriptor, descriptor_length) : 0u;
    s_snapshot.descriptor_matches_capture =
        descriptor_length == JH_CLASSIC_HID_CAPTURE_DESCRIPTOR_LENGTH &&
        s_snapshot.descriptor_hash == JH_CLASSIC_HID_CAPTURE_DESCRIPTOR_HASH;
    if (!s_snapshot.descriptor_matches_capture) {
      s_snapshot.last_status = HAL_EPROTO;
      reject_connected_candidate();
      break;
    }
    if (!s_gamepad_parser.configured) {
      const hal_status_t parser_status = jh_bluetooth_gamepad_parser_configure(
          &s_gamepad_parser, descriptor, descriptor_length);
      s_snapshot.last_status = parser_status;
      if (parser_status != HAL_OK) {
        reject_connected_candidate();
        break;
      }
    }
    const hal_status_t parser_status = gamepad_connection_opened();
    if (parser_status != HAL_OK) {
      s_snapshot.last_status = parser_status;
      reject_connected_candidate();
      break;
    }
    jh_bluetooth_classic_hid_probe_logic_descriptor_accepted(&s_logic);
    break;
  }
  case HID_SUBEVENT_REPORT: {
    const uint8_t *report = hid_subevent_report_get_report(packet);
    uint16_t length = hid_subevent_report_get_report_len(packet);
    const uint16_t transport_length = length;
    hid_input_payload(&report, &length);
    const hal_status_t parser_status = jh_bluetooth_gamepad_parser_parse_input(
        &s_gamepad_parser, report, length);
    if (parser_status != HAL_OK) {
      s_snapshot.last_status = parser_status;
      sync_logic_snapshot();
      break;
    }
    jh_bluetooth_gamepad_snapshot_t snapshot;
    if (jh_bluetooth_gamepad_parser_snapshot(&s_gamepad_parser, &snapshot) !=
        HAL_OK) {
      s_snapshot.last_status = HAL_EINTERNAL;
      break;
    }
    (void)jh_bluetooth_classic_hid_probe_logic_report(&s_logic, &snapshot,
                                                      transport_length);
    drain_gamepad_queue();
    sync_logic_snapshot();
    break;
  }
  case HID_SUBEVENT_SET_PROTOCOL_RESPONSE:
    if (hid_subevent_set_protocol_response_get_handshake_status(packet) ==
        HID_HANDSHAKE_PARAM_TYPE_SUCCESSFUL) {
      const hid_protocol_mode_t mode = (hid_protocol_mode_t)
          hid_subevent_set_protocol_response_get_protocol_mode(packet);
      s_snapshot.protocol = mode == HID_PROTOCOL_MODE_BOOT
                                ? JH_CLASSIC_HID_PROTOCOL_BOOT_FALLBACK
                                : JH_CLASSIC_HID_PROTOCOL_REPORT;
    }
    break;
  case HID_SUBEVENT_CONNECTION_CLOSED:
    if (s_logic.connected) {
      s_connected_accumulated_ms += hal_millis() - s_connected_since_ms;
      ++s_snapshot.disconnections;
    }
    gamepad_connection_closed();
    jh_bluetooth_classic_hid_probe_logic_disconnected(&s_logic);
    sync_logic_snapshot();
    s_snapshot.descriptor_available = false;
    s_snapshot.phase = s_snapshot.known_device ? JH_CLASSIC_HID_PHASE_KNOWN_IDLE
                                               : JH_CLASSIC_HID_PHASE_READY;
    s_hid_cid = 0u;
    break;
  default:
    break;
  }
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
  (void)channel;
  if (packet_type != HCI_EVENT_PACKET || packet == NULL) {
    return;
  }

  const uint8_t event = hci_event_packet_get_type(packet);
  bd_addr_t address;
  switch (event) {
  case BTSTACK_EVENT_STATE:
    s_snapshot.controller_ready =
        btstack_event_state_get_state(packet) == HCI_STATE_WORKING;
    if (s_snapshot.controller_ready &&
        s_snapshot.phase == JH_CLASSIC_HID_PHASE_IDLE) {
      s_snapshot.phase = JH_CLASSIC_HID_PHASE_READY;
    }
    break;
  case GAP_EVENT_INQUIRY_RESULT:
    if (s_logic.discovery_open) {
      handle_inquiry_result(packet);
    }
    break;
  case GAP_EVENT_INQUIRY_COMPLETE:
    handle_inquiry_complete();
    break;
  case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
    if (s_snapshot.phase == JH_CLASSIC_HID_PHASE_REMOTE_NAME) {
      handle_remote_name(packet, size);
    }
    break;
  case HCI_EVENT_IO_CAPABILITY_RESPONSE:
    hci_event_io_capability_response_get_bd_addr(packet, address);
    if (address_is_known(address)) {
      s_snapshot.pairing_method = JH_CLASSIC_HID_PAIRING_SSP_JUST_WORKS;
    }
    break;
  case HCI_EVENT_USER_CONFIRMATION_REQUEST:
    hci_event_user_confirmation_request_get_bd_addr(packet, address);
    set_pairing_request(JH_CLASSIC_HID_PAIRING_SSP_JUST_WORKS, address);
    break;
  case HCI_EVENT_PIN_CODE_REQUEST:
    hci_event_pin_code_request_get_bd_addr(packet, address);
    set_pairing_request(JH_CLASSIC_HID_PAIRING_LEGACY_PIN_0000, address);
    break;
  case HCI_EVENT_USER_PASSKEY_REQUEST:
    hci_event_user_passkey_request_get_bd_addr(packet, address);
    set_pairing_request(JH_CLASSIC_HID_PAIRING_UNSUPPORTED_PASSKEY, address);
    if (address_is_known(address)) {
      (void)gap_ssp_passkey_negative(address);
    }
    break;
  case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
    if (hci_event_simple_pairing_complete_get_status(packet) !=
        ERROR_CODE_SUCCESS) {
      ++s_snapshot.authentication_failures;
    }
    break;
  case HCI_EVENT_AUTHENTICATION_COMPLETE:
    if (hci_event_authentication_complete_get_status(packet) ==
        ERROR_CODE_SUCCESS) {
      ++s_snapshot.authentication_successes;
    } else {
      ++s_snapshot.authentication_failures;
    }
    break;
  case HCI_EVENT_LINK_KEY_NOTIFICATION: {
    ++s_snapshot.link_keys_stored;
    /* Raw layout (no generated accessor for this event): bd_addr at [2..7]
     * (shared with LINK_KEY_REQUEST), link key at [8..23], type at [24]. See
     * BTstack's own hci.c HCI_EVENT_LINK_KEY_NOTIFICATION handling. Mirror
     * its CVE-2020-26555 guard: ignore an all-zero key. */
    bool key_is_null = true;
    for (size_t i = 0u; i < 16u; ++i) {
      if (packet[8u + i] != 0u) {
        key_is_null = false;
        break;
      }
    }
    if (!key_is_null) {
      bd_addr_t notified_address;
      hci_event_link_key_request_get_bd_addr(packet, notified_address);
      jh_bluetooth_classic_hid_probe_logic_link_key_received(
          &s_logic, notified_address, &packet[8], packet[24]);
    }
    break;
  }
  case HCI_EVENT_HID_META:
    handle_hid_event(packet);
    break;
  default:
    break;
  }
}

static hal_status_t link_key_db_start(void *context) {
  (void)context;
  hci_set_link_key_db(btstack_link_key_db_memory_instance());
  return HAL_OK;
}

static void link_key_db_stop(void *context) {
  (void)context;
  hci_set_link_key_db(NULL);
}

static hal_status_t sdp_client_start(void *context) {
  (void)context;
  sdp_client_init();
  return HAL_OK;
}

static void sdp_client_stop(void *context) {
  (void)context;
  sdp_client_deinit();
}

static hal_status_t hid_profile_start(void *context) {
  (void)context;
  hid_host_init(s_hid_descriptor, sizeof(s_hid_descriptor));
  return HAL_OK;
}

static void hid_profile_stop(void *context) {
  (void)context;
  (void)l2cap_unregister_service(PSM_HID_INTERRUPT);
  (void)l2cap_unregister_service(PSM_HID_CONTROL);
  hid_host_deinit();
}

static hal_status_t event_handler_start(void *context) {
  (void)context;
  memset(&s_hci_events, 0, sizeof(s_hci_events));
  s_hci_events.callback = packet_handler;
  hid_host_register_packet_handler(packet_handler);
  hci_add_event_handler(&s_hci_events);
  return HAL_OK;
}

static void event_handler_stop(void *context) {
  (void)context;
  hci_remove_event_handler(&s_hci_events);
  hid_host_register_packet_handler(NULL);
  memset(&s_hci_events, 0, sizeof(s_hci_events));
}

static const jh_bluetooth_classic_hid_lifecycle_ops_t s_lifecycle_ops = {
    .context = NULL,
    .link_key_db_start = link_key_db_start,
    .link_key_db_stop = link_key_db_stop,
    .sdp_client_start = sdp_client_start,
    .sdp_client_stop = sdp_client_stop,
    .hid_host_start = hid_profile_start,
    .hid_host_stop = hid_profile_stop,
    .event_handler_start = event_handler_start,
    .event_handler_stop = event_handler_stop,
};

static hal_status_t profile_start(void *context) {
  (void)context;
  const hal_status_t status =
      jh_bluetooth_classic_hid_lifecycle_start(&s_lifecycle, &s_lifecycle_ops);
  s_snapshot.profile_ready = status == HAL_OK;
  if (status == HAL_OK) {
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE |
                                         LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
    hci_set_master_slave_policy(HCI_ROLE_MASTER);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_authentication_requirement(
        SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_DEDICATED_BONDING);
    gap_ssp_set_auto_accept(0);
  }
  return status;
}

static void profile_stop(void *context) {
  (void)context;
  s_connect_pending = false;
  if (s_logic.discovery_open) {
    (void)gap_inquiry_stop();
  }
  jh_bluetooth_classic_hid_lifecycle_stop(&s_lifecycle, &s_lifecycle_ops);
  s_snapshot.profile_ready = false;
  s_snapshot.controller_ready = false;
}

static hal_status_t profile_service(void *context) {
  (void)context;
  return HAL_OK;
}

static void profile_invalidated(void *context, uint32_t generation) {
  (void)context;
  (void)generation;
  gamepad_connection_closed();
  jh_bluetooth_classic_hid_probe_logic_disconnected(&s_logic);
  sync_logic_snapshot();
  s_snapshot.controller_ready = false;
  s_snapshot.profile_ready = false;
  s_snapshot.last_status = HAL_EHW;
  s_snapshot.transport_status = HAL_EHW;
  s_snapshot.phase = JH_CLASSIC_HID_PHASE_IDLE;
  s_connect_pending = false;
}

static const jh_bluetooth_host_profile_ops_t s_profile_ops = {
    .context = NULL,
    .start = profile_start,
    .stop = profile_stop,
    .service = profile_service,
    .invalidated = profile_invalidated,
};

/* Restore a previously bonded peer from the provider, if any, and reinstall
 * its link key into the controller's (RAM-only) link key database. Called
 * once at start(), from application context -- never from a stack callback,
 * never under the radio lock. */
static void restore_bond_from_provider(void) {
  if (s_bond_provider == NULL || s_bond_provider->load == NULL) {
    return;
  }
  hal_gamepad_bond_blob_t blob;
  const hal_status_t load_status =
      s_bond_provider->load(s_bond_provider->context, &blob);
  if (load_status != HAL_OK) {
    /* HAL_ENOENT (nothing stored yet) or an I/O error: no known device. */
    return;
  }
  jh_gamepad_bond_identity_t identity;
  uint32_t sequence = 0u;
  if (jh_gamepad_bond_decode(&blob, &identity, &sequence) != HAL_OK) {
    /* Structurally invalid, or written under stale verification rules:
     * treat exactly like "no bond" rather than trusting a mismatched peer. */
    return;
  }
  memcpy(s_known_address, identity.bd_addr, sizeof(s_known_address));
  btstack_link_key_db_memory_instance()->put_link_key(
      s_known_address, identity.link_key,
      (link_key_type_t)identity.link_key_type);
  s_snapshot.known_device = true;
  s_bond_sequence = sequence;
}

hal_status_t jh_bluetooth_classic_hid_probe_start(
    const hal_gamepad_bond_provider_t *bond_provider) {
  if (s_snapshot.started) {
    return HAL_ESTATE;
  }
  memset(&s_snapshot, 0, sizeof(s_snapshot));
  memset(&s_lifecycle, 0, sizeof(s_lifecycle));
  memset(&s_host_reference, 0, sizeof(s_host_reference));
  memset(s_hid_descriptor, 0, sizeof(s_hid_descriptor));
  memset(s_known_address, 0, sizeof(s_known_address));
  memset(s_pairing_address, 0, sizeof(s_pairing_address));
  s_hid_cid = 0u;
  s_connected_since_ms = 0u;
  s_connected_accumulated_ms = 0u;
  s_connect_at_ms = 0u;
  s_reconnect_pending = false;
  s_connect_pending = false;
  s_identity_validated = false;
  s_bond_provider = bond_provider;
  s_bond_sequence = 0u;
  reset_candidate();
  jh_bluetooth_gamepad_parser_init(&s_gamepad_parser);
  jh_bluetooth_classic_hid_probe_logic_reset(&s_logic);
  jh_bluetooth_classic_hid_memory_probe_reset();
  s_snapshot.phase = JH_CLASSIC_HID_PHASE_IDLE;
  const hal_status_t status = jh_btstack_host_acquire(
      JH_BLUETOOTH_HOST_PROFILE_CLASSIC_HID, &s_profile_ops, &s_host_reference);
  s_snapshot.started = status == HAL_OK;
  s_snapshot.last_status = status;
  if (status == HAL_OK) {
    restore_bond_from_provider();
  }
  return status;
}

/* Encode and persist a bond staged by the stack callbacks, if one became
 * ready to commit. Called only from service(), after jh_btstack_host_service
 * has returned -- i.e. outside any stack callback and after the radio lock
 * has been released */
static void flush_pending_bond(void) {
  const jh_gamepad_bond_identity_t *identity =
      jh_bluetooth_classic_hid_probe_logic_take_pending_bond(&s_logic);
  if (identity == NULL) {
    return;
  }
  /* Track the newly bonded peer in RAM regardless of persistence -- this may
   * be replacing a different, previously known device. */
  memcpy(s_known_address, identity->bd_addr, sizeof(s_known_address));
  s_snapshot.known_device = true;
  if (s_bond_provider == NULL || s_bond_provider->store == NULL) {
    return;
  }
  hal_gamepad_bond_blob_t blob;
  if (jh_gamepad_bond_encode(identity, s_bond_sequence + 1u, &blob) != HAL_OK) {
    return;
  }
  if (s_bond_provider->store(s_bond_provider->context, &blob) == HAL_OK) {
    ++s_bond_sequence;
  }
}

hal_status_t jh_bluetooth_classic_hid_probe_service(void) {
  if (!s_snapshot.started) {
    return HAL_EUNINIT;
  }
  const hal_status_t status = jh_btstack_host_service(&s_host_reference);
  s_snapshot.last_status = status;
  flush_pending_bond();

  const uint32_t now = hal_millis();
  if (s_connect_pending && (int32_t)(now - s_connect_at_ms) >= 0) {
    s_connect_pending = false;
    (void)connect_candidate(false);
  }

  if (jh_bluetooth_classic_hid_probe_logic_discovery_expired(&s_logic, now)) {
    jh_bluetooth_classic_hid_probe_logic_close_discovery(&s_logic);
    if (s_snapshot.phase == JH_CLASSIC_HID_PHASE_INQUIRY) {
      (void)gap_inquiry_stop();
    }
    reset_candidate();
    s_snapshot.phase = s_snapshot.known_device ? JH_CLASSIC_HID_PHASE_KNOWN_IDLE
                                               : JH_CLASSIC_HID_PHASE_READY;
  }
  sync_logic_snapshot();

  jh_btstack_cyw43_transport_snapshot_t transport;
  jh_btstack_cyw43_transport_snapshot(&transport);
  s_snapshot.rx_packets = transport.rx_packets;
  s_snapshot.rx_event_packets = transport.rx_event_packets;
  s_snapshot.rx_acl_packets = transport.rx_acl_packets;
  s_snapshot.tx_packets = transport.tx_packets;
  s_snapshot.tx_command_packets = transport.tx_command_packets;
  s_snapshot.tx_acl_packets = transport.tx_acl_packets;
  s_snapshot.drain_budget_hits = transport.drain_budget_hits;
  s_snapshot.transport_status = transport.last_status;
  return status;
}

hal_status_t jh_bluetooth_classic_hid_probe_open_pairing_window(void) {
  if (!s_snapshot.started || !s_snapshot.controller_ready ||
      !s_snapshot.profile_ready) {
    return HAL_EUNINIT;
  }
  if ((s_snapshot.phase != JH_CLASSIC_HID_PHASE_READY &&
       s_snapshot.phase != JH_CLASSIC_HID_PHASE_KNOWN_IDLE) ||
      s_connect_pending) {
    return HAL_ESTATE;
  }
  if (!jh_bluetooth_classic_hid_probe_logic_open_discovery(&s_logic,
                                                           hal_millis())) {
    return HAL_ESTATE;
  }
  reset_candidate();
  sync_logic_snapshot();
  start_inquiry_cycle();
  return s_logic.discovery_open ? HAL_OK : HAL_EIO;
}

hal_status_t jh_bluetooth_classic_hid_probe_authorize_pairing(void) {
  if (!s_snapshot.pairing_pending) {
    return HAL_ESTATE;
  }
  int status = ERROR_CODE_COMMAND_DISALLOWED;
  switch (s_snapshot.pairing_method) {
  case JH_CLASSIC_HID_PAIRING_SSP_JUST_WORKS:
    status = gap_ssp_confirmation_response(s_pairing_address);
    break;
  case JH_CLASSIC_HID_PAIRING_LEGACY_PIN_0000:
    status = gap_pin_code_response(s_pairing_address, "0000");
    break;
  default:
    return HAL_EUNSUPPORTED;
  }
  s_snapshot.last_btstack_status = (uint8_t)status;
  if (status != ERROR_CODE_SUCCESS) {
    return HAL_EIO;
  }
  s_snapshot.pairing_pending = false;
  ++s_snapshot.pairing_authorizations;
  return HAL_OK;
}

hal_status_t jh_bluetooth_classic_hid_probe_reconnect(void) {
  if (!s_snapshot.started || !s_snapshot.controller_ready ||
      !s_snapshot.known_device) {
    return HAL_EUNINIT;
  }
  if (s_logic.connected ||
      s_snapshot.phase == JH_CLASSIC_HID_PHASE_CONNECTING ||
      s_logic.discovery_open) {
    return HAL_ESTATE;
  }
  memcpy(s_candidate_address, s_known_address, sizeof(s_candidate_address));
  s_candidate_available = true;
  return connect_candidate(true);
}

hal_status_t jh_bluetooth_classic_hid_probe_disconnect(void) {
  if (!s_snapshot.started || !s_logic.connected || s_hid_cid == 0u) {
    return HAL_ESTATE;
  }
  hid_host_disconnect(s_hid_cid);
  return HAL_OK;
}

hal_status_t jh_bluetooth_classic_hid_probe_forget(void) {
  if (!s_snapshot.started) {
    return HAL_EUNINIT;
  }
  if (s_logic.connected && s_hid_cid != 0u) {
    hid_host_disconnect(s_hid_cid);
  }
  bd_addr_t forgotten_address;
  memcpy(forgotten_address, s_known_address, sizeof(forgotten_address));
  memset(s_known_address, 0, sizeof(s_known_address));
  s_snapshot.known_device = false;
  s_identity_validated = false;
  s_bond_sequence = 0u;
  jh_bluetooth_classic_hid_probe_logic_reset_bond_progress(&s_logic);
  btstack_link_key_db_memory_instance()->delete_link_key(forgotten_address);

  if (s_bond_provider == NULL || s_bond_provider->erase == NULL) {
    return HAL_OK;
  }
  return s_bond_provider->erase(s_bond_provider->context);
}

hal_status_t jh_bluetooth_classic_hid_probe_stop(void) {
  if (!s_snapshot.started) {
    return HAL_EUNINIT;
  }
  const hal_status_t status = jh_btstack_host_release(&s_host_reference);
  s_snapshot.started = false;
  s_connect_pending = false;
  s_snapshot.last_status = status;
  s_snapshot.phase = JH_CLASSIC_HID_PHASE_IDLE;
  return status;
}

void jh_bluetooth_classic_hid_probe_retain_gamepad_queue(bool retain) {
  s_retain_gamepad_queue = retain;
  if (!retain) {
    drain_gamepad_queue();
  }
}

hal_status_t jh_bluetooth_classic_hid_probe_gamepad_snapshot(
    jh_bluetooth_gamepad_snapshot_t *out_snapshot) {
  return jh_bluetooth_gamepad_parser_snapshot(&s_gamepad_parser, out_snapshot);
}

hal_status_t jh_bluetooth_classic_hid_probe_gamepad_next(
    jh_bluetooth_gamepad_snapshot_t *out_snapshot) {
  return jh_bluetooth_gamepad_parser_next(&s_gamepad_parser, out_snapshot);
}

size_t jh_bluetooth_classic_hid_probe_gamepad_pending(void) {
  return s_gamepad_parser.queue_count;
}

void jh_bluetooth_classic_hid_probe_snapshot(
    jh_bluetooth_classic_hid_probe_snapshot_t *out_snapshot) {
  if (out_snapshot == NULL) {
    return;
  }
  sync_logic_snapshot();
  s_snapshot.connected_ms = s_connected_accumulated_ms;
  if (s_logic.connected) {
    s_snapshot.connected_ms += hal_millis() - s_connected_since_ms;
  }
  jh_bluetooth_classic_hid_memory_probe_snapshot(&s_snapshot.pools);
  *out_snapshot = s_snapshot;
}

const char *jh_bluetooth_classic_hid_probe_btstack_version(void) {
  return BTSTACK_VERSION_STRING;
}
