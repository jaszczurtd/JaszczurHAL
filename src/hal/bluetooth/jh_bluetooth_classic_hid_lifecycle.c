#include "jh_bluetooth_classic_hid_lifecycle.h"

#include <stddef.h>

static bool valid_ops(const jh_bluetooth_classic_hid_lifecycle_ops_t *ops) {
  return ops != NULL && ops->link_key_db_start != NULL &&
         ops->link_key_db_stop != NULL && ops->sdp_client_start != NULL &&
         ops->sdp_client_stop != NULL && ops->hid_host_start != NULL &&
         ops->hid_host_stop != NULL && ops->event_handler_start != NULL &&
         ops->event_handler_stop != NULL;
}

static bool is_started(const jh_bluetooth_classic_hid_lifecycle_t *lifecycle) {
  return lifecycle->link_key_db_started || lifecycle->sdp_client_started ||
         lifecycle->hid_host_started || lifecycle->event_handler_started;
}

void jh_bluetooth_classic_hid_lifecycle_stop(
    jh_bluetooth_classic_hid_lifecycle_t *lifecycle,
    const jh_bluetooth_classic_hid_lifecycle_ops_t *ops) {
  if (lifecycle == NULL || !valid_ops(ops)) {
    return;
  }
  if (lifecycle->event_handler_started) {
    ops->event_handler_stop(ops->context);
    lifecycle->event_handler_started = false;
  }
  if (lifecycle->hid_host_started) {
    ops->hid_host_stop(ops->context);
    lifecycle->hid_host_started = false;
  }
  if (lifecycle->sdp_client_started) {
    ops->sdp_client_stop(ops->context);
    lifecycle->sdp_client_started = false;
  }
  if (lifecycle->link_key_db_started) {
    ops->link_key_db_stop(ops->context);
    lifecycle->link_key_db_started = false;
  }
}

hal_status_t jh_bluetooth_classic_hid_lifecycle_start(
    jh_bluetooth_classic_hid_lifecycle_t *lifecycle,
    const jh_bluetooth_classic_hid_lifecycle_ops_t *ops) {
  if (lifecycle == NULL || !valid_ops(ops)) {
    return HAL_EINVAL;
  }
  if (is_started(lifecycle)) {
    return HAL_EBUSY;
  }

  hal_status_t status = ops->link_key_db_start(ops->context);
  if (status != HAL_OK) {
    return status;
  }
  lifecycle->link_key_db_started = true;

  status = ops->sdp_client_start(ops->context);
  if (status == HAL_OK) {
    lifecycle->sdp_client_started = true;
    status = ops->hid_host_start(ops->context);
  }
  if (status == HAL_OK) {
    lifecycle->hid_host_started = true;
    status = ops->event_handler_start(ops->context);
  }
  if (status == HAL_OK) {
    lifecycle->event_handler_started = true;
    return HAL_OK;
  }

  jh_bluetooth_classic_hid_lifecycle_stop(lifecycle, ops);
  return status;
}
