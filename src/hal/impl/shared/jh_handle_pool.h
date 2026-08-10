#pragma once

#include "hal/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One slot backing an opaque generation-tagged handle. */
typedef struct {
  union {
    void *token;
    void *backend_token; /**< Compatibility name for network consumers. */
  };
  uint32_t generation;
  uint32_t active_operations;
  bool in_use;
  bool closing;
} jh_handle_slot_t;

/** @brief Active operation lease that keeps a closing token alive. */
typedef struct {
  union {
    void *token;
    void *backend_token; /**< Compatibility name for network consumers. */
  };
  size_t index;
  uint32_t generation;
  bool active;
} jh_handle_lease_t;

/** @brief Caller-synchronized pool of opaque generation tickets. */
typedef struct {
  jh_handle_slot_t *slots;
  size_t capacity;
  uintptr_t kind;
} jh_handle_pool_t;

hal_status_t jh_handle_pool_init(jh_handle_pool_t *pool,
                                 jh_handle_slot_t *slots, size_t capacity,
                                 uintptr_t kind);
hal_status_t jh_handle_allocate(jh_handle_pool_t *pool, void *token,
                                void **out_handle);
hal_status_t jh_handle_resolve(const jh_handle_pool_t *pool, const void *handle,
                               void **out_token, size_t *out_index);
hal_status_t jh_handle_acquire(jh_handle_pool_t *pool, const void *handle,
                               jh_handle_lease_t *out_lease);
bool jh_handle_lease_is_open(const jh_handle_pool_t *pool,
                             const jh_handle_lease_t *lease);
hal_status_t jh_handle_end_operation(jh_handle_pool_t *pool,
                                     jh_handle_lease_t *lease,
                                     void **out_deferred_token);
hal_status_t jh_handle_begin_close(jh_handle_pool_t *pool, const void *handle,
                                   void **out_token);
size_t jh_handle_begin_close_all(jh_handle_pool_t *pool, void **out_tokens,
                                 size_t token_capacity);
hal_status_t jh_handle_release(jh_handle_pool_t *pool, const void *handle,
                               void **out_token);
void jh_handle_invalidate_all(jh_handle_pool_t *pool);

#ifdef __cplusplus
}
#endif
