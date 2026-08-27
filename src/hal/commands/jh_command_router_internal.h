#pragma once

#include "hal/commands/hal_command_router.h"

#ifdef HAL_ENABLE_COMMAND_ROUTER

#define JH_COMMAND_ROUTER_CALLBACK_STORAGE_SIZE 16u

typedef hal_status_t (*jh_command_router_invoke_t)(
    const void *callback_storage, size_t callback_size,
    const hal_command_request_t *request, hal_command_response_t *response,
    void *user);

typedef struct {
  const char *name;
  hal_command_source_mask_t allowed_sources;
  hal_command_security_flags_t required_security;
  jh_command_router_invoke_t invoke;
  const void *callback;
  size_t callback_size;
  void *user;
} jh_command_router_definition_t;

hal_status_t jh_command_router_register_erased(
    hal_command_router_t router,
    const jh_command_router_definition_t *definition);

bool jh_command_name_valid(const char *name, size_t *out_length);
bool jh_command_source_valid(hal_command_source_t source);
bool jh_command_encoding_valid(hal_command_encoding_t encoding);

#endif /* HAL_ENABLE_COMMAND_ROUTER */
