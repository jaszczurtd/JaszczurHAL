#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_COMMAND_ROUTER

/**
 * @file hal_command_router.h
 * @brief Transport-neutral command registration and dispatch.
 *
 * One router can serve direct calls, network adapters, reliable LoRa links,
 * serial sessions and a future or application-provided BLE Stream adapter.
 * Command handlers receive binary-safe arguments plus source, peer, session
 * and security metadata.
 */

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque command-router handle. */
typedef struct hal_command_router_impl_s *hal_command_router_t;

/** @brief Transport or local entry point that supplied a request. */
typedef enum {
  HAL_COMMAND_SOURCE_DIRECT = 0,
  HAL_COMMAND_SOURCE_HTTP = 1,
  HAL_COMMAND_SOURCE_WEBSOCKET = 2,
  HAL_COMMAND_SOURCE_SERIAL_SESSION = 3,
  HAL_COMMAND_SOURCE_LORA_LINK = 4,
  HAL_COMMAND_SOURCE_BLE_STREAM = 5,
  HAL_COMMAND_SOURCE_COUNT = 6,
} hal_command_source_t;

typedef uint32_t hal_command_source_mask_t;

#define HAL_COMMAND_SOURCE_MASK(source) (UINT32_C(1) << (uint32_t)(source))
#define HAL_COMMAND_SOURCE_MASK_ALL                                            \
  ((UINT32_C(1) << (uint32_t)HAL_COMMAND_SOURCE_COUNT) - UINT32_C(1))

/** @brief Meaning of the binary-safe arguments and response body. */
typedef enum {
  HAL_COMMAND_ENCODING_BINARY = 0,
  HAL_COMMAND_ENCODING_TEXT = 1,
  HAL_COMMAND_ENCODING_JSON = 2,
} hal_command_encoding_t;

typedef uint32_t hal_command_security_flags_t;

#define HAL_COMMAND_SECURITY_AUTHENTICATED UINT32_C(0x00000001)
#define HAL_COMMAND_SECURITY_ENCRYPTED UINT32_C(0x00000002)
#define HAL_COMMAND_SECURITY_INTEGRITY_PROTECTED UINT32_C(0x00000004)
#define HAL_COMMAND_SECURITY_REPLAY_PROTECTED UINT32_C(0x00000008)
#define HAL_COMMAND_SECURITY_ALL                                               \
  (HAL_COMMAND_SECURITY_AUTHENTICATED | HAL_COMMAND_SECURITY_ENCRYPTED |       \
   HAL_COMMAND_SECURITY_INTEGRITY_PROTECTED |                                  \
   HAL_COMMAND_SECURITY_REPLAY_PROTECTED)

/** @brief One borrowed request view valid only during handler execution. */
typedef struct {
  hal_command_source_t source;
  hal_command_encoding_t encoding;
  const char *command;
  const uint8_t *arguments;
  size_t arguments_length;
  uint32_t request_id;
  uint64_t peer_id;
  uint64_t session_id;
  hal_command_security_flags_t security_flags;
  const void *source_context;
} hal_command_request_t;

/** @brief Bounded handler response shared by every command adapter. */
typedef struct {
  hal_status_t status;
  /** Borrowed diagnostic text; it must remain valid while response is used. */
  const char *message;
  /** Borrowed MIME type; it must remain valid while response is used. */
  const char *content_type;
  char body[HAL_COMMAND_RESPONSE_BUFFER_SIZE];
  size_t body_len;
  bool overflow;
  /** Kept last to preserve the established net-response field layout. */
  hal_command_encoding_t encoding;
} hal_command_response_t;

typedef hal_status_t (*hal_command_handler_t)(
    const hal_command_request_t *request, hal_command_response_t *response,
    void *user);

/** @brief Command policy and callback copied into one router slot. */
typedef struct {
  const char *name;
  hal_command_source_mask_t allowed_sources;
  hal_command_security_flags_t required_security;
  hal_command_handler_t handler;
  void *user;
} hal_command_definition_t;

/** @brief Return the process-wide router shared by transport adapters. */
hal_status_t hal_command_router_default(hal_command_router_t *out_router);

/** @brief Allocate an independent router from the static instance pool. */
hal_status_t hal_command_router_create(hal_command_router_t *out_router);

/** @brief Release an independent router. The default router cannot be freed. */
hal_status_t hal_command_router_destroy(hal_command_router_t router);

/**
 * @brief Add or replace one copied command definition.
 *
 * Replacing the command currently being dispatched returns HAL_EBUSY.
 */
hal_status_t
hal_command_router_register(hal_command_router_t router,
                            const hal_command_definition_t *definition);

/** @brief Remove one named command. */
hal_status_t hal_command_router_unregister(hal_command_router_t router,
                                           const char *name);

/** @brief Remove every command when no handler is active. */
hal_status_t hal_command_router_clear(hal_command_router_t router);

/** @brief Copy the number of registered commands. */
hal_status_t hal_command_router_count(hal_command_router_t router,
                                      size_t *out_count);

/**
 * @brief Validate policy and synchronously invoke one registered handler.
 *
 * The response is always reset before lookup. HAL_ENOENT, HAL_EPERM and
 * HAL_EAUTH are also written into response.status. Concurrent dispatches may
 * invoke the same handler at the same time; handler and user state must be
 * thread-safe when multiple callers or adapters can dispatch concurrently.
 */
hal_status_t hal_command_router_dispatch(hal_command_router_t router,
                                         const hal_command_request_t *request,
                                         hal_command_response_t *response);

void hal_command_response_reset(hal_command_response_t *response);
hal_status_t hal_command_response_set_status(hal_command_response_t *response,
                                             hal_status_t status,
                                             const char *message);
hal_status_t
hal_command_response_set_content_type(hal_command_response_t *response,
                                      const char *content_type);
hal_status_t hal_command_response_set_encoding(hal_command_response_t *response,
                                               hal_command_encoding_t encoding);
hal_status_t hal_command_response_write(hal_command_response_t *response,
                                        const void *data, size_t length);
hal_status_t hal_command_response_write_str(hal_command_response_t *response,
                                            const char *text);

const char *hal_command_source_to_string(hal_command_source_t source);
const char *hal_command_encoding_to_string(hal_command_encoding_t encoding);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_COMMAND_ROUTER */
