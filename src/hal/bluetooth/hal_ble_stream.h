#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLE_STREAM

/**
 * @file hal_ble_stream.h
 * @brief JH BLE Stream v1 - bounded byte stream over one static GATT service.
 *
 * The profile is a versioned JaszczurHAL interface. A client reads the protocol
 * version and capabilities without a session. Every mutating or sensitive
 * operation requires a mutually authenticated session built on a per-device
 * secret, HMAC-SHA256 proofs and ChaCha20-Poly1305 frames.
 *
 * This file is the single source of truth for the service UUIDs, the frame
 * layout and the capability bits. Changing any of them raises the profile
 * version.
 */

#include "hal/bluetooth/hal_ble.h"
#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Profile label bound into every transcript. */
#define HAL_BLE_STREAM_PROFILE_NAME "JH BLE Stream"
#define HAL_BLE_STREAM_PROFILE_NAME_LEN 13u
#define HAL_BLE_STREAM_PROTOCOL_VERSION 1u

/* Service and characteristic UUIDs. Assigned once for profile version 1. */
#define HAL_BLE_STREAM_SERVICE_UUID "B7CE0001-3C13-4FE2-801F-D71BDAB1369B"
#define HAL_BLE_STREAM_RX_UUID "B7CE0002-3C13-4FE2-801F-D71BDAB1369B"
#define HAL_BLE_STREAM_TX_UUID "B7CE0003-3C13-4FE2-801F-D71BDAB1369B"
#define HAL_BLE_STREAM_VERSION_UUID "B7CE0004-3C13-4FE2-801F-D71BDAB1369B"
#define HAL_BLE_STREAM_CAPABILITIES_UUID "B7CE0005-3C13-4FE2-801F-D71BDAB1369B"

/* Frame layout: version | type | flags | payload_len | payload. */
#define HAL_BLE_STREAM_FRAME_HEADER_LEN 4u
#define HAL_BLE_STREAM_SECRET_MIN_LEN 32u
#define HAL_BLE_STREAM_SECRET_MAX_LEN 64u
#define HAL_BLE_STREAM_NONCE_LEN 32u
#define HAL_BLE_STREAM_SESSION_ID_LEN 8u
#define HAL_BLE_STREAM_PROOF_LEN 32u
#define HAL_BLE_STREAM_SESSION_KEY_LEN 32u
#define HAL_BLE_STREAM_AEAD_TAG_LEN 16u
#define HAL_BLE_STREAM_AEAD_COUNTER_LEN 8u

/** Largest plaintext payload carried by one DATA frame. */
#ifndef HAL_BLE_STREAM_MAX_PAYLOAD
#define HAL_BLE_STREAM_MAX_PAYLOAD 128u
#endif

#ifndef HAL_BLE_STREAM_RX_QUEUE_DEPTH
#define HAL_BLE_STREAM_RX_QUEUE_DEPTH 4u
#endif

#ifndef HAL_BLE_STREAM_TX_QUEUE_DEPTH
#define HAL_BLE_STREAM_TX_QUEUE_DEPTH 4u
#endif

/** Failed authentication attempts tolerated before the backoff window. */
#ifndef HAL_BLE_STREAM_AUTH_ATTEMPT_LIMIT
#define HAL_BLE_STREAM_AUTH_ATTEMPT_LIMIT 5u
#endif

#ifndef HAL_BLE_STREAM_AUTH_BACKOFF_MS
#define HAL_BLE_STREAM_AUTH_BACKOFF_MS 30000u
#endif

/** Session lifetime without a valid authenticated frame. */
#ifndef HAL_BLE_STREAM_SESSION_IDLE_TIMEOUT_MS
#define HAL_BLE_STREAM_SESSION_IDLE_TIMEOUT_MS 60000u
#endif

#if HAL_BLE_STREAM_MAX_PAYLOAD < 16u || HAL_BLE_STREAM_MAX_PAYLOAD > 200u
#error "HAL_BLE_STREAM_MAX_PAYLOAD must be between 16 and 200"
#endif

/** Largest frame body: AEAD counter, ciphertext and tag. */
#define HAL_BLE_STREAM_MAX_FRAME_BODY                                          \
  (HAL_BLE_STREAM_AEAD_COUNTER_LEN + HAL_BLE_STREAM_MAX_PAYLOAD +              \
   HAL_BLE_STREAM_AEAD_TAG_LEN)

#define HAL_BLE_STREAM_MAX_FRAME_LEN                                           \
  (HAL_BLE_STREAM_FRAME_HEADER_LEN + HAL_BLE_STREAM_MAX_FRAME_BODY)

/** Handshake body: capabilities, session id, device nonce and proof. */
#define HAL_BLE_STREAM_HANDSHAKE_FRAME_LEN                                     \
  (HAL_BLE_STREAM_FRAME_HEADER_LEN + 2u + HAL_BLE_STREAM_SESSION_ID_LEN +      \
   HAL_BLE_STREAM_NONCE_LEN + HAL_BLE_STREAM_PROOF_LEN)

/** ATT overhead of one notification or write. */
#define HAL_BLE_STREAM_ATT_OVERHEAD 3u

/** MTU needed for the handshake; below it the device refuses to start one. */
#define HAL_BLE_STREAM_MIN_ATT_MTU                                             \
  (HAL_BLE_STREAM_HANDSHAKE_FRAME_LEN + HAL_BLE_STREAM_ATT_OVERHEAD)

/** MTU needed to carry a full-size payload in one frame. */
#define HAL_BLE_STREAM_FULL_PAYLOAD_ATT_MTU                                    \
  (HAL_BLE_STREAM_MAX_FRAME_LEN + HAL_BLE_STREAM_ATT_OVERHEAD)

#if HAL_BLE_STREAM_RX_QUEUE_DEPTH < 2u || HAL_BLE_STREAM_TX_QUEUE_DEPTH < 2u
#error "JH BLE Stream queues must hold at least two frames"
#endif

/** Capability bits published without a session and bound into the transcript.
 */
typedef enum {
  HAL_BLE_STREAM_CAP_TELEMETRY = 0x0001u,
  HAL_BLE_STREAM_CAP_DIAGNOSTICS = 0x0002u,
  HAL_BLE_STREAM_CAP_COMMISSIONING = 0x0004u,
  HAL_BLE_STREAM_CAP_CONFIG_WRITE = 0x0008u
} hal_ble_stream_capability_t;

typedef enum {
  HAL_BLE_STREAM_STATE_UNINITIALIZED = 0,
  HAL_BLE_STREAM_STATE_IDLE,        /**< Service published, no subscriber. */
  HAL_BLE_STREAM_STATE_SUBSCRIBED,  /**< Notifications enabled, no session. */
  HAL_BLE_STREAM_STATE_HANDSHAKING, /**< HELLO seen, waiting for client proof.
                                     */
  HAL_BLE_STREAM_STATE_AUTHENTICATED,
  HAL_BLE_STREAM_STATE_BACKOFF /**< Attempt limit reached, refusing handshakes.
                                */
} hal_ble_stream_state_t;

typedef enum {
  HAL_BLE_STREAM_CLOSE_CLIENT_REQUEST = 0,
  HAL_BLE_STREAM_CLOSE_DISCONNECTED,
  HAL_BLE_STREAM_CLOSE_IDLE_TIMEOUT,
  HAL_BLE_STREAM_CLOSE_GENERATION_CHANGED,
  HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR,
  HAL_BLE_STREAM_CLOSE_AUTH_FAILED,
  HAL_BLE_STREAM_CLOSE_REPLAY_DETECTED,
  HAL_BLE_STREAM_CLOSE_COUNTER_EXHAUSTED,
  HAL_BLE_STREAM_CLOSE_LOCAL_REQUEST
} hal_ble_stream_close_reason_t;

typedef struct {
  /** Capabilities advertised to the client and bound into the transcript. */
  uint16_t capabilities;
  /** Session lifetime without a valid frame; zero selects the default. */
  uint32_t idle_timeout_ms;
} hal_ble_stream_config_t;

typedef struct {
  hal_ble_stream_state_t state;
  hal_status_t last_status;
  uint16_t capabilities;
  uint16_t negotiated_capabilities;
  uint32_t generation;
  uint64_t tx_counter;
  uint64_t rx_counter;
  uint32_t auth_failures;
  uint32_t replay_rejections;
  uint32_t dropped_rx_frames;
  uint32_t dropped_tx_frames;
  size_t pending_rx;
  /** Locally queued payloads plus one notification accepted by the backend. */
  size_t pending_tx;
  bool secret_provisioned;
  bool subscribed;
  /** Active public handshake identifier represented as a little-endian integer.
   */
  uint64_t session_id;
} hal_ble_stream_info_t;

/** @brief Authenticated provenance copied with one received payload. */
typedef struct {
  uint32_t generation;
  uint64_t session_id;
  uint64_t counter;
} hal_ble_stream_payload_info_t;

/**
 * @brief Publish the service and reset session state.
 * @param config Capabilities and idle timeout; must not be NULL and is copied.
 * @return HAL_OK on success or when already initialized; HAL_EINVAL for a
 * NULL config, HAL_EUNINIT when BLE is stopped, HAL_ESTATE when BLE is not
 * ready, HAL_EBUSY during another lifecycle change, HAL_EUNSUPPORTED for an
 * incomplete backend, or a service publication error.
 */
hal_status_t hal_ble_stream_initialize(const hal_ble_stream_config_t *config);

/**
 * @brief Close the session, unpublish the stream and zero all key material.
 * @return HAL_OK on success or when already stopped, HAL_EBUSY during another
 * lifecycle change, HAL_ENOMEM when the runtime lock cannot be created, or a
 * service unpublication error.
 */
hal_status_t hal_ble_stream_deinitialize(void);

/**
 * @brief Install the per-device secret used for proofs and session keys.
 * @param secret Secret bytes copied by the profile; must not be NULL.
 * @param length Length from HAL_BLE_STREAM_SECRET_MIN_LEN through
 * HAL_BLE_STREAM_SECRET_MAX_LEN.
 * @return HAL_OK, HAL_EINVAL for invalid input, HAL_EUNINIT before profile
 * initialization, HAL_EBUSY during a lifecycle change, or HAL_ENOMEM.
 * The stored copy is zeroed on deinitialize, clear or replacement.
 */
hal_status_t hal_ble_stream_set_secret(const uint8_t *secret, size_t length);

/**
 * @brief Zero the stored secret and close the active session.
 * @return HAL_OK, HAL_EUNINIT before profile initialization, HAL_EBUSY during
 * a lifecycle change, or HAL_ENOMEM. Authentication remains unavailable until
 * a new secret is installed.
 */
hal_status_t hal_ble_stream_clear_secret(void);

/**
 * @brief Queue one authenticated payload for notification.
 * @param data Payload copied into the bounded transmit queue; must not be NULL.
 * @param length Payload length from 1 through HAL_BLE_STREAM_MAX_PAYLOAD.
 * @return HAL_OK when accepted; HAL_EINVAL for invalid input, HAL_EUNINIT
 * before initialization, HAL_EAUTH without an authenticated session,
 * HAL_EOVERFLOW when the ATT MTU is too small, HAL_EAGAIN on backpressure,
 * HAL_EBUSY during a lifecycle change, or a backend error.
 */
hal_status_t hal_ble_stream_send(const void *data, size_t length);

/**
 * @brief Pop one decrypted payload.
 * @param out Destination buffer; must not be NULL.
 * @param capacity Destination capacity in bytes; must be nonzero.
 * @param out_length Receives the copied length and is set to zero on entry;
 * must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid output, HAL_EUNINIT before
 * initialization, HAL_EAGAIN when empty, HAL_EOVERFLOW for dropped frames or
 * a short destination, HAL_EBUSY during a lifecycle change, or HAL_ENOMEM.
 */
hal_status_t hal_ble_stream_receive(void *out, size_t capacity,
                                    size_t *out_length);

/**
 * @brief Pop one payload together with immutable session metadata.
 * @param out Destination buffer; must not be NULL.
 * @param capacity Destination capacity in bytes; must be nonzero.
 * @param out_length Receives the copied length; must not be NULL.
 * @param out_payload_info Optional metadata output; may be NULL.
 * @return The same statuses as hal_ble_stream_receive().
 */
hal_status_t
hal_ble_stream_receive_ex(void *out, size_t capacity, size_t *out_length,
                          hal_ble_stream_payload_info_t *out_payload_info);

/**
 * @brief Read a consistent stream snapshot.
 * @param out_info Receives the snapshot; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, or HAL_ENOMEM when the runtime
 * lock cannot be created. The uninitialized state is a valid snapshot.
 */
hal_status_t hal_ble_stream_get_info(hal_ble_stream_info_t *out_info);

/**
 * @brief Close the active session and zero its directional keys.
 * @param reason Reason recorded in the session diagnostics.
 * @return HAL_OK, HAL_EUNINIT before profile initialization, HAL_EBUSY during
 * a lifecycle change, or HAL_ENOMEM. Closing an idle profile is allowed.
 */
hal_status_t hal_ble_stream_close_session(hal_ble_stream_close_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLE_STREAM */
