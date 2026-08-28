#include "jh_ble_stream_session.h"

#ifdef HAL_ENABLE_BLE_STREAM

#include "hal/security/hal_crypto.h"
#include "hal/security/jh_secure_random.h"

#include <string.h>

/* Domain separators keep proofs and directional keys independent. */
enum {
  JH_BLE_STREAM_DOMAIN_DEVICE_PROOF = 0x01,
  JH_BLE_STREAM_DOMAIN_CLIENT_PROOF = 0x02,
  JH_BLE_STREAM_DOMAIN_KEY_D2C = 0x03,
  JH_BLE_STREAM_DOMAIN_KEY_C2D = 0x04
};

#define JH_BLE_STREAM_TRANSCRIPT_LEN                                           \
  (1u + HAL_BLE_STREAM_PROFILE_NAME_LEN + 1u + 2u + 2u +                       \
   HAL_BLE_STREAM_SESSION_ID_LEN + (2u * HAL_BLE_STREAM_NONCE_LEN))

#define JH_BLE_STREAM_AAD_LEN (4u + HAL_BLE_STREAM_AEAD_COUNTER_LEN)

static void store_u16_le(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)(value & 0xFFu);
  out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint16_t load_u16_le(const uint8_t *in) {
  return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

static void store_u64_le(uint8_t *out, uint64_t value) {
  for (size_t index = 0u; index < 8u; ++index) {
    out[index] = (uint8_t)((value >> (index * 8u)) & 0xFFu);
  }
}

static uint64_t load_u64_le(const uint8_t *in) {
  uint64_t value = 0u;
  for (size_t index = 0u; index < 8u; ++index) {
    value |= ((uint64_t)in[index]) << (index * 8u);
  }
  return value;
}

static bool bytes_are_all_zero(const uint8_t *data, size_t length) {
  uint8_t combined = 0u;
  for (size_t index = 0u; index < length; ++index) {
    combined |= data[index];
  }
  return combined == 0u;
}

/* transcript = domain | profile | version | device caps | client caps |
                session id | client nonce | device nonce */
_Static_assert(sizeof(HAL_BLE_STREAM_PROFILE_NAME) - 1u ==
                   HAL_BLE_STREAM_PROFILE_NAME_LEN,
               "profile label length must match its declared size");

static void build_transcript(const jh_ble_stream_session_t *session,
                             uint8_t domain,
                             uint8_t out[JH_BLE_STREAM_TRANSCRIPT_LEN]) {
  size_t offset = 0u;
  out[offset++] = domain;
  /* The label enters the transcript as fixed-length bytes, without a
     terminator. */
  for (size_t index = 0u; index < HAL_BLE_STREAM_PROFILE_NAME_LEN; ++index) {
    out[offset + index] = (uint8_t)HAL_BLE_STREAM_PROFILE_NAME[index];
  }
  offset += HAL_BLE_STREAM_PROFILE_NAME_LEN;
  out[offset++] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  store_u16_le(&out[offset], session->local_capabilities);
  offset += 2u;
  store_u16_le(&out[offset], session->peer_capabilities);
  offset += 2u;
  memcpy(&out[offset], session->session_id, HAL_BLE_STREAM_SESSION_ID_LEN);
  offset += HAL_BLE_STREAM_SESSION_ID_LEN;
  memcpy(&out[offset], session->client_nonce, HAL_BLE_STREAM_NONCE_LEN);
  offset += HAL_BLE_STREAM_NONCE_LEN;
  memcpy(&out[offset], session->device_nonce, HAL_BLE_STREAM_NONCE_LEN);
}

static bool derive(const jh_ble_stream_session_t *session, uint8_t domain,
                   uint8_t out[HAL_SHA256_DIGEST_BYTES]) {
  uint8_t transcript[JH_BLE_STREAM_TRANSCRIPT_LEN];
  build_transcript(session, domain, transcript);
  const bool ok = hal_hmac_sha256(session->secret, session->secret_length,
                                  transcript, sizeof(transcript), out);
  jh_secure_zeroize(transcript, sizeof(transcript));
  return ok;
}

static void build_nonce(jh_ble_stream_direction_t direction, uint64_t counter,
                        uint8_t out[HAL_CHACHA20_NONCE_BYTES]) {
  out[0] = (uint8_t)direction;
  out[1] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  out[2] = 0u;
  out[3] = 0u;
  store_u64_le(&out[4], counter);
}

static void build_aad(uint8_t frame_type, uint8_t flags,
                      jh_ble_stream_direction_t direction, uint64_t counter,
                      uint8_t out[JH_BLE_STREAM_AAD_LEN]) {
  out[0] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  out[1] = frame_type;
  out[2] = (uint8_t)direction;
  out[3] = flags;
  store_u64_le(&out[4], counter);
}

static size_t write_header(uint8_t *frame, uint8_t type, uint8_t flags,
                           size_t body_length) {
  frame[0] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  frame[1] = type;
  frame[2] = flags;
  frame[3] = (uint8_t)body_length;
  return HAL_BLE_STREAM_FRAME_HEADER_LEN;
}

static void close_with(jh_ble_stream_session_result_t *result,
                       hal_ble_stream_close_reason_t reason) {
  result->close_session = true;
  result->close_reason = reason;
}

void jh_ble_stream_session_reset(jh_ble_stream_session_t *session) {
  if (session == NULL) {
    return;
  }
  jh_secure_zeroize(session->session_id, sizeof(session->session_id));
  jh_secure_zeroize(session->client_nonce, sizeof(session->client_nonce));
  jh_secure_zeroize(session->device_nonce, sizeof(session->device_nonce));
  jh_secure_zeroize(session->key_device_to_client,
                    sizeof(session->key_device_to_client));
  jh_secure_zeroize(session->key_client_to_device,
                    sizeof(session->key_client_to_device));
  session->peer_capabilities = 0u;
  session->tx_counter = 0u;
  session->rx_counter = 0u;
  session->state = JH_BLE_STREAM_SESSION_IDLE;
}

void jh_ble_stream_session_clear(jh_ble_stream_session_t *session) {
  if (session == NULL) {
    return;
  }
  jh_ble_stream_session_reset(session);
  jh_secure_zeroize(session->secret, sizeof(session->secret));
  session->secret_length = 0u;
  session->local_capabilities = 0u;
}

hal_status_t jh_ble_stream_session_set_secret(jh_ble_stream_session_t *session,
                                              const uint8_t *secret,
                                              size_t length) {
  if (session == NULL || secret == NULL ||
      length < HAL_BLE_STREAM_SECRET_MIN_LEN ||
      length > HAL_BLE_STREAM_SECRET_MAX_LEN) {
    return HAL_EINVAL;
  }
  jh_ble_stream_session_reset(session);
  jh_secure_zeroize(session->secret, sizeof(session->secret));
  memcpy(session->secret, secret, length);
  session->secret_length = length;
  return HAL_OK;
}

static hal_status_t handle_hello(jh_ble_stream_session_t *session,
                                 const uint8_t *body, size_t body_length,
                                 jh_ble_stream_session_result_t *result) {
  if (body_length != (2u + HAL_BLE_STREAM_NONCE_LEN)) {
    close_with(result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EPROTO;
  }
  if (session->secret_length == 0u) {
    close_with(result, HAL_BLE_STREAM_CLOSE_AUTH_FAILED);
    return HAL_EAUTH;
  }

  jh_ble_stream_session_reset(session);
  session->peer_capabilities = load_u16_le(body);
  memcpy(session->client_nonce, &body[2], HAL_BLE_STREAM_NONCE_LEN);

  /* Device nonce and session id must be unpredictable; without entropy the
     handshake fails closed. */
  if (jh_secure_random_bytes(session->device_nonce, HAL_BLE_STREAM_NONCE_LEN) !=
          HAL_OK ||
      bytes_are_all_zero(session->device_nonce, HAL_BLE_STREAM_NONCE_LEN) ||
      jh_secure_random_bytes(session->session_id,
                             HAL_BLE_STREAM_SESSION_ID_LEN) != HAL_OK ||
      bytes_are_all_zero(session->session_id, HAL_BLE_STREAM_SESSION_ID_LEN)) {
    jh_ble_stream_session_reset(session);
    close_with(result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EINTERNAL;
  }

  uint8_t device_proof[HAL_SHA256_DIGEST_BYTES] = {0u};
  if (!derive(session, JH_BLE_STREAM_DOMAIN_DEVICE_PROOF, device_proof)) {
    jh_secure_zeroize(device_proof, sizeof(device_proof));
    jh_ble_stream_session_reset(session);
    close_with(result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EINTERNAL;
  }

  size_t offset =
      write_header(result->response, JH_BLE_STREAM_FRAME_HELLO_ACK, 0u,
                   2u + HAL_BLE_STREAM_SESSION_ID_LEN +
                       HAL_BLE_STREAM_NONCE_LEN + HAL_BLE_STREAM_PROOF_LEN);
  store_u16_le(&result->response[offset], session->local_capabilities);
  offset += 2u;
  memcpy(&result->response[offset], session->session_id,
         HAL_BLE_STREAM_SESSION_ID_LEN);
  offset += HAL_BLE_STREAM_SESSION_ID_LEN;
  memcpy(&result->response[offset], session->device_nonce,
         HAL_BLE_STREAM_NONCE_LEN);
  offset += HAL_BLE_STREAM_NONCE_LEN;
  memcpy(&result->response[offset], device_proof, HAL_BLE_STREAM_PROOF_LEN);
  offset += HAL_BLE_STREAM_PROOF_LEN;
  result->response_length = offset;
  jh_secure_zeroize(device_proof, sizeof(device_proof));

  session->state = JH_BLE_STREAM_SESSION_HANDSHAKING;
  return HAL_OK;
}

static hal_status_t handle_auth(jh_ble_stream_session_t *session,
                                const uint8_t *body, size_t body_length,
                                jh_ble_stream_session_result_t *result) {
  if (session->state != JH_BLE_STREAM_SESSION_HANDSHAKING) {
    close_with(result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_ESTATE;
  }
  if (body_length != HAL_BLE_STREAM_PROOF_LEN) {
    close_with(result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EPROTO;
  }

  uint8_t expected[HAL_SHA256_DIGEST_BYTES] = {0u};
  if (!derive(session, JH_BLE_STREAM_DOMAIN_CLIENT_PROOF, expected)) {
    jh_secure_zeroize(expected, sizeof(expected));
    jh_ble_stream_session_reset(session);
    close_with(result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EINTERNAL;
  }
  const bool matches =
      jh_constant_time_compare(expected, body, HAL_BLE_STREAM_PROOF_LEN);
  jh_secure_zeroize(expected, sizeof(expected));
  if (!matches) {
    close_with(result, HAL_BLE_STREAM_CLOSE_AUTH_FAILED);
    return HAL_EAUTH;
  }

  if (!derive(session, JH_BLE_STREAM_DOMAIN_KEY_D2C,
              session->key_device_to_client) ||
      !derive(session, JH_BLE_STREAM_DOMAIN_KEY_C2D,
              session->key_client_to_device)) {
    jh_ble_stream_session_reset(session);
    close_with(result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EINTERNAL;
  }

  session->tx_counter = 0u;
  session->rx_counter = 0u;
  session->state = JH_BLE_STREAM_SESSION_AUTHENTICATED;

  size_t offset =
      write_header(result->response, JH_BLE_STREAM_FRAME_AUTH_ACK, 0u, 1u);
  result->response[offset++] = 0u; /* accepted */
  result->response_length = offset;
  return HAL_OK;
}

static hal_status_t handle_data(jh_ble_stream_session_t *session, uint8_t flags,
                                const uint8_t *body, size_t body_length,
                                jh_ble_stream_session_result_t *result) {
  if (session->state != JH_BLE_STREAM_SESSION_AUTHENTICATED) {
    close_with(result, HAL_BLE_STREAM_CLOSE_AUTH_FAILED);
    return HAL_EAUTH;
  }
  const size_t overhead =
      HAL_BLE_STREAM_AEAD_COUNTER_LEN + HAL_BLE_STREAM_AEAD_TAG_LEN;
  if (body_length <= overhead ||
      (body_length - overhead) > HAL_BLE_STREAM_MAX_PAYLOAD) {
    close_with(result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EPROTO;
  }

  const uint64_t counter = load_u64_le(body);
  /* Every authenticated frame consumes exactly one counter value. */
  if (counter == UINT64_MAX || session->rx_counter == (UINT64_MAX - 1u)) {
    close_with(result, HAL_BLE_STREAM_CLOSE_COUNTER_EXHAUSTED);
    return HAL_EPROTO;
  }
  if (counter != (session->rx_counter + 1u)) {
    close_with(result, HAL_BLE_STREAM_CLOSE_REPLAY_DETECTED);
    return HAL_EPROTO;
  }

  const size_t text_length = body_length - overhead;
  const uint8_t *ciphertext = &body[HAL_BLE_STREAM_AEAD_COUNTER_LEN];
  const uint8_t *tag = &body[HAL_BLE_STREAM_AEAD_COUNTER_LEN + text_length];

  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t aad[JH_BLE_STREAM_AAD_LEN];
  build_nonce(JH_BLE_STREAM_DIR_CLIENT_TO_DEVICE, counter, nonce);
  build_aad(JH_BLE_STREAM_FRAME_DATA, flags, JH_BLE_STREAM_DIR_CLIENT_TO_DEVICE,
            counter, aad);

  const bool ok = hal_chacha20_poly1305_decrypt(
      session->key_client_to_device, nonce, aad, sizeof(aad), ciphertext,
      text_length, tag, result->payload);
  jh_secure_zeroize(nonce, sizeof(nonce));
  jh_secure_zeroize(aad, sizeof(aad));
  if (!ok) {
    jh_secure_zeroize(result->payload, sizeof(result->payload));
    close_with(result, HAL_BLE_STREAM_CLOSE_AUTH_FAILED);
    return HAL_EAUTH;
  }

  session->rx_counter = counter;
  result->payload_length = text_length;
  return HAL_OK;
}

hal_status_t
jh_ble_stream_session_handle_frame(jh_ble_stream_session_t *session,
                                   const uint8_t *frame, size_t length,
                                   jh_ble_stream_session_result_t *out_result) {
  if (session == NULL || frame == NULL || out_result == NULL) {
    return HAL_EINVAL;
  }
  memset(out_result, 0, sizeof(*out_result));
  if (length < HAL_BLE_STREAM_FRAME_HEADER_LEN ||
      length > HAL_BLE_STREAM_MAX_FRAME_LEN) {
    close_with(out_result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EPROTO;
  }
  if (frame[0] != HAL_BLE_STREAM_PROTOCOL_VERSION) {
    close_with(out_result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EPROTO;
  }
  const uint8_t type = frame[1];
  const uint8_t flags = frame[2];
  const size_t body_length = frame[3];
  if (body_length != (length - HAL_BLE_STREAM_FRAME_HEADER_LEN)) {
    close_with(out_result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EPROTO;
  }
  const uint8_t *body = &frame[HAL_BLE_STREAM_FRAME_HEADER_LEN];

  switch (type) {
  case JH_BLE_STREAM_FRAME_HELLO:
    return handle_hello(session, body, body_length, out_result);
  case JH_BLE_STREAM_FRAME_AUTH:
    return handle_auth(session, body, body_length, out_result);
  case JH_BLE_STREAM_FRAME_DATA:
    return handle_data(session, flags, body, body_length, out_result);
  case JH_BLE_STREAM_FRAME_CLOSE:
    close_with(out_result, HAL_BLE_STREAM_CLOSE_CLIENT_REQUEST);
    return HAL_OK;
  default:
    close_with(out_result, HAL_BLE_STREAM_CLOSE_PROTOCOL_ERROR);
    return HAL_EPROTO;
  }
}

hal_status_t jh_ble_stream_session_build_data(jh_ble_stream_session_t *session,
                                              const uint8_t *payload,
                                              size_t length, uint8_t *out_frame,
                                              size_t capacity,
                                              size_t *out_length) {
  if (session == NULL || payload == NULL || out_frame == NULL ||
      out_length == NULL || length == 0u ||
      length > HAL_BLE_STREAM_MAX_PAYLOAD) {
    return HAL_EINVAL;
  }
  if (session->state != JH_BLE_STREAM_SESSION_AUTHENTICATED) {
    return HAL_EAUTH;
  }
  const size_t body_length =
      HAL_BLE_STREAM_AEAD_COUNTER_LEN + length + HAL_BLE_STREAM_AEAD_TAG_LEN;
  if (capacity < (HAL_BLE_STREAM_FRAME_HEADER_LEN + body_length)) {
    return HAL_EOVERFLOW;
  }
  if (session->tx_counter >= (UINT64_MAX - 1u)) {
    /* Never reuse a nonce with the same key. */
    return HAL_EOVERFLOW;
  }

  const uint64_t counter = session->tx_counter + 1u;
  size_t offset =
      write_header(out_frame, JH_BLE_STREAM_FRAME_DATA, 0u, body_length);
  store_u64_le(&out_frame[offset], counter);
  offset += HAL_BLE_STREAM_AEAD_COUNTER_LEN;

  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t aad[JH_BLE_STREAM_AAD_LEN];
  build_nonce(JH_BLE_STREAM_DIR_DEVICE_TO_CLIENT, counter, nonce);
  build_aad(JH_BLE_STREAM_FRAME_DATA, 0u, JH_BLE_STREAM_DIR_DEVICE_TO_CLIENT,
            counter, aad);

  const bool ok = hal_chacha20_poly1305_encrypt(
      session->key_device_to_client, nonce, aad, sizeof(aad), payload, length,
      &out_frame[offset], &out_frame[offset + length]);
  jh_secure_zeroize(nonce, sizeof(nonce));
  jh_secure_zeroize(aad, sizeof(aad));
  if (!ok) {
    jh_secure_zeroize(out_frame, HAL_BLE_STREAM_FRAME_HEADER_LEN + body_length);
    return HAL_EINTERNAL;
  }

  session->tx_counter = counter;
  *out_length = HAL_BLE_STREAM_FRAME_HEADER_LEN + body_length;
  return HAL_OK;
}

#endif /* HAL_ENABLE_BLE_STREAM */
