#pragma once

#include "hal/core/hal_status.h"
#include "jh_bluetooth_classic_hid_memory_probe.h"
#include "jh_bluetooth_gamepad_parser.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_CLASSIC_HID_PHASE_IDLE = 0,
  JH_CLASSIC_HID_PHASE_READY,
  JH_CLASSIC_HID_PHASE_INQUIRY,
  JH_CLASSIC_HID_PHASE_REMOTE_NAME,
  JH_CLASSIC_HID_PHASE_SDP_HID,
  JH_CLASSIC_HID_PHASE_SDP_PNP,
  JH_CLASSIC_HID_PHASE_CONNECTING,
  JH_CLASSIC_HID_PHASE_CONNECTED,
  JH_CLASSIC_HID_PHASE_KNOWN_IDLE,
} jh_bluetooth_classic_hid_phase_t;

typedef enum {
  JH_CLASSIC_HID_PAIRING_NONE = 0,
  JH_CLASSIC_HID_PAIRING_SSP_JUST_WORKS,
  JH_CLASSIC_HID_PAIRING_LEGACY_PIN_0000,
  JH_CLASSIC_HID_PAIRING_UNSUPPORTED_PASSKEY,
} jh_bluetooth_classic_hid_pairing_method_t;

typedef enum {
  JH_CLASSIC_HID_PROTOCOL_UNKNOWN = 0,
  JH_CLASSIC_HID_PROTOCOL_REPORT,
  JH_CLASSIC_HID_PROTOCOL_BOOT_FALLBACK,
} jh_bluetooth_classic_hid_protocol_t;

typedef struct {
  jh_bluetooth_classic_hid_memory_snapshot_t pools;
  jh_bluetooth_gamepad_parser_diagnostics_t parser;
  uint32_t hid_events;
  uint32_t rejected_incoming_connections;
  uint32_t accepted_incoming_connections;
  uint32_t inquiry_cycles;
  uint32_t inquiry_results;
  uint32_t peripheral_candidates;
  uint32_t identity_rejections;
  uint32_t sdp_hid_matches;
  uint32_t pnp_identity_matches;
  uint32_t pnp_service_records;
  uint32_t pnp_record_handle;
  uint32_t pairing_requests;
  uint32_t pairing_authorizations;
  uint32_t authentication_successes;
  uint32_t authentication_failures;
  uint32_t link_keys_stored;
  uint32_t connections;
  uint32_t connection_failures;
  uint32_t disconnections;
  uint32_t reconnect_attempts;
  uint32_t reconnect_successes;
  uint32_t reports;
  uint32_t report_bytes;
  uint32_t invalid_reports;
  uint32_t release_all_events;
  uint32_t connected_ms;
  uint32_t descriptor_hash;
  uint32_t rx_packets;
  uint32_t rx_event_packets;
  uint32_t rx_acl_packets;
  uint32_t tx_packets;
  uint32_t tx_command_packets;
  uint32_t tx_acl_packets;
  uint32_t drain_budget_hits;
  uint16_t descriptor_length;
  uint16_t descriptor_length_high_water;
  uint16_t report_length_high_water;
  uint16_t active_controls_mask;
  uint16_t seen_controls_mask;
  uint8_t last_btstack_status;
  hal_status_t last_status;
  hal_status_t transport_status;
  jh_bluetooth_classic_hid_phase_t phase;
  jh_bluetooth_classic_hid_pairing_method_t pairing_method;
  jh_bluetooth_classic_hid_protocol_t protocol;
  bool started;
  bool controller_ready;
  bool profile_ready;
  bool discovery_open;
  bool pairing_pending;
  bool known_device;
  bool connected;
  bool descriptor_available;
  bool descriptor_matches_capture;
} jh_bluetooth_classic_hid_probe_snapshot_t;

hal_status_t jh_bluetooth_classic_hid_probe_start(void);
hal_status_t jh_bluetooth_classic_hid_probe_service(void);
hal_status_t jh_bluetooth_classic_hid_probe_open_pairing_window(void);
hal_status_t jh_bluetooth_classic_hid_probe_authorize_pairing(void);
hal_status_t jh_bluetooth_classic_hid_probe_reconnect(void);
hal_status_t jh_bluetooth_classic_hid_probe_disconnect(void);
hal_status_t jh_bluetooth_classic_hid_probe_stop(void);
void jh_bluetooth_classic_hid_probe_snapshot(
    jh_bluetooth_classic_hid_probe_snapshot_t *out_snapshot);
const char *jh_bluetooth_classic_hid_probe_btstack_version(void);

#ifdef __cplusplus
}
#endif
