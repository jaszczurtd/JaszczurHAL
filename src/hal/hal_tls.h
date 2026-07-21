#pragma once

#include "hal_config.h"
#include "hal_status.h"

#ifdef HAL_ENABLE_TLS

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque, generation-checked TLS client handle. */
typedef struct hal_tls_client_impl_t *hal_tls_client_t;

/** @brief Execution policy for the private TLS provider. */
typedef enum {
  /** Caller repeatedly invokes hal_tls_client_poll_ex(); no call owns a task.
   */
  HAL_TLS_EXECUTION_POLL = 0,
  /** Finite blocking I/O is allowed from a dedicated worker/task context. */
  HAL_TLS_EXECUTION_BOUNDED_WORKER = 1
} hal_tls_execution_model_t;

/** @brief Observable client lifecycle without exposing provider state. */
typedef enum {
  HAL_TLS_STATE_CREATED = 0,
  HAL_TLS_STATE_CONFIGURED,
  HAL_TLS_STATE_CONNECTING,
  HAL_TLS_STATE_CONNECTED,
  HAL_TLS_STATE_CLOSING,
  HAL_TLS_STATE_CLOSED,
  HAL_TLS_STATE_FAILED
} hal_tls_state_t;

typedef struct {
  hal_tls_execution_model_t execution_model;
  uint32_t transport_timeout_ms;
  uint32_t operation_timeout_ms;
  uint16_t poll_step_budget;
} hal_tls_client_config_t;

/** Fill a client configuration with bounded defaults. */
hal_status_t hal_tls_client_config_init(hal_tls_client_config_t *config);

hal_status_t hal_tls_client_create_ex(const hal_tls_client_config_t *config,
                                      hal_tls_client_t *out_client);
hal_status_t hal_tls_client_configure_server_ex(hal_tls_client_t client,
                                                const char *hostname,
                                                uint16_t port);
hal_status_t hal_tls_client_connect_ex(hal_tls_client_t client);
hal_status_t hal_tls_client_poll_ex(hal_tls_client_t client);
hal_status_t hal_tls_client_read_ex(hal_tls_client_t client, void *buffer,
                                    size_t capacity, size_t *out_received);
hal_status_t hal_tls_client_write_ex(hal_tls_client_t client, const void *data,
                                     size_t length, size_t *out_written);
hal_status_t hal_tls_client_shutdown_ex(hal_tls_client_t client);
hal_status_t hal_tls_client_get_state_ex(hal_tls_client_t client,
                                         hal_tls_state_t *out_state);
hal_status_t hal_tls_client_get_last_error_ex(hal_tls_client_t client,
                                              hal_status_t *out_status,
                                              int32_t *out_provider_error);

/** Release the client slot. Stale copies of the handle remain invalid. */
hal_status_t hal_tls_client_close_ex(hal_tls_client_t client);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_TLS */
