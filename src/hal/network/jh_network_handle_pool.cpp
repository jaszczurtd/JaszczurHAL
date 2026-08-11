#include "jh_network_handle_pool.h"

hal_status_t jh_network_handle_pool_init(jh_network_handle_pool_t *pool,
                                         jh_network_handle_slot_t *slots,
                                         size_t capacity, uintptr_t kind) {
  return jh_handle_pool_init(pool, slots, capacity, kind);
}

hal_status_t jh_network_handle_allocate(jh_network_handle_pool_t *pool,
                                        void *backend_token,
                                        void **out_handle) {
  return jh_handle_allocate(pool, backend_token, out_handle);
}

hal_status_t jh_network_handle_resolve(const jh_network_handle_pool_t *pool,
                                       const void *handle,
                                       void **out_backend_token,
                                       size_t *out_index) {
  return jh_handle_resolve(pool, handle, out_backend_token, out_index);
}

hal_status_t jh_network_handle_acquire(jh_network_handle_pool_t *pool,
                                       const void *handle,
                                       jh_network_handle_lease_t *out_lease) {
  return jh_handle_acquire(pool, handle, out_lease);
}

bool jh_network_handle_lease_is_open(const jh_network_handle_pool_t *pool,
                                     const jh_network_handle_lease_t *lease) {
  return jh_handle_lease_is_open(pool, lease);
}

hal_status_t
jh_network_handle_end_operation(jh_network_handle_pool_t *pool,
                                jh_network_handle_lease_t *lease,
                                void **out_deferred_backend_token) {
  return jh_handle_end_operation(pool, lease, out_deferred_backend_token);
}

hal_status_t jh_network_handle_begin_close(jh_network_handle_pool_t *pool,
                                           const void *handle,
                                           void **out_backend_token) {
  return jh_handle_begin_close(pool, handle, out_backend_token);
}

size_t jh_network_handle_begin_close_all(jh_network_handle_pool_t *pool,
                                         void **out_backend_tokens,
                                         size_t token_capacity) {
  return jh_handle_begin_close_all(pool, out_backend_tokens, token_capacity);
}

hal_status_t jh_network_handle_release(jh_network_handle_pool_t *pool,
                                       const void *handle,
                                       void **out_backend_token) {
  return jh_handle_release(pool, handle, out_backend_token);
}

void jh_network_handle_invalidate_all(jh_network_handle_pool_t *pool) {
  jh_handle_invalidate_all(pool);
}
