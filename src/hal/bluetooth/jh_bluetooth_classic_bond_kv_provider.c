#include "jh_bluetooth_classic_bond_kv_provider.h"

#if defined(HAL_ENABLE_BLUETOOTH_CLASSIC) && defined(HAL_ENABLE_KV)

#include "hal/storage/hal_kv.h"

#include <limits.h>

static hal_status_t
slot_key(const jh_bluetooth_classic_bond_kv_context_t *context, size_t index,
         uint16_t *out_key) {
  if (context == NULL || out_key == NULL || index >= context->capacity ||
      index > (size_t)(UINT16_MAX - context->key)) {
    return HAL_EINVAL;
  }
  *out_key = (uint16_t)(context->key + index);
  return HAL_OK;
}

static hal_status_t kv_bond_load(void *context, size_t index,
                                 hal_bluetooth_classic_bond_blob_t *out_blob) {
  uint16_t key = 0u;
  if (out_blob == NULL || slot_key(context, index, &key) != HAL_OK) {
    return HAL_EINVAL;
  }
  uint16_t length = 0u;
  const hal_status_t status = hal_kv_get_blob_ex(
      key, out_blob->bytes, (uint16_t)sizeof(out_blob->bytes), &length);
  if (status != HAL_OK) {
    return status;
  }
  return length == sizeof(out_blob->bytes) ? HAL_OK : HAL_EPROTO;
}

static hal_status_t
kv_bond_store(void *context, size_t index,
              const hal_bluetooth_classic_bond_blob_t *blob) {
  uint16_t key = 0u;
  if (blob == NULL || slot_key(context, index, &key) != HAL_OK) {
    return HAL_EINVAL;
  }
  return hal_kv_set_blob_ex(key, blob->bytes, (uint16_t)sizeof(blob->bytes));
}

static hal_status_t kv_bond_erase(void *context, size_t index) {
  uint16_t key = 0u;
  if (slot_key(context, index, &key) != HAL_OK) {
    return HAL_EINVAL;
  }
  const hal_status_t status = hal_kv_delete_ex(key);
  return status == HAL_ENOENT ? HAL_OK : status;
}

hal_bluetooth_classic_bond_provider_t jh_bluetooth_classic_bond_kv_provider(
    jh_bluetooth_classic_bond_kv_context_t *context, uint16_t first_key,
    size_t capacity) {
  hal_bluetooth_classic_bond_provider_t provider = {0};
  if (context == NULL || capacity == 0u ||
      capacity - 1u > (size_t)(UINT16_MAX - first_key)) {
    return provider;
  }
  context->key = first_key;
  context->capacity = capacity;
  provider.context = context;
  provider.capacity = capacity;
  provider.load = kv_bond_load;
  provider.store = kv_bond_store;
  provider.erase = kv_bond_erase;
  return provider;
}

#endif /* HAL_ENABLE_BLUETOOTH_CLASSIC && HAL_ENABLE_KV */
