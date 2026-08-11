#pragma once

#include "hal/core/hal_config.h"
#include "hal/core/hal_status.h"

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

typedef enum {
  HAL_TLS_TRUST_KEY_RSA = 1,
  HAL_TLS_TRUST_KEY_EC = 2
} hal_tls_trust_key_type_t;

/** Provider-neutral X.509 trust anchor. Referenced buffers must remain valid
 * until the client is closed. */
typedef struct {
  const uint8_t *subject_dn;
  size_t subject_dn_length;
  hal_tls_trust_key_type_t key_type;
  union {
    struct {
      const uint8_t *modulus;
      size_t modulus_length;
      const uint8_t *exponent;
      size_t exponent_length;
    } rsa;
    struct {
      int curve;
      const uint8_t *point;
      size_t point_length;
    } ec;
  } key;
} hal_tls_trust_anchor_t;

#define HAL_TLS_TRUST_DN_MAX_LENGTH 512u
#define HAL_TLS_TRUST_KEY_MAX_LENGTH 512u
#define HAL_TLS_TRUST_RSA_EXPONENT_MAX_LENGTH 8u

/** Caller-owned storage used to decode a DER CA certificate without heap
 * allocation or provider-specific public types. */
typedef struct {
  hal_tls_trust_anchor_t anchor;
  uint8_t subject_dn[HAL_TLS_TRUST_DN_MAX_LENGTH];
  uint8_t key[HAL_TLS_TRUST_KEY_MAX_LENGTH];
  uint8_t rsa_exponent[HAL_TLS_TRUST_RSA_EXPONENT_MAX_LENGTH];
} hal_tls_trust_anchor_storage_t;

hal_status_t
hal_tls_trust_anchor_from_der_ex(const void *certificate_der,
                                 size_t certificate_der_length,
                                 hal_tls_trust_anchor_storage_t *out_storage);

typedef hal_status_t (*hal_tls_time_fn)(void *context,
                                        uint64_t *out_unix_seconds);
typedef hal_status_t (*hal_tls_entropy_fn)(void *context, void *buffer,
                                           size_t length);
typedef bool (*hal_tls_cancel_fn)(void *context);
typedef void (*hal_tls_service_fn)(void *context);

hal_status_t hal_tls_default_time(void *context, uint64_t *out_unix_seconds);
hal_status_t hal_tls_default_entropy(void *context, void *buffer,
                                     size_t length);

typedef struct hal_tls_security_config_t {
  const hal_tls_trust_anchor_t *trust_anchors;
  size_t trust_anchor_count;
  hal_tls_time_fn get_time;
  hal_tls_entropy_fn get_entropy;
  hal_tls_cancel_fn is_cancelled;
  hal_tls_service_fn service;
  void *callback_context;
  /** Optional additive SHA-256 pin of the validated server public key.
   * Hash input is key type byte followed by RSA n/e or EC curve/q. */
  const uint8_t *server_public_key_sha256;
} hal_tls_security_config_t;

/** Fill a client configuration with bounded defaults. */
hal_status_t hal_tls_client_config_init(hal_tls_client_config_t *config);

hal_status_t hal_tls_client_create_ex(const hal_tls_client_config_t *config,
                                      hal_tls_client_t *out_client);
hal_status_t hal_tls_client_configure_server_ex(hal_tls_client_t client,
                                                const char *hostname,
                                                uint16_t port);
hal_status_t
hal_tls_client_configure_security_ex(hal_tls_client_t client,
                                     const hal_tls_security_config_t *security);
hal_status_t hal_tls_client_connect_ex(hal_tls_client_t client);
hal_status_t hal_tls_client_poll_ex(hal_tls_client_t client);
hal_status_t hal_tls_client_read_ex(hal_tls_client_t client, void *buffer,
                                    size_t capacity, size_t *out_received);
hal_status_t hal_tls_client_write_ex(hal_tls_client_t client, const void *data,
                                     size_t length, size_t *out_written);
hal_status_t hal_tls_client_shutdown_ex(hal_tls_client_t client);
hal_status_t hal_tls_client_cancel_ex(hal_tls_client_t client);
hal_status_t hal_tls_client_get_state_ex(hal_tls_client_t client,
                                         hal_tls_state_t *out_state);
/**
 * @brief Return the last HAL and optional provider-specific error.
 *
 * Operations on one client are serialized. Security callbacks execute without
 * the client mutex; a callback that re-enters the same client receives
 * HAL_EBUSY. Closing that client from a callback is supported and invalidates
 * the handle immediately while resource destruction is safely deferred.
 * @p out_provider_error may be NULL when only the portable HAL status is
 * required.
 */
hal_status_t hal_tls_client_get_last_error_ex(hal_tls_client_t client,
                                              hal_status_t *out_status,
                                              int32_t *out_provider_error);

/**
 * @brief Release the client slot. Stale copies of the handle remain invalid.
 *
 * When another operation is active, the handle is invalidated immediately and
 * provider/transport destruction occurs after that operation returns.
 */
hal_status_t hal_tls_client_close_ex(hal_tls_client_t client);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_TLS */
