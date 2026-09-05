#pragma once

#include "hci_transport.h"
#include "jh_btstack_diagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

const hci_transport_t *jh_btstack_cyw43_hci_transport_instance(void);
void jh_btstack_cyw43_transport_invalidate(void);

#ifdef __cplusplus
}
#endif
