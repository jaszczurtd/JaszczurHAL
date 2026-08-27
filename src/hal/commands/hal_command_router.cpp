#include "hal/commands/hal_command_router.h"

#ifdef HAL_ENABLE_COMMAND_ROUTER

#include "hal/commands/jh_command_router_internal.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/system/hal_sync.h"

#include <string.h>

#define JH_COMMAND_ROUTER_HANDLE_KIND 11u

typedef struct {
  bool used;
  char name[HAL_COMMAND_ROUTER_NAME_MAX];
  hal_command_source_mask_t allowed_sources;
  hal_command_security_flags_t required_security;
  jh_command_router_invoke_t invoke;
  uint8_t callback[JH_COMMAND_ROUTER_CALLBACK_STORAGE_SIZE];
  size_t callback_size;
  void *user;
  uint32_t active_dispatches;
} jh_command_slot_t;

typedef struct {
  hal_mutex_t mutex;
  jh_command_slot_t commands[HAL_COMMAND_ROUTER_MAX_COMMANDS];
  bool allocated;
  bool is_default;
} jh_command_router_context_t;

static jh_command_router_context_t
    s_contexts[HAL_COMMAND_ROUTER_MAX_INSTANCES] = {};
static jh_handle_slot_t s_handle_slots[HAL_COMMAND_ROUTER_MAX_INSTANCES] = {};
static jh_handle_pool_t s_handle_pool = {};
static hal_mutex_t s_pool_mutex = NULL;
static bool s_pool_initialized = false;
static hal_command_router_t s_default_router = NULL;

static hal_status_t pool_lock(void) {
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_pool_mutex);
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_pool_initialized) {
    hal_status_t status = jh_handle_pool_init(&s_handle_pool, s_handle_slots,
                                              HAL_COMMAND_ROUTER_MAX_INSTANCES,
                                              JH_COMMAND_ROUTER_HANDLE_KIND);
    if (status != HAL_OK) {
      hal_mutex_unlock(mutex);
      return status;
    }
    for (size_t index = 0u; index < HAL_COMMAND_ROUTER_MAX_INSTANCES; ++index) {
      if (jh_hal_mutex_create_once(&s_contexts[index].mutex) == NULL) {
        hal_mutex_unlock(mutex);
        return HAL_ENOMEM;
      }
    }
    s_pool_initialized = true;
  }
  return HAL_OK;
}

static void pool_unlock(void) { hal_mutex_unlock(s_pool_mutex); }

static void clear_context(jh_command_router_context_t *context) {
  hal_mutex_t mutex = context->mutex;
  memset(context, 0, sizeof(*context));
  context->mutex = mutex;
}

static hal_status_t allocate_context_locked(bool is_default,
                                            hal_command_router_t *out_router) {
  jh_command_router_context_t *context = NULL;
  for (size_t index = 0u; index < HAL_COMMAND_ROUTER_MAX_INSTANCES; ++index) {
    if (!s_contexts[index].allocated) {
      context = &s_contexts[index];
      break;
    }
  }
  if (context == NULL) {
    return HAL_ENOMEM;
  }

  clear_context(context);
  context->allocated = true;
  context->is_default = is_default;
  void *handle = NULL;
  hal_status_t status = jh_handle_allocate(&s_handle_pool, context, &handle);
  if (status != HAL_OK) {
    clear_context(context);
    return status;
  }
  *out_router = reinterpret_cast<hal_command_router_t>(handle);
  return HAL_OK;
}

static hal_status_t lease_finish(jh_handle_lease_t *lease) {
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *deferred_token = NULL;
  status = jh_handle_end_operation(&s_handle_pool, lease, &deferred_token);
  if (status == HAL_OK && deferred_token != NULL) {
    clear_context(static_cast<jh_command_router_context_t *>(deferred_token));
  }
  pool_unlock();
  return status;
}

static hal_status_t context_acquire(hal_command_router_t router,
                                    jh_handle_lease_t *out_lease,
                                    jh_command_router_context_t **out_context) {
  if (out_lease == NULL || out_context == NULL) {
    return HAL_EINVAL;
  }
  memset(out_lease, 0, sizeof(*out_lease));
  *out_context = NULL;
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  status = jh_handle_acquire(&s_handle_pool, router, out_lease);
  pool_unlock();
  if (status != HAL_OK || out_lease->token == NULL) {
    return HAL_EUNINIT;
  }
  jh_command_router_context_t *context =
      static_cast<jh_command_router_context_t *>(out_lease->token);
  hal_mutex_lock(context->mutex);
  status = pool_lock();
  if (status != HAL_OK) {
    hal_mutex_unlock(context->mutex);
    (void)lease_finish(out_lease);
    return status;
  }
  const bool lease_is_open = jh_handle_lease_is_open(&s_handle_pool, out_lease);
  pool_unlock();
  if (!lease_is_open) {
    hal_mutex_unlock(context->mutex);
    status = lease_finish(out_lease);
    return status == HAL_OK ? HAL_EUNINIT : status;
  }
  *out_context = context;
  return HAL_OK;
}

static hal_status_t context_finish(jh_command_router_context_t *context,
                                   jh_handle_lease_t *lease) {
  hal_mutex_unlock(context->mutex);
  return lease_finish(lease);
}

static hal_status_t
context_finish_with_status(jh_command_router_context_t *context,
                           jh_handle_lease_t *lease,
                           hal_status_t operation_status) {
  const hal_status_t finish_status = context_finish(context, lease);
  return finish_status == HAL_OK ? operation_status : finish_status;
}

bool jh_command_name_valid(const char *name, size_t *out_length) {
  if (name == NULL || name[0] == '\0') {
    return false;
  }
  size_t length = 0u;
  while (length < HAL_COMMAND_ROUTER_NAME_MAX && name[length] != '\0') {
    const unsigned char value = (unsigned char)name[length];
    if (value <= (unsigned char)' ' || value == 0x7fu) {
      return false;
    }
    ++length;
  }
  if (length == 0u || length >= HAL_COMMAND_ROUTER_NAME_MAX ||
      name[length] != '\0') {
    return false;
  }
  if (out_length != NULL) {
    *out_length = length;
  }
  return true;
}

bool jh_command_source_valid(hal_command_source_t source) {
  return source >= HAL_COMMAND_SOURCE_DIRECT &&
         source < HAL_COMMAND_SOURCE_COUNT;
}

bool jh_command_encoding_valid(hal_command_encoding_t encoding) {
  return encoding == HAL_COMMAND_ENCODING_BINARY ||
         encoding == HAL_COMMAND_ENCODING_TEXT ||
         encoding == HAL_COMMAND_ENCODING_JSON;
}

static jh_command_slot_t *find_command(jh_command_router_context_t *context,
                                       const char *name, size_t *out_index) {
  for (size_t index = 0u; index < HAL_COMMAND_ROUTER_MAX_COMMANDS; ++index) {
    jh_command_slot_t *slot = &context->commands[index];
    if (slot->used && strcmp(slot->name, name) == 0) {
      if (out_index != NULL) {
        *out_index = index;
      }
      return slot;
    }
  }
  return NULL;
}

static hal_status_t invoke_public_handler(const void *callback_storage,
                                          size_t callback_size,
                                          const hal_command_request_t *request,
                                          hal_command_response_t *response,
                                          void *user) {
  if (callback_storage == NULL ||
      callback_size != sizeof(hal_command_handler_t)) {
    return HAL_EINTERNAL;
  }
  hal_command_handler_t handler = NULL;
  memcpy(reinterpret_cast<unsigned char *>(&handler), callback_storage,
         sizeof(handler));
  return handler != NULL ? handler(request, response, user) : HAL_EINTERNAL;
}

hal_status_t hal_command_router_default(hal_command_router_t *out_router) {
  if (out_router == NULL) {
    return HAL_EINVAL;
  }
  *out_router = NULL;
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  if (s_default_router != NULL) {
    void *token = NULL;
    status = jh_handle_resolve(&s_handle_pool, s_default_router, &token, NULL);
    if (status == HAL_OK && token != NULL) {
      *out_router = s_default_router;
      pool_unlock();
      return HAL_OK;
    }
    s_default_router = NULL;
  }
  status = allocate_context_locked(true, &s_default_router);
  if (status == HAL_OK) {
    *out_router = s_default_router;
  }
  pool_unlock();
  return status;
}

hal_status_t hal_command_router_create(hal_command_router_t *out_router) {
  if (out_router == NULL) {
    return HAL_EINVAL;
  }
  *out_router = NULL;
  hal_status_t status = pool_lock();
  if (status == HAL_OK) {
    status = allocate_context_locked(false, out_router);
    pool_unlock();
  }
  return status;
}

hal_status_t hal_command_router_destroy(hal_command_router_t router) {
  jh_handle_lease_t lease = {};
  jh_command_router_context_t *context = NULL;
  hal_status_t status = context_acquire(router, &lease, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (context->is_default) {
    status = context_finish(context, &lease);
    return status == HAL_OK ? HAL_EPERM : status;
  }
  for (size_t index = 0u; index < HAL_COMMAND_ROUTER_MAX_COMMANDS; ++index) {
    if (context->commands[index].active_dispatches != 0u) {
      return context_finish_with_status(context, &lease, HAL_EBUSY);
    }
  }

  status = pool_lock();
  if (status == HAL_OK) {
    void *immediate_token = NULL;
    status = jh_handle_begin_close(&s_handle_pool, router, &immediate_token);
    if (status == HAL_OK && immediate_token != NULL) {
      status = HAL_EINTERNAL;
    }
    pool_unlock();
  }
  const hal_status_t finish_status = context_finish(context, &lease);
  return status == HAL_OK ? finish_status : status;
}

hal_status_t jh_command_router_register_erased(
    hal_command_router_t router,
    const jh_command_router_definition_t *definition) {
  size_t name_length = 0u;
  if (definition == NULL ||
      !jh_command_name_valid(definition->name, &name_length) ||
      definition->allowed_sources == 0u ||
      (definition->allowed_sources & ~HAL_COMMAND_SOURCE_MASK_ALL) != 0u ||
      (definition->required_security & ~HAL_COMMAND_SECURITY_ALL) != 0u ||
      definition->invoke == NULL || definition->callback == NULL ||
      definition->callback_size == 0u ||
      definition->callback_size > JH_COMMAND_ROUTER_CALLBACK_STORAGE_SIZE) {
    return HAL_EINVAL;
  }

  jh_handle_lease_t lease = {};
  jh_command_router_context_t *context = NULL;
  hal_status_t status = context_acquire(router, &lease, &context);
  if (status != HAL_OK) {
    return status;
  }
  jh_command_slot_t *slot = find_command(context, definition->name, NULL);
  if (slot != NULL && slot->active_dispatches != 0u) {
    return context_finish_with_status(context, &lease, HAL_EBUSY);
  }
  if (slot == NULL) {
    for (size_t index = 0u; index < HAL_COMMAND_ROUTER_MAX_COMMANDS; ++index) {
      if (!context->commands[index].used) {
        slot = &context->commands[index];
        break;
      }
    }
  }
  if (slot == NULL) {
    return context_finish_with_status(context, &lease, HAL_ENOMEM);
  }

  memset(slot, 0, sizeof(*slot));
  slot->used = true;
  memcpy(slot->name, definition->name, name_length);
  slot->name[name_length] = '\0';
  slot->allowed_sources = definition->allowed_sources;
  slot->required_security = definition->required_security;
  slot->invoke = definition->invoke;
  memcpy(slot->callback, definition->callback, definition->callback_size);
  slot->callback_size = definition->callback_size;
  slot->user = definition->user;
  return context_finish(context, &lease);
}

hal_status_t
hal_command_router_register(hal_command_router_t router,
                            const hal_command_definition_t *definition) {
  static_assert(sizeof(hal_command_handler_t) <=
                    JH_COMMAND_ROUTER_CALLBACK_STORAGE_SIZE,
                "command callback storage is too small");
  if (definition == NULL || definition->handler == NULL) {
    return HAL_EINVAL;
  }
  jh_command_router_definition_t erased = {};
  erased.name = definition->name;
  erased.allowed_sources = definition->allowed_sources;
  erased.required_security = definition->required_security;
  erased.invoke = invoke_public_handler;
  erased.callback =
      reinterpret_cast<const unsigned char *>(&definition->handler);
  erased.callback_size = sizeof(definition->handler);
  erased.user = definition->user;
  return jh_command_router_register_erased(router, &erased);
}

hal_status_t hal_command_router_unregister(hal_command_router_t router,
                                           const char *name) {
  if (!jh_command_name_valid(name, NULL)) {
    return HAL_EINVAL;
  }
  jh_handle_lease_t lease = {};
  jh_command_router_context_t *context = NULL;
  hal_status_t status = context_acquire(router, &lease, &context);
  if (status != HAL_OK) {
    return status;
  }
  jh_command_slot_t *slot = find_command(context, name, NULL);
  if (slot == NULL) {
    return context_finish_with_status(context, &lease, HAL_ENOENT);
  }
  if (slot->active_dispatches != 0u) {
    return context_finish_with_status(context, &lease, HAL_EBUSY);
  }
  memset(slot, 0, sizeof(*slot));
  return context_finish(context, &lease);
}

hal_status_t hal_command_router_clear(hal_command_router_t router) {
  jh_handle_lease_t lease = {};
  jh_command_router_context_t *context = NULL;
  hal_status_t status = context_acquire(router, &lease, &context);
  if (status != HAL_OK) {
    return status;
  }
  for (size_t index = 0u; index < HAL_COMMAND_ROUTER_MAX_COMMANDS; ++index) {
    if (context->commands[index].active_dispatches != 0u) {
      return context_finish_with_status(context, &lease, HAL_EBUSY);
    }
  }
  memset(context->commands, 0, sizeof(context->commands));
  return context_finish(context, &lease);
}

hal_status_t hal_command_router_count(hal_command_router_t router,
                                      size_t *out_count) {
  if (out_count == NULL) {
    return HAL_EINVAL;
  }
  *out_count = 0u;
  jh_handle_lease_t lease = {};
  jh_command_router_context_t *context = NULL;
  hal_status_t status = context_acquire(router, &lease, &context);
  if (status != HAL_OK) {
    return status;
  }
  for (size_t index = 0u; index < HAL_COMMAND_ROUTER_MAX_COMMANDS; ++index) {
    if (context->commands[index].used) {
      ++(*out_count);
    }
  }
  return context_finish(context, &lease);
}

static hal_status_t reject_request(hal_command_response_t *response,
                                   hal_status_t status, const char *message) {
  response->status = status;
  response->message = message;
  return status;
}

hal_status_t hal_command_router_dispatch(hal_command_router_t router,
                                         const hal_command_request_t *request,
                                         hal_command_response_t *response) {
  if (response == NULL) {
    return HAL_EINVAL;
  }
  hal_command_response_reset(response);
  if (request == NULL || !jh_command_source_valid(request->source) ||
      !jh_command_encoding_valid(request->encoding) ||
      !jh_command_name_valid(request->command, NULL) ||
      (request->arguments_length > 0u && request->arguments == NULL) ||
      (request->security_flags & ~HAL_COMMAND_SECURITY_ALL) != 0u) {
    return reject_request(response, HAL_EINVAL, "invalid request");
  }
  (void)hal_command_response_set_encoding(response, request->encoding);

  jh_handle_lease_t lease = {};
  jh_command_router_context_t *context = NULL;
  hal_status_t status = context_acquire(router, &lease, &context);
  if (status != HAL_OK) {
    return reject_request(response, status, hal_status_to_string(status));
  }
  size_t slot_index = 0u;
  jh_command_slot_t *slot =
      find_command(context, request->command, &slot_index);
  if (slot == NULL) {
    status = context_finish(context, &lease);
    if (status != HAL_OK) {
      return reject_request(response, status, hal_status_to_string(status));
    }
    return reject_request(response, HAL_ENOENT, "unknown command");
  }
  const hal_command_source_mask_t source_mask =
      HAL_COMMAND_SOURCE_MASK(request->source);
  if ((slot->allowed_sources & source_mask) == 0u) {
    status = context_finish(context, &lease);
    if (status != HAL_OK) {
      return reject_request(response, status, hal_status_to_string(status));
    }
    return reject_request(response, HAL_EPERM, "source not allowed");
  }
  if ((request->security_flags & slot->required_security) !=
      slot->required_security) {
    status = context_finish(context, &lease);
    if (status != HAL_OK) {
      return reject_request(response, status, hal_status_to_string(status));
    }
    return reject_request(response, HAL_EAUTH, "security policy not met");
  }

  ++slot->active_dispatches;
  jh_command_router_invoke_t invoke = slot->invoke;
  uint8_t callback[JH_COMMAND_ROUTER_CALLBACK_STORAGE_SIZE] = {};
  const size_t callback_size = slot->callback_size;
  memcpy(callback, slot->callback, callback_size);
  void *user = slot->user;
  hal_mutex_unlock(context->mutex);

  hal_status_t handler_status =
      invoke(callback, callback_size, request, response, user);

  hal_mutex_lock(context->mutex);
  jh_command_slot_t *active_slot = &context->commands[slot_index];
  if (!active_slot->used || active_slot->active_dispatches == 0u) {
    status = context_finish(context, &lease);
    if (status != HAL_OK) {
      return reject_request(response, status, hal_status_to_string(status));
    }
    return reject_request(response, HAL_EINTERNAL, "dispatch state lost");
  }
  --active_slot->active_dispatches;
  status = context_finish(context, &lease);
  if (status != HAL_OK) {
    return reject_request(response, status, hal_status_to_string(status));
  }

  if (handler_status == HAL_NONE) {
    handler_status = HAL_EINTERNAL;
  }
  if (handler_status != HAL_OK && response->status == HAL_OK) {
    response->status = handler_status;
    response->message = hal_status_to_string(handler_status);
  }
  if (response->status == HAL_NONE) {
    response->status = HAL_EINTERNAL;
    response->message = hal_status_to_string(HAL_EINTERNAL);
  }
  return response->status;
}

void hal_command_response_reset(hal_command_response_t *response) {
  if (response == NULL) {
    return;
  }
  response->status = HAL_OK;
  response->message = "OK";
  response->content_type = "application/octet-stream";
  response->encoding = HAL_COMMAND_ENCODING_BINARY;
  response->body_len = 0u;
  response->body[0] = '\0';
  response->overflow = false;
}

hal_status_t hal_command_response_set_status(hal_command_response_t *response,
                                             hal_status_t status,
                                             const char *message) {
  if (response == NULL || status == HAL_NONE) {
    return HAL_EINVAL;
  }
  response->status = status;
  response->message = message != NULL ? message : hal_status_to_string(status);
  return HAL_OK;
}

hal_status_t
hal_command_response_set_content_type(hal_command_response_t *response,
                                      const char *content_type) {
  if (response == NULL || content_type == NULL) {
    return HAL_EINVAL;
  }
  response->content_type = content_type;
  return HAL_OK;
}

hal_status_t
hal_command_response_set_encoding(hal_command_response_t *response,
                                  hal_command_encoding_t encoding) {
  if (response == NULL || !jh_command_encoding_valid(encoding)) {
    return HAL_EINVAL;
  }
  response->encoding = encoding;
  if (encoding == HAL_COMMAND_ENCODING_JSON) {
    response->content_type = "application/json";
  } else if (encoding == HAL_COMMAND_ENCODING_TEXT) {
    response->content_type = "text/plain";
  } else {
    response->content_type = "application/octet-stream";
  }
  return HAL_OK;
}

hal_status_t hal_command_response_write(hal_command_response_t *response,
                                        const void *data, size_t length) {
  if (response == NULL || (length > 0u && data == NULL)) {
    return HAL_EINVAL;
  }
  if (response->body_len >= sizeof(response->body) ||
      length >= sizeof(response->body) - response->body_len) {
    response->overflow = true;
    return HAL_EOVERFLOW;
  }
  if (length > 0u) {
    memcpy(response->body + response->body_len, data, length);
    response->body_len += length;
    response->body[response->body_len] = '\0';
  }
  return HAL_OK;
}

hal_status_t hal_command_response_write_str(hal_command_response_t *response,
                                            const char *text) {
  return text != NULL ? hal_command_response_write(response, text, strlen(text))
                      : HAL_EINVAL;
}

const char *hal_command_source_to_string(hal_command_source_t source) {
  switch (source) {
  case HAL_COMMAND_SOURCE_DIRECT:
    return "DIRECT";
  case HAL_COMMAND_SOURCE_HTTP:
    return "HTTP";
  case HAL_COMMAND_SOURCE_WEBSOCKET:
    return "WEBSOCKET";
  case HAL_COMMAND_SOURCE_SERIAL_SESSION:
    return "SERIAL_SESSION";
  case HAL_COMMAND_SOURCE_LORA_LINK:
    return "LORA_LINK";
  case HAL_COMMAND_SOURCE_BLE_STREAM:
    return "BLE_STREAM";
  default:
    return "UNKNOWN";
  }
}

const char *hal_command_encoding_to_string(hal_command_encoding_t encoding) {
  switch (encoding) {
  case HAL_COMMAND_ENCODING_BINARY:
    return "BINARY";
  case HAL_COMMAND_ENCODING_TEXT:
    return "TEXT";
  case HAL_COMMAND_ENCODING_JSON:
    return "JSON";
  default:
    return "UNKNOWN";
  }
}

#endif /* HAL_ENABLE_COMMAND_ROUTER */
