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
  bool in_use;
} jh_network_handle_slot_t;

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
hal_status_t jh_network_handle_release(jh_network_handle_pool_t *pool,
                                       const void *handle,
                                       void **out_backend_token);
void jh_network_handle_invalidate_all(jh_network_handle_pool_t *pool);

#ifdef __cplusplus
}
#endif
