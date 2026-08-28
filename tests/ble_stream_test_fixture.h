#pragma once

#include "hal/bluetooth/hal_ble.h"
#include "hal/bluetooth/hal_ble_stream.h"
#include "hal/bluetooth/jh_ble_stream_session.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/security/hal_crypto.h"
#include "utils/unity.h"

#include <string.h>

namespace jh_test_ble_stream {

constexpr uint16_t kCapabilities =
    HAL_BLE_STREAM_CAP_TELEMETRY | HAL_BLE_STREAM_CAP_COMMISSIONING;
constexpr uint16_t kClientCapabilities = HAL_BLE_STREAM_CAP_TELEMETRY;
constexpr uint64_t kRandomSeed = 0x0123456789ABCDEFull;

/* Mirror of the device transcript, written independently of the session
   module so a silent change in either side fails the test. */
struct client_t {
  uint8_t secret[HAL_BLE_STREAM_SECRET_MIN_LEN];
  uint8_t client_nonce[HAL_BLE_STREAM_NONCE_LEN];
  uint8_t device_nonce[HAL_BLE_STREAM_NONCE_LEN];
  uint8_t session_id[HAL_BLE_STREAM_SESSION_ID_LEN];
  uint16_t device_capabilities;
  uint8_t key_device_to_client[HAL_BLE_STREAM_SESSION_KEY_LEN];
  uint8_t key_client_to_device[HAL_BLE_STREAM_SESSION_KEY_LEN];
  uint64_t tx_counter;
  uint64_t rx_counter;
};

inline client_t s_client{};

inline hal_ble_address_t address(uint8_t tail) {
  hal_ble_address_t value{};
  value.bytes[0] = 0x28u;
  value.bytes[5] = tail;
  value.type = HAL_BLE_ADDRESS_PUBLIC;
  return value;
}

inline void store_u16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)(value & 0xFFu);
  out[1] = (uint8_t)(value >> 8);
}

inline void store_u64(uint8_t *out, uint64_t value) {
  for (size_t index = 0u; index < 8u; ++index) {
    out[index] = (uint8_t)(value >> (index * 8u));
  }
}

inline uint64_t load_u64(const uint8_t *in) {
  uint64_t value = 0u;
  for (size_t index = 0u; index < 8u; ++index) {
    value |= (uint64_t)in[index] << (index * 8u);
  }
  return value;
}

inline size_t build_transcript(uint8_t domain, uint8_t *out) {
  size_t offset = 0u;
  out[offset++] = domain;
  /* Fixed-length label, without a terminator. */
  for (size_t index = 0u; index < HAL_BLE_STREAM_PROFILE_NAME_LEN; ++index) {
    out[offset + index] = (uint8_t)HAL_BLE_STREAM_PROFILE_NAME[index];
  }
  offset += HAL_BLE_STREAM_PROFILE_NAME_LEN;
  out[offset++] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  store_u16(&out[offset], s_client.device_capabilities);
  offset += 2u;
  store_u16(&out[offset], kClientCapabilities);
  offset += 2u;
  memcpy(&out[offset], s_client.session_id, HAL_BLE_STREAM_SESSION_ID_LEN);
  offset += HAL_BLE_STREAM_SESSION_ID_LEN;
  memcpy(&out[offset], s_client.client_nonce, HAL_BLE_STREAM_NONCE_LEN);
  offset += HAL_BLE_STREAM_NONCE_LEN;
  memcpy(&out[offset], s_client.device_nonce, HAL_BLE_STREAM_NONCE_LEN);
  offset += HAL_BLE_STREAM_NONCE_LEN;
  return offset;
}

inline void derive(uint8_t domain, uint8_t *out) {
  uint8_t transcript[128];
  const size_t length = build_transcript(domain, transcript);
  TEST_ASSERT_TRUE(hal_hmac_sha256(s_client.secret, sizeof(s_client.secret),
                                   transcript, length, out));
}

inline void build_nonce(uint8_t direction, uint64_t counter, uint8_t *out) {
  out[0] = direction;
  out[1] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  out[2] = 0u;
  out[3] = 0u;
  store_u64(&out[4], counter);
}

inline void build_aad(uint8_t direction, uint64_t counter, uint8_t *out) {
  out[0] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  out[1] = JH_BLE_STREAM_FRAME_DATA;
  out[2] = direction;
  out[3] = 0u;
  store_u64(&out[4], counter);
}

inline size_t build_hello(uint8_t *frame) {
  frame[0] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  frame[1] = JH_BLE_STREAM_FRAME_HELLO;
  frame[2] = 0u;
  frame[3] = (uint8_t)(2u + HAL_BLE_STREAM_NONCE_LEN);
  store_u16(&frame[4], kClientCapabilities);
  memcpy(&frame[6], s_client.client_nonce, HAL_BLE_STREAM_NONCE_LEN);
  return HAL_BLE_STREAM_FRAME_HEADER_LEN + 2u + HAL_BLE_STREAM_NONCE_LEN;
}

inline void complete_notification(void) {
  TEST_ASSERT_TRUE(hal_mock_ble_stream_notification_pending());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
}

/* Read HELLO_ACK from the mock and keep the device nonce and session id. */
inline void consume_hello_ack(void) {
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_get_stream_frame(frame, sizeof(frame), &length));
  TEST_ASSERT_EQUAL_UINT8(JH_BLE_STREAM_FRAME_HELLO_ACK, frame[1]);
  size_t offset = HAL_BLE_STREAM_FRAME_HEADER_LEN;
  s_client.device_capabilities =
      (uint16_t)(frame[offset] | ((uint16_t)frame[offset + 1u] << 8));
  offset += 2u;
  memcpy(s_client.session_id, &frame[offset], HAL_BLE_STREAM_SESSION_ID_LEN);
  offset += HAL_BLE_STREAM_SESSION_ID_LEN;
  memcpy(s_client.device_nonce, &frame[offset], HAL_BLE_STREAM_NONCE_LEN);
  offset += HAL_BLE_STREAM_NONCE_LEN;

  uint8_t expected_proof[HAL_SHA256_DIGEST_BYTES];
  derive(0x01u, expected_proof);
  TEST_ASSERT_EQUAL_INT(
      0, memcmp(expected_proof, &frame[offset], HAL_BLE_STREAM_PROOF_LEN));
  complete_notification();
}

inline void consume_auth_ack(void) {
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_get_stream_frame(frame, sizeof(frame), &length));
  TEST_ASSERT_EQUAL_size_t(HAL_BLE_STREAM_FRAME_HEADER_LEN + 1u, length);
  TEST_ASSERT_EQUAL_UINT8(HAL_BLE_STREAM_PROTOCOL_VERSION, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(JH_BLE_STREAM_FRAME_AUTH_ACK, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(1u, frame[3]);
  TEST_ASSERT_EQUAL_UINT8(0u, frame[HAL_BLE_STREAM_FRAME_HEADER_LEN]);
  complete_notification();
}

inline size_t build_auth(uint8_t *frame, bool corrupt) {
  frame[0] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  frame[1] = JH_BLE_STREAM_FRAME_AUTH;
  frame[2] = 0u;
  frame[3] = HAL_BLE_STREAM_PROOF_LEN;
  uint8_t proof[HAL_SHA256_DIGEST_BYTES];
  derive(0x02u, proof);
  if (corrupt) {
    proof[0] = (uint8_t)(proof[0] ^ 0xFFu);
  }
  memcpy(&frame[4], proof, HAL_BLE_STREAM_PROOF_LEN);
  return HAL_BLE_STREAM_FRAME_HEADER_LEN + HAL_BLE_STREAM_PROOF_LEN;
}

inline size_t build_data(uint8_t *frame, const uint8_t *payload, size_t length,
                         uint64_t counter, bool forge_tag) {
  const size_t body =
      HAL_BLE_STREAM_AEAD_COUNTER_LEN + length + HAL_BLE_STREAM_AEAD_TAG_LEN;
  frame[0] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  frame[1] = JH_BLE_STREAM_FRAME_DATA;
  frame[2] = 0u;
  frame[3] = (uint8_t)body;
  store_u64(&frame[4], counter);

  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t aad[12];
  build_nonce(JH_BLE_STREAM_DIR_CLIENT_TO_DEVICE, counter, nonce);
  build_aad(JH_BLE_STREAM_DIR_CLIENT_TO_DEVICE, counter, aad);
  uint8_t *ciphertext = &frame[4 + HAL_BLE_STREAM_AEAD_COUNTER_LEN];
  uint8_t *tag = ciphertext + length;
  TEST_ASSERT_TRUE(hal_chacha20_poly1305_encrypt(
      s_client.key_client_to_device, nonce, aad, sizeof(aad), payload, length,
      ciphertext, tag));
  if (forge_tag) {
    tag[0] = (uint8_t)(tag[0] ^ 0xFFu);
  }
  return HAL_BLE_STREAM_FRAME_HEADER_LEN + body;
}

inline void connect_and_subscribe(void) {
  const hal_ble_address_t peer = address(0x11u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_connection(&peer));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_inject_mtu(HAL_BLE_STREAM_FULL_PAYLOAD_ATT_MTU));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_subscription(true));
}

/* Drive the handshake to the authenticated state. */
inline void authenticate(void) {
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t length = build_hello(frame);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  consume_hello_ack();
  length = build_auth(frame, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  consume_auth_ack();
  derive(0x03u, s_client.key_device_to_client);
  derive(0x04u, s_client.key_client_to_device);
  s_client.tx_counter = 0u;
  s_client.rx_counter = 0u;
}

inline void setup_stream(void) {
  hal_mock_ble_reset();
  hal_mock_secure_random_reset();
  hal_mock_secure_random_set_seed(kRandomSeed);
  hal_mock_secure_random_set_status(HAL_OK);
  (void)hal_ble_stream_deinitialize();
  (void)hal_ble_deinitialize();

  memset(&s_client, 0, sizeof(s_client));
  for (size_t index = 0u; index < sizeof(s_client.secret); ++index) {
    s_client.secret[index] = (uint8_t)(0xA0u + index);
  }
  for (size_t index = 0u; index < sizeof(s_client.client_nonce); ++index) {
    s_client.client_nonce[index] = (uint8_t)(index * 3u + 1u);
  }

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  const hal_ble_address_t local = address(0xF8u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_ready(&local));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());

  hal_ble_stream_config_t config{};
  config.capabilities = kCapabilities;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_initialize(&config));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_set_secret(
                                    s_client.secret, sizeof(s_client.secret)));
  connect_and_subscribe();
}

inline hal_ble_stream_info_t info(void) {
  hal_ble_stream_info_t value{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_get_info(&value));
  return value;
}

inline hal_status_t client_send(const void *payload, size_t length) {
  if (payload == nullptr || length == 0u ||
      length > HAL_BLE_STREAM_MAX_PAYLOAD) {
    return HAL_EINVAL;
  }
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const uint64_t counter = s_client.tx_counter + 1u;
  const size_t frame_length = build_data(
      frame, static_cast<const uint8_t *>(payload), length, counter, false);
  const hal_status_t status =
      hal_mock_ble_inject_stream_frame(frame, frame_length);
  if (status == HAL_OK) {
    s_client.tx_counter = counter;
  }
  return status;
}

inline hal_status_t client_receive(void *out, size_t capacity,
                                   size_t *out_length) {
  if (out == nullptr || out_length == nullptr) {
    return HAL_EINVAL;
  }
  *out_length = 0u;
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t frame_length = 0u;
  hal_status_t status =
      hal_mock_ble_get_stream_frame(frame, sizeof(frame), &frame_length);
  if (status != HAL_OK) {
    return status;
  }
  if (frame_length < HAL_BLE_STREAM_FRAME_HEADER_LEN +
                         HAL_BLE_STREAM_AEAD_COUNTER_LEN +
                         HAL_BLE_STREAM_AEAD_TAG_LEN ||
      frame[0] != HAL_BLE_STREAM_PROTOCOL_VERSION ||
      frame[1] != JH_BLE_STREAM_FRAME_DATA) {
    return HAL_EPROTO;
  }
  const size_t payload_length = frame_length - HAL_BLE_STREAM_FRAME_HEADER_LEN -
                                HAL_BLE_STREAM_AEAD_COUNTER_LEN -
                                HAL_BLE_STREAM_AEAD_TAG_LEN;
  if (payload_length > capacity) {
    return HAL_EOVERFLOW;
  }
  const uint64_t counter = load_u64(&frame[HAL_BLE_STREAM_FRAME_HEADER_LEN]);
  if (counter != s_client.rx_counter + 1u) {
    return HAL_EPROTO;
  }
  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t aad[12];
  build_nonce(JH_BLE_STREAM_DIR_DEVICE_TO_CLIENT, counter, nonce);
  build_aad(JH_BLE_STREAM_DIR_DEVICE_TO_CLIENT, counter, aad);
  const uint8_t *ciphertext =
      &frame[HAL_BLE_STREAM_FRAME_HEADER_LEN + HAL_BLE_STREAM_AEAD_COUNTER_LEN];
  const uint8_t *tag = &ciphertext[payload_length];
  if (!hal_chacha20_poly1305_decrypt(s_client.key_device_to_client, nonce, aad,
                                     sizeof(aad), ciphertext, payload_length,
                                     tag, static_cast<uint8_t *>(out))) {
    return HAL_EAUTH;
  }
  s_client.rx_counter = counter;
  *out_length = payload_length;
  complete_notification();
  return HAL_OK;
}

} // namespace jh_test_ble_stream
