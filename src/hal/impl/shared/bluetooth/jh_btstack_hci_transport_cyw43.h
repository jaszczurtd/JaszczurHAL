#pragma once

#include <stdint.h>

#include "hal/hal_status.h"
#include "hci_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  hal_status_t last_status;
  uint32_t rx_packets;
  uint32_t tx_packets;
  uint32_t drain_budget_hits;
} jh_btstack_cyw43_transport_snapshot_t;

const hci_transport_t *jh_btstack_cyw43_hci_transport_instance(void);
void jh_btstack_cyw43_transport_snapshot(
    jh_btstack_cyw43_transport_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
