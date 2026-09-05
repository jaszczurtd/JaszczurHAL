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
} jh_bluetooth_classic_hid_pool_snapshot_t;

/** Allocation snapshot for pools used by the Classic HID/gamepad profile. */
typedef struct {
  jh_bluetooth_classic_hid_pool_snapshot_t hci_connections;
  jh_bluetooth_classic_hid_pool_snapshot_t l2cap_services;
  jh_bluetooth_classic_hid_pool_snapshot_t l2cap_channels;
  jh_bluetooth_classic_hid_pool_snapshot_t link_keys;
  jh_bluetooth_classic_hid_pool_snapshot_t hid_connections;
} jh_bluetooth_classic_hid_memory_snapshot_t;

/** @brief Reset Classic HID/gamepad allocation counters and peak levels. */
void jh_bluetooth_classic_hid_memory_probe_reset(void);

/**
 * @brief Copy current Classic HID/gamepad allocation diagnostics.
 * @param out_snapshot Receives the snapshot; ignored when NULL.
 */
void jh_bluetooth_classic_hid_memory_probe_snapshot(
    jh_bluetooth_classic_hid_memory_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
