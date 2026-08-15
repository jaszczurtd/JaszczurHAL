/*
 * JaszczurHAL lifecycle adapter for the pinned lwIP mDNS responder.
 *
 * The upstream implementation leaves delayed response and truncated-question
 * timers armed when a netif is removed. Rename its public removal function,
 * compile it in this translation unit so its private state remains visible,
 * and provide the corrected entry point below.
 */

#include "lwip/apps/mdns.h"

#if LWIP_MDNS_RESPONDER

err_t jh_lwip_mdns_resp_remove_netif_upstream(struct netif *netif);

#define mdns_resp_remove_netif jh_lwip_mdns_resp_remove_netif_upstream
#include "jh_lwip_mdns_upstream.inc"
#undef mdns_resp_remove_netif

#include "jh_lwip_mdns_teardown.inc"

#endif /* LWIP_MDNS_RESPONDER */
