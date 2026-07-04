#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_NET_COMMANDS

#include "hal_http_server.h"
#include "hal_status.h"
#include "hal_websocket.h"
#include "impl/shared/frameworks/cjson/cJSON.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file hal_net_commands.h
 * @brief Small HTTP/WebSocket command dispatcher for connected firmware.
 *
 * Commands are registered by name and can be invoked either as plain text
 * (`name optional args`) or JSON (`{"cmd":"name","args":{...}}`). JSON is
 * parsed with the bundled cJSON framework. The module is transport-light: it
 * provides ready-to-register HTTP and WebSocket helpers, but command handlers
 * stay independent from the concrete server that delivered the request.
 */

#ifndef HAL_NET_COMMANDS_MAX_COMMANDS
#define HAL_NET_COMMANDS_MAX_COMMANDS 8u
#endif

#ifndef HAL_NET_COMMANDS_NAME_MAX
#define HAL_NET_COMMANDS_NAME_MAX 32u
#endif

#ifndef HAL_NET_COMMANDS_TEXT_BUFFER_SIZE
#define HAL_NET_COMMANDS_TEXT_BUFFER_SIZE 256u
#endif

#ifndef HAL_NET_COMMANDS_RESPONSE_BUFFER_SIZE
#define HAL_NET_COMMANDS_RESPONSE_BUFFER_SIZE 512u
#endif

#define HAL_NET_COMMANDS_DEFAULT_HTTP_PATH "/api/command"

#if HAL_NET_COMMANDS_MAX_COMMANDS < 1
#error "HAL_NET_COMMANDS_MAX_COMMANDS must be at least 1"
#endif

#if HAL_NET_COMMANDS_NAME_MAX < 2
#error "HAL_NET_COMMANDS_NAME_MAX must be at least 2"
#endif

#if HAL_NET_COMMANDS_TEXT_BUFFER_SIZE < 8
#error "HAL_NET_COMMANDS_TEXT_BUFFER_SIZE must be at least 8"
#endif

#if HAL_NET_COMMANDS_RESPONSE_BUFFER_SIZE < 32
#error "HAL_NET_COMMANDS_RESPONSE_BUFFER_SIZE must be at least 32"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HAL_NET_COMMANDS_FORMAT_TEXT = 0,
  HAL_NET_COMMANDS_FORMAT_JSON,
  HAL_NET_COMMANDS_FORMAT_AUTO
} hal_net_commands_format_t;

typedef enum {
  HAL_NET_COMMANDS_SOURCE_DIRECT = 0,
  HAL_NET_COMMANDS_SOURCE_HTTP,
  HAL_NET_COMMANDS_SOURCE_WEBSOCKET
} hal_net_commands_source_t;

typedef struct {
  hal_net_commands_source_t source;
  const char *command;
  const char *args_text;
  const cJSON *json_root;
  const cJSON *json_args;
  const hal_http_request_t *http_request;
  hal_websocket_client_t websocket_client;
} hal_net_command_request_t;

typedef struct {
  hal_status_t status;
  const char *message;
  const char *content_type;
  char body[HAL_NET_COMMANDS_RESPONSE_BUFFER_SIZE];
  size_t body_len;
  bool overflow;
} hal_net_command_response_t;

typedef hal_status_t (*hal_net_command_handler_t)(
    const hal_net_command_request_t *request,
    hal_net_command_response_t *response, void *user);

hal_status_t hal_net_commands_register(const char *name,
                                       hal_net_command_handler_t handler,
                                       void *user);

hal_status_t hal_net_commands_unregister(const char *name);

void hal_net_commands_clear(void);

size_t hal_net_commands_count(void);

void hal_net_command_response_reset(hal_net_command_response_t *response);

hal_status_t
hal_net_command_response_set_status(hal_net_command_response_t *response,
                                    hal_status_t status, const char *message);

hal_status_t
hal_net_command_response_set_content_type(hal_net_command_response_t *response,
                                          const char *content_type);

hal_status_t
hal_net_command_response_write(hal_net_command_response_t *response,
                               const void *data, size_t len);

hal_status_t
hal_net_command_response_write_str(hal_net_command_response_t *response,
                                   const char *text);

hal_status_t
hal_net_command_response_write_json(hal_net_command_response_t *response,
                                    const cJSON *json);

hal_status_t
hal_net_commands_execute_text(const char *text,
                              hal_net_command_response_t *response);

hal_status_t
hal_net_commands_execute_json(const char *json, size_t len,
                              hal_net_command_response_t *response);

hal_status_t hal_net_commands_execute(const void *data, size_t len,
                                      hal_net_commands_format_t format,
                                      hal_net_command_response_t *response);

hal_status_t
hal_net_commands_register_http_route(const char *path,
                                     hal_net_commands_format_t format);

hal_status_t
hal_net_commands_handle_http_request(const hal_http_request_t *request,
                                     hal_http_response_t *response,
                                     hal_net_commands_format_t format);

hal_status_t hal_net_commands_handle_websocket_message(
    hal_websocket_client_t client, hal_websocket_message_type_t type,
    const uint8_t *data, size_t len, hal_net_commands_format_t format);

const char *hal_net_commands_format_to_string(hal_net_commands_format_t format);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_NET_COMMANDS */
