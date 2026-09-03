#include "jh_gamepad_bond_kv_provider.h"

#if defined(HAL_ENABLE_BLUETOOTH_GAMEPAD) && defined(HAL_ENABLE_KV)

static hal_status_t kv_bond_load(void *context,
                                 hal_gamepad_bond_blob_t *out_blob) {
  jh_gamepad_bond_kv_context_t *kv_context = context;
  const hal_bluetooth_classic_bond_provider_t provider =
      jh_bluetooth_classic_bond_kv_provider(
          kv_context, kv_context != NULL ? kv_context->key : 0u, 1u);
  return provider.load != NULL ? provider.load(provider.context, 0u, out_blob)
                               : HAL_EINVAL;
}

static hal_status_t kv_bond_store(void *context,
                                  const hal_gamepad_bond_blob_t *blob) {
  jh_gamepad_bond_kv_context_t *kv_context = context;
  const hal_bluetooth_classic_bond_provider_t provider =
      jh_bluetooth_classic_bond_kv_provider(
          kv_context, kv_context != NULL ? kv_context->key : 0u, 1u);
  return provider.store != NULL ? provider.store(provider.context, 0u, blob)
                                : HAL_EINVAL;
}

static hal_status_t kv_bond_erase(void *context) {
  jh_gamepad_bond_kv_context_t *kv_context = context;
  const hal_bluetooth_classic_bond_provider_t provider =
      jh_bluetooth_classic_bond_kv_provider(
          kv_context, kv_context != NULL ? kv_context->key : 0u, 1u);
  return provider.erase != NULL ? provider.erase(provider.context, 0u)
                                : HAL_EINVAL;
}

hal_gamepad_bond_provider_t
jh_gamepad_bond_kv_provider(jh_gamepad_bond_kv_context_t *context,
                            uint16_t key) {
  hal_gamepad_bond_provider_t provider = {0};
  if (context == NULL) {
    return provider;
  }
  (void)jh_bluetooth_classic_bond_kv_provider(context, key, 1u);
  provider.context = context;
  provider.load = kv_bond_load;
  provider.store = kv_bond_store;
  provider.erase = kv_bond_erase;
  return provider;
}

#endif /* HAL_ENABLE_BLUETOOTH_GAMEPAD && HAL_ENABLE_KV */
