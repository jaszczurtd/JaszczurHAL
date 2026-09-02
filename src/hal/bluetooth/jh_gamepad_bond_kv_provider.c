#include "jh_gamepad_bond_kv_provider.h"

#if defined(HAL_ENABLE_BLUETOOTH_GAMEPAD) && defined(HAL_ENABLE_KV)

#include "hal/storage/hal_kv.h"

static uint16_t kv_bond_key(const void *context) {
  return ((const jh_gamepad_bond_kv_context_t *)context)->key;
}

static hal_status_t kv_bond_load(void *context,
                                 hal_gamepad_bond_blob_t *out_blob) {
  if (context == NULL || out_blob == NULL) {
    return HAL_EINVAL;
  }
  uint16_t len = 0u;
  const hal_status_t status =
      hal_kv_get_blob_ex(kv_bond_key(context), out_blob->bytes,
                         (uint16_t)sizeof(out_blob->bytes), &len);
  if (status != HAL_OK) {
    return status;
  }
  return len == sizeof(out_blob->bytes) ? HAL_OK : HAL_EPROTO;
}

static hal_status_t kv_bond_store(void *context,
                                  const hal_gamepad_bond_blob_t *blob) {
  if (context == NULL || blob == NULL) {
    return HAL_EINVAL;
  }
  return hal_kv_set_blob_ex(kv_bond_key(context), blob->bytes,
                            (uint16_t)sizeof(blob->bytes));
}

static hal_status_t kv_bond_erase(void *context) {
  if (context == NULL) {
    return HAL_EINVAL;
  }
  const hal_status_t status = hal_kv_delete_ex(kv_bond_key(context));
  /* Already absent is not an error for a factory-reset erase(). */
  return status == HAL_ENOENT ? HAL_OK : status;
}

hal_gamepad_bond_provider_t
jh_gamepad_bond_kv_provider(jh_gamepad_bond_kv_context_t *context,
                            uint16_t key) {
  hal_gamepad_bond_provider_t provider = {0};
  if (context == NULL) {
    return provider;
  }
  context->key = key;
  provider.context = context;
  provider.load = kv_bond_load;
  provider.store = kv_bond_store;
  provider.erase = kv_bond_erase;
  return provider;
}

#endif /* HAL_ENABLE_BLUETOOTH_GAMEPAD && HAL_ENABLE_KV */
