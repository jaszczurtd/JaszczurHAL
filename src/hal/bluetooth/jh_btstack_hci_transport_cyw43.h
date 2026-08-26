#pragma once

#include "hci_transport.h"
#include "jh_bluetooth_hci_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef jh_bluetooth_hci_transport_snapshot_t
    jh_btstack_cyw43_transport_snapshot_t;

const hci_transport_t *jh_btstack_cyw43_hci_transport_instance(void);
void jh_btstack_cyw43_transport_snapshot(
    jh_btstack_cyw43_transport_snapshot_t *out_snapshot);
void jh_btstack_cyw43_transport_invalidate(void);

#ifdef __cplusplus
}
#endif
