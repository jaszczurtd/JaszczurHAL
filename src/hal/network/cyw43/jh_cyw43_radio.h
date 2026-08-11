#pragma once

#include "jh_cyw43_radio_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t jh_cyw43_radio_acquire(jh_cyw43_radio_client_t client);
hal_status_t jh_cyw43_radio_release(jh_cyw43_radio_client_t client);
hal_status_t
jh_cyw43_radio_set_invalidation_handler(jh_cyw43_radio_client_t client,
                                        jh_cyw43_radio_invalidation_fn handler,
                                        void *context);
hal_status_t
jh_cyw43_radio_set_service_handler(jh_cyw43_radio_client_t client,
                                   jh_cyw43_radio_service_fn handler,
                                   void *context);
/** Run active internal client services while the caller owns the stack lock. */
hal_status_t jh_cyw43_radio_service_clients(void);
hal_status_t jh_cyw43_radio_restart(void);
hal_status_t jh_cyw43_radio_enter(jh_cyw43_radio_client_t client,
                                  bool require_ipv4);
hal_status_t jh_cyw43_radio_leave(void);
hal_status_t jh_cyw43_radio_service(jh_cyw43_radio_client_t client);
hal_status_t
jh_cyw43_radio_snapshot(jh_cyw43_radio_runtime_snapshot_t *out_snapshot);
bool jh_cyw43_radio_generation_is_current(jh_cyw43_radio_client_t client,
                                          uint32_t generation);

#ifdef __cplusplus
}
#endif
