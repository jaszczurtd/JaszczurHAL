#pragma once

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t hid_events;
  uint32_t rejected_incoming_connections;
  uint32_t rx_packets;
  uint32_t rx_event_packets;
  uint32_t rx_acl_packets;
  uint32_t tx_packets;
  uint32_t tx_command_packets;
  uint32_t tx_acl_packets;
  uint32_t drain_budget_hits;
  hal_status_t last_status;
  hal_status_t transport_status;
  bool started;
  bool controller_ready;
  bool profile_ready;
} jh_bluetooth_classic_hid_probe_snapshot_t;

hal_status_t jh_bluetooth_classic_hid_probe_start(void);
hal_status_t jh_bluetooth_classic_hid_probe_service(void);
hal_status_t jh_bluetooth_classic_hid_probe_stop(void);
void jh_bluetooth_classic_hid_probe_snapshot(
    jh_bluetooth_classic_hid_probe_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
