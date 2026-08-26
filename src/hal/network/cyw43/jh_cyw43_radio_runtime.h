#pragma once

#include "hal/core/hal_status.h"
#include "hal/network/jh_network_service.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_CYW43_RADIO_CLIENT_WIFI = 0,
  JH_CYW43_RADIO_CLIENT_BLUETOOTH,
  JH_CYW43_RADIO_CLIENT_COUNT,
} jh_cyw43_radio_client_t;

typedef enum {
  JH_CYW43_RADIO_STATE_OFF = 0,
  JH_CYW43_RADIO_STATE_STARTING,
  JH_CYW43_RADIO_STATE_READY,
  JH_CYW43_RADIO_STATE_STOPPING,
  JH_CYW43_RADIO_STATE_FAILED,
} jh_cyw43_radio_state_t;

typedef void (*jh_cyw43_radio_invalidation_fn)(void *context,
                                               uint32_t generation);
/* Internal stack hooks run under the shared CYW43 lock. Application event
 * callbacks must use a deferred dispatcher outside this runtime. */
typedef hal_status_t (*jh_cyw43_radio_service_fn)(void *context);

typedef struct {
  void *context;
  const jh_network_service_port_t *service_port;
  hal_status_t (*start)(void *context);
  hal_status_t (*stop)(void *context);
} jh_cyw43_radio_runtime_port_t;

typedef struct {
  jh_cyw43_radio_state_t state;
  uint32_t generation;
  uint16_t wifi_references;
  uint16_t bluetooth_references;
} jh_cyw43_radio_runtime_snapshot_t;

typedef struct {
  const jh_cyw43_radio_runtime_port_t *port;
  jh_network_service_t service;
  jh_cyw43_radio_invalidation_fn invalidation[JH_CYW43_RADIO_CLIENT_COUNT];
  void *invalidation_context[JH_CYW43_RADIO_CLIENT_COUNT];
  jh_cyw43_radio_service_fn client_service[JH_CYW43_RADIO_CLIENT_COUNT];
  void *client_service_context[JH_CYW43_RADIO_CLIENT_COUNT];
  uint16_t references[JH_CYW43_RADIO_CLIENT_COUNT];
  jh_cyw43_radio_state_t state;
  bool initialized;
} jh_cyw43_radio_runtime_t;

/** Initialize a zero-initialized runtime object with a persistent port. */
hal_status_t
jh_cyw43_radio_runtime_init(jh_cyw43_radio_runtime_t *runtime,
                            const jh_cyw43_radio_runtime_port_t *port);
hal_status_t jh_cyw43_radio_runtime_set_invalidation_handler(
    jh_cyw43_radio_runtime_t *runtime, jh_cyw43_radio_client_t client,
    jh_cyw43_radio_invalidation_fn handler, void *context);
hal_status_t jh_cyw43_radio_runtime_set_service_handler(
    jh_cyw43_radio_runtime_t *runtime, jh_cyw43_radio_client_t client,
    jh_cyw43_radio_service_fn handler, void *context);
/** Run active client stack services. The caller must hold the stack lock. */
hal_status_t
jh_cyw43_radio_runtime_service_clients(jh_cyw43_radio_runtime_t *runtime);
hal_status_t jh_cyw43_radio_runtime_acquire(jh_cyw43_radio_runtime_t *runtime,
                                            jh_cyw43_radio_client_t client);
hal_status_t jh_cyw43_radio_runtime_release(jh_cyw43_radio_runtime_t *runtime,
                                            jh_cyw43_radio_client_t client);
hal_status_t jh_cyw43_radio_runtime_restart(jh_cyw43_radio_runtime_t *runtime);
hal_status_t jh_cyw43_radio_runtime_enter(jh_cyw43_radio_runtime_t *runtime,
                                          jh_cyw43_radio_client_t client,
                                          bool require_ipv4);
hal_status_t jh_cyw43_radio_runtime_leave(jh_cyw43_radio_runtime_t *runtime);
hal_status_t jh_cyw43_radio_runtime_service(jh_cyw43_radio_runtime_t *runtime,
                                            jh_cyw43_radio_client_t client);
hal_status_t jh_cyw43_radio_runtime_snapshot(
    jh_cyw43_radio_runtime_t *runtime,
    jh_cyw43_radio_runtime_snapshot_t *out_snapshot);
bool jh_cyw43_radio_runtime_generation_is_current(
    jh_cyw43_radio_runtime_t *runtime, jh_cyw43_radio_client_t client,
    uint32_t generation);

#ifdef __cplusplus
}
#endif
