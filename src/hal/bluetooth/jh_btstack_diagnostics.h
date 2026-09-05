#pragma once

#include "jh_bluetooth_hci_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/** CYW43 BTstack HCI transport counters copied without address data. */
typedef jh_bluetooth_hci_transport_snapshot_t
    jh_btstack_cyw43_transport_snapshot_t;

/**
 * @brief Copy current CYW43 BTstack transport diagnostics.
 * @param out_snapshot Receives the snapshot; ignored when NULL.
 */
void jh_btstack_cyw43_transport_snapshot(
    jh_btstack_cyw43_transport_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
