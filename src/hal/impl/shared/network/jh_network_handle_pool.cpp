#include "jh_network_handle_pool.h"

#include <limits.h>
#include <string.h>

/* Opaque public handles are tagged tickets, never backend object addresses.
 * Low bits carry a one-based slot and kind; upper bits carry a generation.
 * They are compared and decoded only and are never dereferenced. */
#define JH_HANDLE_SLOT_BITS 8u
#define JH_HANDLE_KIND_BITS 4u
#define JH_HANDLE_SLOT_MASK ((((uintptr_t)1u) << JH_HANDLE_SLOT_BITS) - 1u)
#define JH_HANDLE_KIND_MASK ((((uintptr_t)1u) << JH_HANDLE_KIND_BITS) - 1u)
#define JH_HANDLE_GENERATION_SHIFT (JH_HANDLE_SLOT_BITS + JH_HANDLE_KIND_BITS)
#define JH_HANDLE_GENERATION_MASK (UINTPTR_MAX >> JH_HANDLE_GENERATION_SHIFT)

static uint32_t next_generation(uint32_t generation) {
  generation =
      (uint32_t)(((uintptr_t)generation + 1u) & JH_HANDLE_GENERATION_MASK);
  return generation == 0u ? 1u : generation;
}

static void *encode_handle(const jh_network_handle_pool_t *pool, size_t index,
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

static bool decode_handle(const jh_network_handle_pool_t *pool,
                          const void *handle, size_t *out_index,
                          uint32_t *out_generation) {
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

hal_status_t jh_network_handle_pool_init(jh_network_handle_pool_t *pool,
                                         jh_network_handle_slot_t *slots,
                                         size_t capacity, uintptr_t kind) {
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

hal_status_t jh_network_handle_allocate(jh_network_handle_pool_t *pool,
                                        void *backend_token,
                                        void **out_handle) {
  if (pool == nullptr || pool->slots == nullptr || backend_token == nullptr ||
      out_handle == nullptr) {
    return HAL_EINVAL;
  }
  *out_handle = nullptr;
  for (size_t index = 0u; index < pool->capacity; ++index) {
    jh_network_handle_slot_t *slot = &pool->slots[index];
    if (!slot->in_use) {
      slot->generation = next_generation(slot->generation);
      slot->backend_token = backend_token;
      slot->in_use = true;
      *out_handle = encode_handle(pool, index, slot->generation);
      return HAL_OK;
    }
  }
  return HAL_ENOMEM;
}

hal_status_t jh_network_handle_resolve(const jh_network_handle_pool_t *pool,
                                       const void *handle,
                                       void **out_backend_token,
                                       size_t *out_index) {
  if (out_backend_token != nullptr) {
    *out_backend_token = nullptr;
  }
  if (pool == nullptr || pool->slots == nullptr ||
      out_backend_token == nullptr) {
    return HAL_EINVAL;
  }
  size_t index = 0u;
  uint32_t generation = 0u;
  if (!decode_handle(pool, handle, &index, &generation)) {
    return HAL_EINVAL;
  }
  const jh_network_handle_slot_t *slot = &pool->slots[index];
  if (!slot->in_use || slot->generation != generation ||
      slot->backend_token == nullptr) {
    return HAL_EINVAL;
  }
  *out_backend_token = slot->backend_token;
  if (out_index != nullptr) {
    *out_index = index;
  }
  return HAL_OK;
}

hal_status_t jh_network_handle_release(jh_network_handle_pool_t *pool,
                                       const void *handle,
                                       void **out_backend_token) {
  size_t index = 0u;
  hal_status_t status =
      jh_network_handle_resolve(pool, handle, out_backend_token, &index);
  if (status != HAL_OK) {
    return status;
  }
  pool->slots[index].backend_token = nullptr;
  pool->slots[index].in_use = false;
  return HAL_OK;
}

void jh_network_handle_invalidate_all(jh_network_handle_pool_t *pool) {
  if (pool == nullptr || pool->slots == nullptr) {
    return;
  }
  for (size_t index = 0u; index < pool->capacity; ++index) {
    pool->slots[index].backend_token = nullptr;
    pool->slots[index].in_use = false;
    pool->slots[index].generation =
        next_generation(pool->slots[index].generation);
  }
}
