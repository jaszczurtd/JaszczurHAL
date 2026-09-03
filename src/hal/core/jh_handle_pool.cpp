#include "hal/core/jh_handle_pool.h"

#include <limits.h>
#include <string.h>

/* Opaque public handles are tagged tickets, never backend object addresses.
 * Low bits carry a one-based slot and kind; upper bits carry a generation.
 * They are compared and decoded only and are never dereferenced. */
#define JH_HANDLE_SLOT_BITS 8u
#define JH_HANDLE_KIND_BITS 5u
#define JH_HANDLE_SLOT_MASK ((((uintptr_t)1u) << JH_HANDLE_SLOT_BITS) - 1u)
#define JH_HANDLE_KIND_MASK ((((uintptr_t)1u) << JH_HANDLE_KIND_BITS) - 1u)
#define JH_HANDLE_GENERATION_SHIFT (JH_HANDLE_SLOT_BITS + JH_HANDLE_KIND_BITS)
#define JH_HANDLE_GENERATION_MASK (UINTPTR_MAX >> JH_HANDLE_GENERATION_SHIFT)

static uint32_t next_generation(uint32_t generation) {
  generation =
      (uint32_t)(((uintptr_t)generation + 1u) & JH_HANDLE_GENERATION_MASK);
  return generation == 0u ? 1u : generation;
}

static void *encode_handle(const jh_handle_pool_t *pool, size_t index,
                           uint32_t generation) {
  const uintptr_t raw =
      ((uintptr_t)generation << JH_HANDLE_GENERATION_SHIFT) |
      ((pool->kind & JH_HANDLE_KIND_MASK) << JH_HANDLE_SLOT_BITS) |
      (uintptr_t)(index + 1u);
  /* The public handle is an opaque, non-dereferenceable generation ticket.
   * Integer tagging is intentional here: returning a slot address would let a
   * stale handle alias the next owner after pool reuse. */
  return reinterpret_cast<void *>(raw); // NOLINT(performance-no-int-to-ptr)
}

static bool decode_handle(const jh_handle_pool_t *pool, const void *handle,
                          size_t *out_index, uint32_t *out_generation) {
  const uintptr_t raw = reinterpret_cast<uintptr_t>(handle);
  const uintptr_t slot = raw & JH_HANDLE_SLOT_MASK;
  const uintptr_t kind = (raw >> JH_HANDLE_SLOT_BITS) & JH_HANDLE_KIND_MASK;
  const uintptr_t generation = raw >> JH_HANDLE_GENERATION_SHIFT;
  if (slot == 0u || slot > pool->capacity || kind != pool->kind ||
      generation == 0u || generation > JH_HANDLE_GENERATION_MASK) {
    return false;
  }
  *out_index = (size_t)(slot - 1u);
  *out_generation = (uint32_t)generation;
  return true;
}

hal_status_t jh_handle_pool_init(jh_handle_pool_t *pool,
                                 jh_handle_slot_t *slots, size_t capacity,
                                 uintptr_t kind) {
  if (pool == nullptr || slots == nullptr || capacity == 0u ||
      capacity > JH_HANDLE_SLOT_MASK || kind == 0u ||
      kind > JH_HANDLE_KIND_MASK) {
    return HAL_EINVAL;
  }
  memset(slots, 0, capacity * sizeof(slots[0]));
  pool->slots = slots;
  pool->capacity = capacity;
  pool->kind = kind;
  return HAL_OK;
}

hal_status_t jh_handle_allocate(jh_handle_pool_t *pool, void *token,
                                void **out_handle) {
  if (pool == nullptr || pool->slots == nullptr || token == nullptr ||
      out_handle == nullptr) {
    return HAL_EINVAL;
  }
  *out_handle = nullptr;
  for (size_t index = 0u; index < pool->capacity; ++index) {
    jh_handle_slot_t *slot = &pool->slots[index];
    if (!slot->in_use && !slot->closing && slot->active_operations == 0u &&
        slot->token == nullptr) {
      slot->generation = next_generation(slot->generation);
      slot->token = token;
      slot->in_use = true;
      *out_handle = encode_handle(pool, index, slot->generation);
      return HAL_OK;
    }
  }
  return HAL_ENOMEM;
}

hal_status_t jh_handle_resolve(const jh_handle_pool_t *pool, const void *handle,
                               void **out_token, size_t *out_index) {
  if (out_token != nullptr) {
    *out_token = nullptr;
  }
  if (pool == nullptr || pool->slots == nullptr || out_token == nullptr) {
    return HAL_EINVAL;
  }
  size_t index = 0u;
  uint32_t generation = 0u;
  if (!decode_handle(pool, handle, &index, &generation)) {
    return HAL_EINVAL;
  }
  const jh_handle_slot_t *slot = &pool->slots[index];
  if (!slot->in_use || slot->generation != generation ||
      slot->token == nullptr) {
    return HAL_EINVAL;
  }
  *out_token = slot->token;
  if (out_index != nullptr) {
    *out_index = index;
  }
  return HAL_OK;
}

hal_status_t jh_handle_acquire(jh_handle_pool_t *pool, const void *handle,
                               jh_handle_lease_t *out_lease) {
  if (out_lease != nullptr) {
    memset(out_lease, 0, sizeof(*out_lease));
  }
  if (pool == nullptr || out_lease == nullptr) {
    return HAL_EINVAL;
  }
  void *token = nullptr;
  size_t index = 0u;
  const hal_status_t status = jh_handle_resolve(pool, handle, &token, &index);
  if (status != HAL_OK) {
    return status;
  }
  jh_handle_slot_t *slot = &pool->slots[index];
  if (slot->active_operations == UINT32_MAX) {
    return HAL_EOVERFLOW;
  }
  ++slot->active_operations;
  out_lease->token = token;
  out_lease->index = index;
  out_lease->generation = slot->generation;
  out_lease->active = true;
  return HAL_OK;
}

bool jh_handle_lease_is_open(const jh_handle_pool_t *pool,
                             const jh_handle_lease_t *lease) {
  if (pool == nullptr || pool->slots == nullptr || lease == nullptr ||
      !lease->active || lease->index >= pool->capacity) {
    return false;
  }
  const jh_handle_slot_t *slot = &pool->slots[lease->index];
  return slot->in_use && !slot->closing &&
         slot->generation == lease->generation && slot->token == lease->token;
}

hal_status_t jh_handle_end_operation(jh_handle_pool_t *pool,
                                     jh_handle_lease_t *lease,
                                     void **out_deferred_token) {
  if (out_deferred_token != nullptr) {
    *out_deferred_token = nullptr;
  }
  if (pool == nullptr || pool->slots == nullptr || lease == nullptr ||
      !lease->active || lease->index >= pool->capacity ||
      out_deferred_token == nullptr) {
    return HAL_EINVAL;
  }
  jh_handle_slot_t *slot = &pool->slots[lease->index];
  if (slot->generation != lease->generation || slot->token != lease->token ||
      slot->active_operations == 0u) {
    return HAL_EINVAL;
  }
  --slot->active_operations;
  lease->active = false;
  if (slot->closing && slot->active_operations == 0u) {
    *out_deferred_token = slot->token;
    slot->token = nullptr;
    slot->closing = false;
  }
  return HAL_OK;
}

hal_status_t jh_handle_begin_close(jh_handle_pool_t *pool, const void *handle,
                                   void **out_token) {
  if (out_token != nullptr) {
    *out_token = nullptr;
  }
  if (pool == nullptr || pool->slots == nullptr || out_token == nullptr) {
    return HAL_EINVAL;
  }
  size_t index = 0u;
  uint32_t generation = 0u;
  if (!decode_handle(pool, handle, &index, &generation)) {
    return HAL_EINVAL;
  }
  jh_handle_slot_t *slot = &pool->slots[index];
  if (!slot->in_use || slot->generation != generation ||
      slot->token == nullptr) {
    return HAL_EINVAL;
  }
  slot->in_use = false;
  slot->closing = true;
  if (slot->active_operations == 0u) {
    *out_token = slot->token;
    slot->token = nullptr;
    slot->closing = false;
  }
  return HAL_OK;
}

size_t jh_handle_begin_close_all(jh_handle_pool_t *pool, void **out_tokens,
                                 size_t token_capacity) {
  if (pool == nullptr || pool->slots == nullptr ||
      token_capacity < pool->capacity || out_tokens == nullptr) {
    return 0u;
  }
  size_t token_count = 0u;
  for (size_t index = 0u; index < pool->capacity; ++index) {
    jh_handle_slot_t *slot = &pool->slots[index];
    if (!slot->in_use) {
      continue;
    }
    slot->in_use = false;
    slot->closing = true;
    if (slot->active_operations == 0u && token_count < token_capacity) {
      out_tokens[token_count++] = slot->token;
      slot->token = nullptr;
      slot->closing = false;
    }
  }
  return token_count;
}

hal_status_t jh_handle_release(jh_handle_pool_t *pool, const void *handle,
                               void **out_token) {
  return jh_handle_begin_close(pool, handle, out_token);
}

void jh_handle_invalidate_all(jh_handle_pool_t *pool) {
  if (pool == nullptr || pool->slots == nullptr) {
    return;
  }
  for (size_t index = 0u; index < pool->capacity; ++index) {
    pool->slots[index].token = nullptr;
    pool->slots[index].in_use = false;
    pool->slots[index].active_operations = 0u;
    pool->slots[index].closing = false;
    pool->slots[index].generation =
        next_generation(pool->slots[index].generation);
  }
}
