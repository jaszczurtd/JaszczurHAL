#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLE_STREAM

#include "hal/bluetooth/hal_ble_stream.h"
#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wire frame types of JH BLE Stream v1. */
typedef enum {
  JH_BLE_STREAM_FRAME_HELLO = 0x01,
  JH_BLE_STREAM_FRAME_HELLO_ACK = 0x02,
  JH_BLE_STREAM_FRAME_AUTH = 0x03,
  JH_BLE_STREAM_FRAME_AUTH_ACK = 0x04,
  JH_BLE_STREAM_FRAME_DATA = 0x05,
  JH_BLE_STREAM_FRAME_CLOSE = 0x06
} jh_ble_stream_frame_type_t;

/* Direction byte bound into the AEAD nonce and associated data. */
typedef enum {
  JH_BLE_STREAM_DIR_DEVICE_TO_CLIENT = 0x01,
  JH_BLE_STREAM_DIR_CLIENT_TO_DEVICE = 0x02
} jh_ble_stream_direction_t;

typedef enum {
  JH_BLE_STREAM_SESSION_IDLE = 0,
  JH_BLE_STREAM_SESSION_HANDSHAKING,
  JH_BLE_STREAM_SESSION_AUTHENTICATED
} jh_ble_stream_session_state_t;

typedef struct {
  uint8_t secret[HAL_BLE_STREAM_SECRET_MAX_LEN];
  size_t secret_length;
  uint16_t local_capabilities;
  uint16_t peer_capabilities;
  uint8_t session_id[HAL_BLE_STREAM_SESSION_ID_LEN];
  uint8_t client_nonce[HAL_BLE_STREAM_NONCE_LEN];
  uint8_t device_nonce[HAL_BLE_STREAM_NONCE_LEN];
  uint8_t key_device_to_client[HAL_BLE_STREAM_SESSION_KEY_LEN];
  uint8_t key_client_to_device[HAL_BLE_STREAM_SESSION_KEY_LEN];
  uint64_t tx_counter;
  uint64_t rx_counter;
  jh_ble_stream_session_state_t state;
} jh_ble_stream_session_t;

/* Result of processing one inbound frame. */
typedef struct {
  /* Frame to notify back to the client, when response_length is non-zero. */
  uint8_t response[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t response_length;
  /* Decrypted application payload, when payload_length is non-zero. */
  uint8_t payload[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t payload_length;
  /* Set when the session must be dropped and its keys zeroed. */
  bool close_session;
  hal_ble_stream_close_reason_t close_reason;
} jh_ble_stream_session_result_t;

/** Drop session state and zero every derived key. The secret is kept. */
void jh_ble_stream_session_reset(jh_ble_stream_session_t *session);

/** Zero the whole session including the provisioned secret. */
void jh_ble_stream_session_clear(jh_ble_stream_session_t *session);

/** Install the per-device secret used for proofs and key derivation. */
hal_status_t jh_ble_stream_session_set_secret(jh_ble_stream_session_t *session,
                                              const uint8_t *secret,
                                              size_t length);

/**
 * Process one inbound frame and produce the response, the decrypted payload
 * or a close request. Rejected frames leave the session unchanged apart from
 * the close request, so callers stay fail-closed.
 */
hal_status_t
jh_ble_stream_session_handle_frame(jh_ble_stream_session_t *session,
                                   const uint8_t *frame, size_t length,
                                   jh_ble_stream_session_result_t *out_result);

/** Build one authenticated DATA frame from an application payload. */
hal_status_t jh_ble_stream_session_build_data(jh_ble_stream_session_t *session,
                                              const uint8_t *payload,
                                              size_t length, uint8_t *out_frame,
                                              size_t capacity,
                                              size_t *out_length);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLE_STREAM */
