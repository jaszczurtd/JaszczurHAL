#include "jh_gamepad_bond_kv_provider.h"

#if defined(HAL_ENABLE_BLUETOOTH_GAMEPAD) && defined(HAL_ENABLE_KV)

#include "hal/storage/hal_kv.h"

static hal_status_t kv_bond_load(void *context,
                                 hal_gamepad_bond_blob_t *out_blob) {
  if (out_blob == NULL) {
    return HAL_EINVAL;
  }
  const uint16_t key = (uint16_t)(uintptr_t)context;
  uint16_t len = 0u;
  const hal_status_t status = hal_kv_get_blob_ex(
      key, out_blob->bytes, (uint16_t)sizeof(out_blob->bytes), &len);
  if (status != HAL_OK) {
    return status;
  }
  return len == sizeof(out_blob->bytes) ? HAL_OK : HAL_EPROTO;
}

static hal_status_t kv_bond_store(void *context,
                                  const hal_gamepad_bond_blob_t *blob) {
  if (blob == NULL) {
    return HAL_EINVAL;
  }
  const uint16_t key = (uint16_t)(uintptr_t)context;
  return hal_kv_set_blob_ex(key, blob->bytes, (uint16_t)sizeof(blob->bytes));
}

static hal_status_t kv_bond_erase(void *context) {
  const uint16_t key = (uint16_t)(uintptr_t)context;
  const hal_status_t status = hal_kv_delete_ex(key);
  /* Already absent is not an error for a factory-reset erase(). */
  return status == HAL_ENOENT ? HAL_OK : status;
}

hal_gamepad_bond_provider_t jh_gamepad_bond_kv_provider(uint16_t key) {
  hal_gamepad_bond_provider_t provider = {0};
  provider.context = (void *)(uintptr_t)key;
  provider.load = kv_bond_load;
  provider.store = kv_bond_store;
  provider.erase = kv_bond_erase;
  return provider;
}

#endif /* HAL_ENABLE_BLUETOOTH_GAMEPAD && HAL_ENABLE_KV */
