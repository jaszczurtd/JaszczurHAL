#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool started;
  bool controller_ready;
  bool advertising;
  bool connected;
  uint32_t connection_count;
  uint32_t writes_received;
  uint32_t rx_packets;
  uint32_t tx_packets;
  uint32_t drain_budget_hits;
  hal_status_t last_status;
  hal_status_t transport_status;
} jh_bluetooth_stage1_snapshot_t;

hal_status_t jh_bluetooth_stage1_start(void);
hal_status_t jh_bluetooth_stage1_service(void);
void jh_bluetooth_stage1_snapshot(jh_bluetooth_stage1_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
