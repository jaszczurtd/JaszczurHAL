#pragma once

#include "jh_bluetooth_hci_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef jh_bluetooth_hci_transport_snapshot_t
    jh_btstack_cyw43_transport_snapshot_t;

void jh_btstack_cyw43_transport_snapshot(
    jh_btstack_cyw43_transport_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
