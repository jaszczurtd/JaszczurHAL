#pragma once

#include "hal/commands/hal_command_wire.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/system/hal_sync.h"

#ifdef HAL_ENABLE_COMMAND_ROUTER

typedef hal_status_t (*jh_command_adapter_pool_lock_t)(void);
typedef void (*jh_command_adapter_pool_unlock_t)(void);
typedef hal_mutex_t (*jh_command_adapter_context_mutex_t)(void *context);
typedef bool (*jh_command_adapter_context_allocated_t)(const void *context);
typedef void (*jh_command_adapter_context_clear_t)(void *context);

typedef struct {
  jh_handle_pool_t *pool;
  jh_command_adapter_pool_lock_t pool_lock;
  jh_command_adapter_pool_unlock_t pool_unlock;
  jh_command_adapter_context_mutex_t context_mutex;
  jh_command_adapter_context_allocated_t context_allocated;
  jh_command_adapter_context_clear_t context_clear;
} jh_command_adapter_context_access_t;

typedef struct {
  const jh_command_adapter_context_access_t *access;
  void *context;
  hal_mutex_t context_mutex;
  jh_handle_lease_t lease;
} jh_command_adapter_operation_t;

#ifdef __cplusplus
template <typename Context>
hal_mutex_t jh_command_adapter_context_mutex(void *context) {
  return static_cast<Context *>(context)->mutex;
}

template <typename Context>
bool jh_command_adapter_context_allocated(const void *context) {
  return static_cast<const Context *>(context)->allocated;
}

template <typename Context>
Context *jh_command_adapter_operation_context(
    jh_command_adapter_operation_t *operation) {
  return operation == nullptr ? nullptr
                              : static_cast<Context *>(operation->context);
}
#endif

hal_status_t jh_command_adapter_context_lock(
    const jh_command_adapter_context_access_t *access, const void *handle,
    jh_command_adapter_operation_t *operation);

hal_status_t
jh_command_adapter_operation_finish(jh_command_adapter_operation_t *operation,
                                    hal_status_t status);

bool jh_command_adapter_status_is_hard(hal_status_t status);

uint32_t jh_command_adapter_next_request_id(uint32_t request_id);

hal_status_t jh_command_adapter_prepare_message(
    hal_command_message_t *scratch, hal_command_message_type_t type,
    uint32_t request_id, const char *name, hal_command_encoding_t encoding,
    const void *payload, size_t payload_length, uint8_t *wire,
    size_t wire_capacity, size_t *out_wire_length);

hal_status_t jh_command_adapter_encode_response(
    const hal_command_response_t *response, uint32_t request_id,
    hal_command_message_t *scratch, uint8_t *wire, size_t wire_capacity,
    size_t *out_wire_length);

#endif /* HAL_ENABLE_COMMAND_ROUTER */
