#include "hal/commands/jh_command_adapter_internal.h"

#ifdef HAL_ENABLE_COMMAND_ROUTER

#include <string.h>

namespace {

bool context_access_valid(const jh_command_adapter_context_access_t *access) {
  return access != nullptr && access->pool != nullptr &&
         access->pool_lock != nullptr && access->pool_unlock != nullptr &&
         access->context_mutex != nullptr &&
         access->context_allocated != nullptr &&
         access->context_clear != nullptr;
}

hal_status_t operation_end(jh_command_adapter_operation_t *operation) {
  if (operation == nullptr || !context_access_valid(operation->access) ||
      operation->context == nullptr || !operation->lease.active) {
    return HAL_EINVAL;
  }

  const jh_command_adapter_context_access_t *access = operation->access;
  hal_status_t status = access->pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *deferred_context = nullptr;
  status = jh_handle_end_operation(access->pool, &operation->lease,
                                   &deferred_context);
  if (status == HAL_OK && deferred_context != nullptr) {
    if (deferred_context == operation->context) {
      access->context_clear(deferred_context);
    } else {
      status = HAL_EINTERNAL;
    }
  }
  access->pool_unlock();
  operation->access = nullptr;
  operation->context = nullptr;
  operation->context_mutex = nullptr;
  return status;
}

hal_status_t copy_message_name(hal_command_message_t *message,
                               const char *name) {
  if (message == nullptr || name == nullptr) {
    return HAL_EINVAL;
  }
  size_t length = 0u;
  while (length < sizeof(message->name) && name[length] != '\0') {
    ++length;
  }
  if (length == 0u || length >= sizeof(message->name)) {
    return HAL_EINVAL;
  }
  memcpy(message->name, name, length);
  message->name[length] = '\0';
  return HAL_OK;
}

} // namespace

hal_status_t jh_command_adapter_context_lock(
    const jh_command_adapter_context_access_t *access, const void *handle,
    jh_command_adapter_operation_t *operation) {
  if (!context_access_valid(access) || operation == nullptr) {
    return HAL_EINVAL;
  }
  memset(operation, 0, sizeof(*operation));
  operation->access = access;

  hal_status_t status = access->pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  status = jh_handle_acquire(access->pool, handle, &operation->lease);
  access->pool_unlock();
  if (status != HAL_OK || operation->lease.token == nullptr) {
    return HAL_EUNINIT;
  }

  operation->context = operation->lease.token;
  operation->context_mutex = access->context_mutex(operation->context);
  hal_mutex_lock(operation->context_mutex);
  status = access->pool_lock();
  if (status != HAL_OK) {
    hal_mutex_unlock(operation->context_mutex);
    const hal_status_t end_status = operation_end(operation);
    return end_status == HAL_OK ? status : end_status;
  }
  const bool open = jh_handle_lease_is_open(access->pool, &operation->lease);
  const bool allocated = access->context_allocated(operation->context);
  access->pool_unlock();
  if (!open || !allocated) {
    hal_mutex_unlock(operation->context_mutex);
    const hal_status_t end_status = operation_end(operation);
    return end_status == HAL_OK ? HAL_EUNINIT : end_status;
  }
  return HAL_OK;
}

hal_status_t
jh_command_adapter_operation_finish(jh_command_adapter_operation_t *operation,
                                    hal_status_t status) {
  if (operation == nullptr || operation->context == nullptr ||
      operation->context_mutex == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_unlock(operation->context_mutex);
  const hal_status_t end_status = operation_end(operation);
  return end_status == HAL_OK ? status : end_status;
}

uint32_t jh_command_adapter_next_request_id(uint32_t request_id) {
  return request_id == UINT32_MAX ? UINT32_C(1) : request_id + UINT32_C(1);
}

hal_status_t jh_command_adapter_prepare_message(
    hal_command_message_t *scratch, hal_command_message_type_t type,
    uint32_t request_id, const char *name, hal_command_encoding_t encoding,
    const void *payload, size_t payload_length, uint8_t *wire,
    size_t wire_capacity, size_t *out_wire_length) {
  if (scratch == nullptr || wire == nullptr || out_wire_length == nullptr ||
      (payload_length > 0u && payload == nullptr)) {
    return HAL_EINVAL;
  }
  if (payload_length > sizeof(scratch->payload)) {
    return HAL_EOVERFLOW;
  }

  memset(scratch, 0, sizeof(*scratch));
  scratch->type = type;
  scratch->encoding = encoding;
  scratch->request_id = request_id;
  scratch->status = HAL_NONE;
  hal_status_t status = copy_message_name(scratch, name);
  if (status != HAL_OK) {
    return status;
  }
  if (payload_length > 0u) {
    memcpy(scratch->payload, payload, payload_length);
  }
  scratch->payload_length = payload_length;
  return hal_command_message_encode(scratch, wire, wire_capacity,
                                    out_wire_length);
}

hal_status_t jh_command_adapter_encode_response(
    const hal_command_response_t *response, uint32_t request_id,
    hal_command_message_t *scratch, uint8_t *wire, size_t wire_capacity,
    size_t *out_wire_length) {
  if (response == nullptr || scratch == nullptr || wire == nullptr ||
      out_wire_length == nullptr || request_id == 0u) {
    return HAL_EINVAL;
  }

  memset(scratch, 0, sizeof(*scratch));
  scratch->type = HAL_COMMAND_MESSAGE_RESPONSE;
  scratch->encoding = response->encoding;
  scratch->request_id = request_id;
  scratch->status = response->status;
  if (response->overflow || response->body_len > sizeof(response->body) ||
      response->body_len > sizeof(scratch->payload)) {
    scratch->encoding = HAL_COMMAND_ENCODING_BINARY;
    scratch->status = HAL_EOVERFLOW;
  } else if (response->body_len > 0u) {
    memcpy(scratch->payload, response->body, response->body_len);
    scratch->payload_length = response->body_len;
  }

  hal_status_t status =
      hal_command_message_encode(scratch, wire, wire_capacity, out_wire_length);
  if (status == HAL_OK) {
    return HAL_OK;
  }

  memset(scratch, 0, sizeof(*scratch));
  scratch->type = HAL_COMMAND_MESSAGE_RESPONSE;
  scratch->encoding = HAL_COMMAND_ENCODING_BINARY;
  scratch->request_id = request_id;
  scratch->status = HAL_EINTERNAL;
  return hal_command_message_encode(scratch, wire, wire_capacity,
                                    out_wire_length);
}

#endif /* HAL_ENABLE_COMMAND_ROUTER */
