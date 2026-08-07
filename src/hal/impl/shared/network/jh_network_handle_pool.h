#pragma once

#include "../../../hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *backend_token;
  uint32_t generation;
  uint32_t active_operations;
  bool in_use;
  bool closing;
} jh_network_handle_slot_t;

typedef struct {
  void *backend_token;
  size_t index;
  uint32_t generation;
  bool active;
} jh_network_handle_lease_t;

typedef struct {
  jh_network_handle_slot_t *slots;
  size_t capacity;
  uintptr_t kind;
} jh_network_handle_pool_t;

hal_status_t jh_network_handle_pool_init(jh_network_handle_pool_t *pool,
                                         jh_network_handle_slot_t *slots,
                                         size_t capacity, uintptr_t kind);
hal_status_t jh_network_handle_allocate(jh_network_handle_pool_t *pool,
                                        void *backend_token, void **out_handle);
hal_status_t jh_network_handle_resolve(const jh_network_handle_pool_t *pool,
                                       const void *handle,
                                       void **out_backend_token,
                                       size_t *out_index);
hal_status_t jh_network_handle_acquire(jh_network_handle_pool_t *pool,
                                       const void *handle,
                                       jh_network_handle_lease_t *out_lease);
bool jh_network_handle_lease_is_open(const jh_network_handle_pool_t *pool,
                                     const jh_network_handle_lease_t *lease);
hal_status_t jh_network_handle_end_operation(jh_network_handle_pool_t *pool,
                                             jh_network_handle_lease_t *lease,
                                             void **out_deferred_backend_token);
hal_status_t jh_network_handle_begin_close(jh_network_handle_pool_t *pool,
                                           const void *handle,
                                           void **out_backend_token);
size_t jh_network_handle_begin_close_all(jh_network_handle_pool_t *pool,
                                         void **out_backend_tokens,
                                         size_t token_capacity);
hal_status_t jh_network_handle_release(jh_network_handle_pool_t *pool,
                                       const void *handle,
                                       void **out_backend_token);
void jh_network_handle_invalidate_all(jh_network_handle_pool_t *pool);

#ifdef __cplusplus
}
#endif
