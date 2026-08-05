#include "jh_btstack_run_loop.h"

#include <stddef.h>

#include "btstack_run_loop.h"
#include "btstack_run_loop_embedded.h"

static jh_btstack_run_loop_snapshot_t s_snapshot = {
    .last_status = HAL_NONE,
};

hal_status_t jh_btstack_run_loop_init(void) {
  if (s_snapshot.initialized) {
    return HAL_OK;
  }
  btstack_run_loop_init(btstack_run_loop_embedded_get_instance());
  s_snapshot.initialized = true;
  s_snapshot.last_status = HAL_OK;
  return HAL_OK;
}

hal_status_t jh_btstack_run_loop_service_once(void *context) {
  (void)context;
  if (!s_snapshot.initialized) {
    return HAL_EUNINIT;
  }
  if (s_snapshot.in_service) {
    return HAL_EBUSY;
  }
  s_snapshot.in_service = true;
  if (s_snapshot.poll_pending) {
    s_snapshot.poll_pending = false;
    btstack_run_loop_poll_data_sources_from_irq();
  }
  btstack_run_loop_embedded_execute_once();
  ++s_snapshot.service_calls;
  s_snapshot.in_service = false;
  s_snapshot.last_status = HAL_OK;
  return HAL_OK;
}

void jh_btstack_run_loop_notify(void) {
  ++s_snapshot.notifications;
  if (s_snapshot.poll_pending) {
    ++s_snapshot.coalesced_notifications;
  }
  s_snapshot.poll_pending = true;
}

void jh_btstack_run_loop_invalidate(void *context, uint32_t generation) {
  (void)context;
  (void)generation;
  s_snapshot.poll_pending = false;
  s_snapshot.last_status = HAL_EHW;
}

void jh_btstack_run_loop_snapshot(
    jh_btstack_run_loop_snapshot_t *out_snapshot) {
  if (out_snapshot != NULL) {
    *out_snapshot = s_snapshot;
  }
}
