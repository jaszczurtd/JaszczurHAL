#pragma once

#include "hal/bluetooth/hal_gamepad.h"
#include "hal/bluetooth/jh_bluetooth_classic_bond_kv_provider.h"

#include <stdint.h>

#if defined(HAL_ENABLE_BLUETOOTH_GAMEPAD) && defined(HAL_ENABLE_KV)

#ifdef __cplusplus
extern "C" {
#endif

/** Caller-owned state used by the KV-backed bond provider callbacks. */
typedef jh_bluetooth_classic_bond_kv_context_t jh_gamepad_bond_kv_context_t;

/**
 * @brief Build a hal_gamepad bond provider backed by hal_kv.
 *
 * Compatibility wrapper over jh_bluetooth_classic_bond_kv_provider() with
 * capacity one. New Classic users should use the indexed provider directly.
 *
 * The caller owns hal_kv's lifetime: hal_kv_init_ex() must already have
 * succeeded before this provider is used, and stay initialized for as long
 * as the gamepad profile is open.
 *
 * @param context Caller-owned storage that must remain valid for as long as
 *                the returned provider can be used.
 * @param key     KV key reserved for the bond blob; must not collide with any
 *                other key the application stores.
 * @return A provider ready to pass to hal_gamepad_open_ex(), or a
 *         zero-initialized provider when @p context is NULL.
 */
hal_gamepad_bond_provider_t
jh_gamepad_bond_kv_provider(jh_gamepad_bond_kv_context_t *context,
                            uint16_t key);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_GAMEPAD && HAL_ENABLE_KV */
