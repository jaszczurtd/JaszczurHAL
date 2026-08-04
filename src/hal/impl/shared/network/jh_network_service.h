#pragma once

#include "../../../hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t jh_network_context_owner_t;

typedef struct {
  void *context;
  void (*state_lock)(void *context);
  void (*state_unlock)(void *context);
  jh_network_context_owner_t (*current_owner)(void *context);
  hal_status_t (*stack_enter)(void *context);
  void (*stack_leave)(void *context);
  hal_status_t (*service)(void *context);
  bool (*ipv4_ready)(void *context);
} jh_network_service_port_t;

typedef struct {
  const jh_network_service_port_t *port;
  uint32_t generation;
  jh_network_context_owner_t owner;
  size_t depth;
  size_t pending_operations;
  bool running;
  bool stopping;
} jh_network_service_t;

typedef struct {
  jh_network_service_t *service;
  uint32_t generation;
  bool active;
} jh_network_operation_t;

hal_status_t jh_network_service_init(jh_network_service_t *service,
                                     const jh_network_service_port_t *port);
hal_status_t jh_network_service_start(jh_network_service_t *service);
hal_status_t jh_network_service_stop(jh_network_service_t *service);
/**
 * Stop only when no stack context is active. Pending operations are
 * invalidated by the generation change. Unlike jh_network_service_stop(), a
 * busy result leaves the service running.
 */
hal_status_t jh_network_service_try_stop(jh_network_service_t *service);
bool jh_network_service_is_quiescent(jh_network_service_t *service);

hal_status_t jh_network_service_enter(jh_network_service_t *service,
                                      bool require_ipv4);
hal_status_t jh_network_service_leave(jh_network_service_t *service);

hal_status_t jh_network_operation_begin(jh_network_service_t *service,
                                        jh_network_operation_t *operation);
bool jh_network_operation_complete(jh_network_operation_t *operation);
bool jh_network_operation_cancel(jh_network_operation_t *operation);

#ifdef __cplusplus
}
#endif
