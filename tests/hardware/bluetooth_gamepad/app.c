#include <hal/bluetooth/jh_bluetooth_classic_hid_probe.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <tools_c.h>

#include <pico/version.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  JHBT5_COMMAND_CAPACITY = 32u,
  JHBT5_SNAPSHOT_CAPACITY = 2300u,
  JHBT5_REPORT_PERIOD_MS = 1000u,
};

static char s_command[JHBT5_COMMAND_CAPACITY];
static size_t s_command_length;
static uint32_t s_last_report_ms;
static hal_status_t s_start_status = HAL_NONE;

static void print_ack(const char *command, hal_status_t status) {
  char line[128];
  const int length = snprintf(
      line, sizeof(line), "JHBT5-ACK {\"command\":\"%s\",\"status\":\"%s\"}",
      command, hal_status_to_string(status));
  if (length > 0 && (size_t)length < sizeof(line)) {
    hal_serial_println(line);
  }
}

static void report_snapshot(void) {
  jh_bluetooth_classic_hid_probe_snapshot_t snapshot = {0};
  jh_bluetooth_classic_hid_probe_snapshot(&snapshot);
  char line[JHBT5_SNAPSHOT_CAPACITY];
  const int length = snprintf(
      line, sizeof(line),
      "JHBT5-SNAPSHOT {\"schemaVersion\":1,\"target\":\"%s\","
      "\"board\":\"%s\",\"btstackVersion\":\"%s\","
      "\"picoSdkVersion\":\"%s\",\"phase\":%u,\"pairingMethod\":%u,"
      "\"protocol\":%u,\"started\":%u,\"controllerReady\":%u,"
      "\"profileReady\":%u,\"discoveryOpen\":%u,\"pairingPending\":%u,"
      "\"knownDevice\":%u,\"connected\":%u,\"descriptorAvailable\":%u,"
      "\"descriptorMatchesCapture\":%u,\"descriptorLength\":%u,"
      "\"descriptorLengthHighWater\":%u,\"descriptorHash\":%lu,"
      "\"reportLengthHighWater\":%u,\"activeControlsMask\":%u,"
      "\"seenControlsMask\":%u,\"connectedMs\":%lu,\"hidEvents\":%lu,"
      "\"inquiryCycles\":%lu,\"inquiryResults\":%lu,"
      "\"peripheralCandidates\":%lu,\"identityRejections\":%lu,"
      "\"sdpHidMatches\":%lu,\"pnpIdentityMatches\":%lu,"
      "\"pnpServiceRecords\":%lu,\"pnpRecordHandle\":%lu,"
      "\"pairingRequests\":%lu,\"pairingAuthorizations\":%lu,"
      "\"authenticationSuccesses\":%lu,\"authenticationFailures\":%lu,"
      "\"linkKeysStored\":%lu,\"connections\":%lu,"
      "\"connectionFailures\":%lu,\"disconnections\":%lu,"
      "\"reconnectAttempts\":%lu,\"reconnectSuccesses\":%lu,"
      "\"acceptedIncomingConnections\":%lu,"
      "\"rejectedIncomingConnections\":%lu,\"reports\":%lu,"
      "\"reportBytes\":%lu,\"invalidReports\":%lu,"
      "\"releaseAllEvents\":%lu,\"lastBtstackStatus\":%u,"
      "\"lastStatus\":%d,\"transportStatus\":%d,"
      "\"parser\":{\"descriptorLimit\":%u,\"reportLimit\":%u,"
      "\"queueCapacity\":%u,\"descriptorsAccepted\":%lu,"
      "\"descriptorsRejected\":%lu,\"reportsReceived\":%lu,"
      "\"reportsAccepted\":%lu,\"reportsRejected\":%lu,"
      "\"duplicateReports\":%lu,\"stateChanges\":%lu,"
      "\"ignoredUsages\":%lu,\"unknownReportIds\":%lu,"
      "\"truncatedReports\":%lu,\"droppedSnapshots\":%lu,"
      "\"reportBytes\":%lu,\"descriptorLengthHighWater\":%u,"
      "\"reportLengthHighWater\":%u,\"queueHighWater\":%u,"
      "\"lastStatus\":%d,\"lastRejectReason\":%u},"
      "\"transport\":{\"rx\":%lu,\"rxEvents\":%lu,\"rxAcl\":%lu,"
      "\"tx\":%lu,\"txCommands\":%lu,\"txAcl\":%lu,"
      "\"drainBudgetHits\":%lu},"
      "\"pools\":{\"l2capServices\":[%u,%u,%u,%u],"
      "\"l2capChannels\":[%u,%u,%u,%u],\"linkKeys\":[%u,%u,%u,%u],"
      "\"hidConnections\":[%u,%u,%u,%u]}}",
      HAL_TARGET_NAME, HAL_BOARD_PROFILE_NAME,
      jh_bluetooth_classic_hid_probe_btstack_version(), PICO_SDK_VERSION_STRING,
      (unsigned)snapshot.phase, (unsigned)snapshot.pairing_method,
      (unsigned)snapshot.protocol, snapshot.started ? 1u : 0u,
      snapshot.controller_ready ? 1u : 0u, snapshot.profile_ready ? 1u : 0u,
      snapshot.discovery_open ? 1u : 0u, snapshot.pairing_pending ? 1u : 0u,
      snapshot.known_device ? 1u : 0u, snapshot.connected ? 1u : 0u,
      snapshot.descriptor_available ? 1u : 0u,
      snapshot.descriptor_matches_capture ? 1u : 0u,
      (unsigned)snapshot.descriptor_length,
      (unsigned)snapshot.descriptor_length_high_water,
      (unsigned long)snapshot.descriptor_hash,
      (unsigned)snapshot.report_length_high_water,
      (unsigned)snapshot.active_controls_mask,
      (unsigned)snapshot.seen_controls_mask,
      (unsigned long)snapshot.connected_ms, (unsigned long)snapshot.hid_events,
      (unsigned long)snapshot.inquiry_cycles,
      (unsigned long)snapshot.inquiry_results,
      (unsigned long)snapshot.peripheral_candidates,
      (unsigned long)snapshot.identity_rejections,
      (unsigned long)snapshot.sdp_hid_matches,
      (unsigned long)snapshot.pnp_identity_matches,
      (unsigned long)snapshot.pnp_service_records,
      (unsigned long)snapshot.pnp_record_handle,
      (unsigned long)snapshot.pairing_requests,
      (unsigned long)snapshot.pairing_authorizations,
      (unsigned long)snapshot.authentication_successes,
      (unsigned long)snapshot.authentication_failures,
      (unsigned long)snapshot.link_keys_stored,
      (unsigned long)snapshot.connections,
      (unsigned long)snapshot.connection_failures,
      (unsigned long)snapshot.disconnections,
      (unsigned long)snapshot.reconnect_attempts,
      (unsigned long)snapshot.reconnect_successes,
      (unsigned long)snapshot.accepted_incoming_connections,
      (unsigned long)snapshot.rejected_incoming_connections,
      (unsigned long)snapshot.reports, (unsigned long)snapshot.report_bytes,
      (unsigned long)snapshot.invalid_reports,
      (unsigned long)snapshot.release_all_events,
      (unsigned)snapshot.last_btstack_status, (int)snapshot.last_status,
      (int)snapshot.transport_status,
      (unsigned)JH_BLUETOOTH_GAMEPAD_DESCRIPTOR_MAX,
      (unsigned)JH_BLUETOOTH_GAMEPAD_REPORT_MAX,
      (unsigned)JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY,
      (unsigned long)snapshot.parser.descriptors_accepted,
      (unsigned long)snapshot.parser.descriptors_rejected,
      (unsigned long)snapshot.parser.reports_received,
      (unsigned long)snapshot.parser.reports_accepted,
      (unsigned long)snapshot.parser.reports_rejected,
      (unsigned long)snapshot.parser.duplicate_reports,
      (unsigned long)snapshot.parser.state_changes,
      (unsigned long)snapshot.parser.ignored_usages,
      (unsigned long)snapshot.parser.unknown_report_ids,
      (unsigned long)snapshot.parser.truncated_reports,
      (unsigned long)snapshot.parser.dropped_snapshots,
      (unsigned long)snapshot.parser.report_bytes,
      (unsigned)snapshot.parser.descriptor_length_high_water,
      (unsigned)snapshot.parser.report_length_high_water,
      (unsigned)snapshot.parser.queue_high_water,
      (int)snapshot.parser.last_status,
      (unsigned)snapshot.parser.last_reject_reason,
      (unsigned long)snapshot.rx_packets,
      (unsigned long)snapshot.rx_event_packets,
      (unsigned long)snapshot.rx_acl_packets,
      (unsigned long)snapshot.tx_packets,
      (unsigned long)snapshot.tx_command_packets,
      (unsigned long)snapshot.tx_acl_packets,
      (unsigned long)snapshot.drain_budget_hits,
      (unsigned)snapshot.pools.l2cap_services.current,
      (unsigned)snapshot.pools.l2cap_services.high_water,
      (unsigned)snapshot.pools.l2cap_services.capacity,
      (unsigned)snapshot.pools.l2cap_services.allocation_failures,
      (unsigned)snapshot.pools.l2cap_channels.current,
      (unsigned)snapshot.pools.l2cap_channels.high_water,
      (unsigned)snapshot.pools.l2cap_channels.capacity,
      (unsigned)snapshot.pools.l2cap_channels.allocation_failures,
      (unsigned)snapshot.pools.link_keys.current,
      (unsigned)snapshot.pools.link_keys.high_water,
      (unsigned)snapshot.pools.link_keys.capacity,
      (unsigned)snapshot.pools.link_keys.allocation_failures,
      (unsigned)snapshot.pools.hid_connections.current,
      (unsigned)snapshot.pools.hid_connections.high_water,
      (unsigned)snapshot.pools.hid_connections.capacity,
      (unsigned)snapshot.pools.hid_connections.allocation_failures);
  if (length <= 0 || (size_t)length >= sizeof(line)) {
    hal_serial_println("JHBT5-ERROR snapshot-overflow");
    return;
  }
  hal_serial_println(line);
}

static void execute_command(void) {
  s_command[s_command_length] = '\0';
  hal_status_t status = HAL_EINVAL;
  if (strcmp(s_command, "DISCOVER") == 0) {
    status = jh_bluetooth_classic_hid_probe_open_pairing_window();
  } else if (strcmp(s_command, "AUTHORIZE") == 0) {
    status = jh_bluetooth_classic_hid_probe_authorize_pairing();
  } else if (strcmp(s_command, "RECONNECT") == 0) {
    status = jh_bluetooth_classic_hid_probe_reconnect();
  } else if (strcmp(s_command, "DISCONNECT") == 0) {
    status = jh_bluetooth_classic_hid_probe_disconnect();
  } else if (strcmp(s_command, "SNAPSHOT") == 0) {
    status = HAL_OK;
  }
  print_ack(s_command, status);
  report_snapshot();
  s_command_length = 0u;
}

static void service_commands(void) {
  while (hal_serial_available() > 0) {
    const int value = hal_serial_read();
    if (value < 0) {
      return;
    }
    if (value == '\r') {
      continue;
    }
    if (value == '\n') {
      if (s_command_length > 0u) {
        execute_command();
      }
      continue;
    }
    if (s_command_length + 1u >= sizeof(s_command)) {
      s_command_length = 0u;
      print_ack("OVERFLOW", HAL_EOVERFLOW);
      continue;
    }
    s_command[s_command_length++] = (char)value;
  }
}

void app_start(void) {
  hal_debug_init_default();
  s_start_status = jh_bluetooth_classic_hid_probe_start(NULL);
  report_snapshot();
}

void app_task0(void) {
  if (s_start_status == HAL_OK) {
    const hal_status_t status = jh_bluetooth_classic_hid_probe_service();
    if (status != HAL_OK) {
      s_start_status = status;
    }
  }
  service_commands();

  const uint32_t now = hal_millis();
  if (now - s_last_report_ms >= JHBT5_REPORT_PERIOD_MS) {
    s_last_report_ms = now;
    report_snapshot();
  }
  hal_delay_ms(1u);
}
