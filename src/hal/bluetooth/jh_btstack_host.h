#pragma once

#include "jh_bluetooth_host_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t
jh_btstack_host_acquire(jh_bluetooth_host_profile_t profile,
                        const jh_bluetooth_host_profile_ops_t *profile_ops,
                        jh_bluetooth_host_reference_t *out_reference);
hal_status_t jh_btstack_host_release(jh_bluetooth_host_reference_t *reference);
hal_status_t
jh_btstack_host_service(const jh_bluetooth_host_reference_t *reference);
void jh_btstack_host_snapshot(jh_bluetooth_host_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
