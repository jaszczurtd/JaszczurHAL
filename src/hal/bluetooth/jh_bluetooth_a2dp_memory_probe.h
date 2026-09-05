#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Current and peak allocation use for one fixed BTstack object pool. */
typedef struct {
  uint8_t current;
  uint8_t high_water;
  uint8_t capacity;
  uint8_t allocation_failures;
} jh_bluetooth_a2dp_pool_snapshot_t;

/** Allocation snapshot for pools used by the A2DP/AVRCP profiles. */
typedef struct {
  jh_bluetooth_a2dp_pool_snapshot_t hci_connections;
  jh_bluetooth_a2dp_pool_snapshot_t l2cap_services;
  jh_bluetooth_a2dp_pool_snapshot_t l2cap_channels;
  jh_bluetooth_a2dp_pool_snapshot_t link_keys;
  jh_bluetooth_a2dp_pool_snapshot_t service_records;
  jh_bluetooth_a2dp_pool_snapshot_t avdtp_endpoints;
  jh_bluetooth_a2dp_pool_snapshot_t avdtp_connections;
  jh_bluetooth_a2dp_pool_snapshot_t avrcp_connections;
} jh_bluetooth_a2dp_memory_snapshot_t;

/** @brief Reset A2DP/AVRCP allocation counters and peak levels. */
void jh_bluetooth_a2dp_memory_probe_reset(void);

/**
 * @brief Copy current A2DP/AVRCP allocation diagnostics.
 * @param out_snapshot Receives the snapshot; ignored when NULL.
 */
void jh_bluetooth_a2dp_memory_probe_snapshot(
    jh_bluetooth_a2dp_memory_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
