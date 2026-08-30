#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t current;
  uint8_t high_water;
  uint8_t capacity;
  uint8_t allocation_failures;
} jh_bluetooth_classic_hid_pool_snapshot_t;

typedef struct {
  jh_bluetooth_classic_hid_pool_snapshot_t l2cap_services;
  jh_bluetooth_classic_hid_pool_snapshot_t l2cap_channels;
  jh_bluetooth_classic_hid_pool_snapshot_t link_keys;
  jh_bluetooth_classic_hid_pool_snapshot_t hid_connections;
} jh_bluetooth_classic_hid_memory_snapshot_t;

void jh_bluetooth_classic_hid_memory_probe_reset(void);
void jh_bluetooth_classic_hid_memory_probe_snapshot(
    jh_bluetooth_classic_hid_memory_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
