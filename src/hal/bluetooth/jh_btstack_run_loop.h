#pragma once

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  hal_status_t last_status;
  uint32_t service_calls;
  uint32_t notifications;
  uint32_t coalesced_notifications;
  bool initialized;
  bool poll_pending;
  bool in_service;
} jh_btstack_run_loop_snapshot_t;

hal_status_t jh_btstack_run_loop_init(void);
void jh_btstack_run_loop_deinit(void);
hal_status_t jh_btstack_run_loop_service_once(void *context);
void jh_btstack_run_loop_notify(void);
void jh_btstack_run_loop_invalidate(void *context, uint32_t generation);

#ifdef __cplusplus
}
#endif
