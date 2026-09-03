#pragma once

#include "hal/bluetooth/hal_bluetooth_classic.h"

#if defined(HAL_ENABLE_BLUETOOTH_CLASSIC) && defined(HAL_ENABLE_KV)

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Caller-owned state for an indexed Classic bond provider backed by hal_kv. */
typedef struct {
  uint16_t key;
  size_t capacity;
} jh_bluetooth_classic_bond_kv_context_t;

/**
 * Build an indexed Classic bond provider over consecutive hal_kv keys.
 *
 * Slot index zero uses @p first_key, slot one uses first_key + 1, and so on.
 * hal_kv must remain initialized for the provider's lifetime. The returned
 * provider is zero-initialized when any argument or key range is invalid.
 */
hal_bluetooth_classic_bond_provider_t jh_bluetooth_classic_bond_kv_provider(
    jh_bluetooth_classic_bond_kv_context_t *context, uint16_t first_key,
    size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_CLASSIC && HAL_ENABLE_KV */
