#pragma once

#include "hal/core/hal_status.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *context;
  hal_status_t (*link_key_db_start)(void *context);
  void (*link_key_db_stop)(void *context);
  hal_status_t (*sdp_client_start)(void *context);
  void (*sdp_client_stop)(void *context);
  hal_status_t (*hid_host_start)(void *context);
  void (*hid_host_stop)(void *context);
  hal_status_t (*event_handler_start)(void *context);
  void (*event_handler_stop)(void *context);
} jh_bluetooth_classic_hid_lifecycle_ops_t;

typedef struct {
  bool link_key_db_started;
  bool sdp_client_started;
  bool hid_host_started;
  bool event_handler_started;
} jh_bluetooth_classic_hid_lifecycle_t;

hal_status_t jh_bluetooth_classic_hid_lifecycle_start(
    jh_bluetooth_classic_hid_lifecycle_t *lifecycle,
    const jh_bluetooth_classic_hid_lifecycle_ops_t *ops);
void jh_bluetooth_classic_hid_lifecycle_stop(
    jh_bluetooth_classic_hid_lifecycle_t *lifecycle,
    const jh_bluetooth_classic_hid_lifecycle_ops_t *ops);

#ifdef __cplusplus
}
#endif
