#include "hal/hal_ble.h"
#include "hal/hal_ble_stream.h"
#include "hal/hal_crypto.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/bluetooth/jh_ble_stream_session.h"
#include "utils/unity.h"

#include <string.h>

namespace {

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
};

client_t s_client{};

hal_ble_address_t address(uint8_t tail) {
  hal_ble_address_t value{};
  value.bytes[0] = 0x28u;
  value.bytes[5] = tail;
  value.type = HAL_BLE_ADDRESS_PUBLIC;
  return value;
}

void store_u16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)(value & 0xFFu);
  out[1] = (uint8_t)(value >> 8);
}

void store_u64(uint8_t *out, uint64_t value) {
  for (size_t index = 0u; index < 8u; ++index) {
    out[index] = (uint8_t)(value >> (index * 8u));
  }
}

uint64_t load_u64(const uint8_t *in) {
  uint64_t value = 0u;
  for (size_t index = 0u; index < 8u; ++index) {
    value |= (uint64_t)in[index] << (index * 8u);
  }
  return value;
}

size_t build_transcript(uint8_t domain, uint8_t *out) {
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

void derive(uint8_t domain, uint8_t *out) {
  uint8_t transcript[128];
  const size_t length = build_transcript(domain, transcript);
  TEST_ASSERT_TRUE(hal_hmac_sha256(s_client.secret, sizeof(s_client.secret),
                                   transcript, length, out));
}

void build_nonce(uint8_t direction, uint64_t counter, uint8_t *out) {
  out[0] = direction;
  out[1] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  out[2] = 0u;
  out[3] = 0u;
  store_u64(&out[4], counter);
}

void build_aad(uint8_t direction, uint64_t counter, uint8_t *out) {
  out[0] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  out[1] = JH_BLE_STREAM_FRAME_DATA;
  out[2] = direction;
  out[3] = 0u;
  store_u64(&out[4], counter);
}

size_t build_hello(uint8_t *frame) {
  frame[0] = HAL_BLE_STREAM_PROTOCOL_VERSION;
  frame[1] = JH_BLE_STREAM_FRAME_HELLO;
  frame[2] = 0u;
  frame[3] = (uint8_t)(2u + HAL_BLE_STREAM_NONCE_LEN);
  store_u16(&frame[4], kClientCapabilities);
  memcpy(&frame[6], s_client.client_nonce, HAL_BLE_STREAM_NONCE_LEN);
  return HAL_BLE_STREAM_FRAME_HEADER_LEN + 2u + HAL_BLE_STREAM_NONCE_LEN;
}

/* Read HELLO_ACK from the mock and keep the device nonce and session id. */
void consume_hello_ack(void) {
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
}

size_t build_auth(uint8_t *frame, bool corrupt) {
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

size_t build_data(uint8_t *frame, const uint8_t *payload, size_t length,
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

void connect_and_subscribe(void) {
  const hal_ble_address_t peer = address(0x11u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_connection(&peer));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_inject_mtu(HAL_BLE_STREAM_FULL_PAYLOAD_ATT_MTU));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_subscription(true));
}

/* Drive the handshake to the authenticated state. */
void authenticate(void) {
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t length = build_hello(frame);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  consume_hello_ack();
  length = build_auth(frame, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  derive(0x03u, s_client.key_device_to_client);
  derive(0x04u, s_client.key_client_to_device);
  s_client.tx_counter = 0u;
}

void setup_stream(void) {
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

hal_ble_stream_info_t info(void) {
  hal_ble_stream_info_t value{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_get_info(&value));
  return value;
}

} // namespace

void setUp(void) { setup_stream(); }

void tearDown(void) {
  (void)hal_ble_stream_deinitialize();
  (void)hal_ble_deinitialize();
  hal_mock_secure_random_reset();
}

static void test_publishes_version_and_capabilities(void) {
  uint8_t version = 0u;
  uint16_t capabilities = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_get_stream_published(&version, &capabilities));
  TEST_ASSERT_EQUAL_UINT8(HAL_BLE_STREAM_PROTOCOL_VERSION, version);
  TEST_ASSERT_EQUAL_UINT16(kCapabilities, capabilities);
}

static void test_handshake_authenticates_and_negotiates(void) {
  authenticate();
  const hal_ble_stream_info_t state = info();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, state.state);
  TEST_ASSERT_EQUAL_UINT16(kCapabilities & kClientCapabilities,
                           state.negotiated_capabilities);
  TEST_ASSERT_TRUE(state.secret_provisioned);
  TEST_ASSERT_EQUAL_UINT32(0u, state.auth_failures);
}

static void test_send_before_authentication_is_refused(void) {
  const uint8_t payload[] = {1u, 2u, 3u};
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH,
                        hal_ble_stream_send(payload, sizeof(payload)));
}

static void test_authenticated_send_is_encrypted(void) {
  authenticate();
  const uint8_t payload[] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_send(payload, sizeof(payload)));

  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_get_stream_frame(frame, sizeof(frame), &length));
  TEST_ASSERT_EQUAL_UINT8(JH_BLE_STREAM_FRAME_DATA, frame[1]);

  /* The ciphertext must differ from the plaintext and decrypt with the
     directional key. */
  const uint8_t *ciphertext = &frame[4 + HAL_BLE_STREAM_AEAD_COUNTER_LEN];
  TEST_ASSERT_NOT_EQUAL(0, memcmp(ciphertext, payload, sizeof(payload)));

  uint8_t nonce[HAL_CHACHA20_NONCE_BYTES];
  uint8_t aad[12];
  build_nonce(JH_BLE_STREAM_DIR_DEVICE_TO_CLIENT, 1u, nonce);
  build_aad(JH_BLE_STREAM_DIR_DEVICE_TO_CLIENT, 1u, aad);
  uint8_t plaintext[HAL_BLE_STREAM_MAX_PAYLOAD];
  TEST_ASSERT_TRUE(hal_chacha20_poly1305_decrypt(
      s_client.key_device_to_client, nonce, aad, sizeof(aad), ciphertext,
      sizeof(payload), ciphertext + sizeof(payload), plaintext));
  TEST_ASSERT_EQUAL_INT(0, memcmp(plaintext, payload, sizeof(payload)));
}

static void test_client_data_reaches_the_application(void) {
  authenticate();
  const uint8_t payload[] = {0x10u, 0x20u, 0x30u};
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t length = build_data(frame, payload, sizeof(payload), 1u, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));

  uint8_t received[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t received_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_ble_stream_receive(received, sizeof(received), &received_length));
  TEST_ASSERT_EQUAL_size_t(sizeof(payload), received_length);
  TEST_ASSERT_EQUAL_INT(0, memcmp(received, payload, sizeof(payload)));
}

static void test_wrong_secret_fails_authentication(void) {
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t length = build_hello(frame);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  consume_hello_ack();
  /* Corrupting the proof models a client without the device secret. */
  length = build_auth(frame, true);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));

  const hal_ble_stream_info_t state = info();
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, state.state);
  TEST_ASSERT_EQUAL_UINT32(1u, state.auth_failures);
}

static void test_forged_tag_is_rejected(void) {
  authenticate();
  const uint8_t payload[] = {0x55u, 0x66u};
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t length = build_data(frame, payload, sizeof(payload), 1u, true);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));

  uint8_t received[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t received_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN,
      hal_ble_stream_receive(received, sizeof(received), &received_length));
  const hal_ble_stream_info_t state = info();
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, state.state);
  TEST_ASSERT_EQUAL_UINT32(1u, state.auth_failures);
}

static void test_replayed_counter_closes_the_session(void) {
  authenticate();
  const uint8_t payload[] = {0x77u};
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t length = build_data(frame, payload, sizeof(payload), 1u, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  uint8_t received[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t received_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_ble_stream_receive(received, sizeof(received), &received_length));

  /* Replaying the same counter must be refused. */
  length = build_data(frame, payload, sizeof(payload), 1u, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  const hal_ble_stream_info_t state = info();
  TEST_ASSERT_EQUAL_UINT32(1u, state.replay_rejections);
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, state.state);
}

static void test_counter_gap_is_rejected(void) {
  authenticate();
  const uint8_t payload[] = {0x88u};
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t length = build_data(frame, payload, sizeof(payload), 2u, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  TEST_ASSERT_EQUAL_UINT32(1u, info().replay_rejections);
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);
}

static void test_tx_backpressure_does_not_skip_counter(void) {
  authenticate();
  const uint8_t payload[] = {0x31u};
  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_send(payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_UINT64(0u, info().tx_counter);
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_tx);

  hal_mock_ble_set_stream_notify_status(HAL_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_get_stream_frame(frame, sizeof(frame), &length));
  TEST_ASSERT_EQUAL_UINT64(1u, load_u64(&frame[4]));
  TEST_ASSERT_EQUAL_UINT64(1u, info().tx_counter);
  TEST_ASSERT_EQUAL_size_t(0u, info().pending_tx);
}

static void test_hello_response_survives_backpressure(void) {
  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t length = build_hello(frame);
  const size_t before = hal_mock_ble_stream_notify_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  TEST_ASSERT_EQUAL_size_t(before, hal_mock_ble_stream_notify_count());
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_HANDSHAKING, info().state);

  hal_mock_ble_set_stream_notify_status(HAL_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
  TEST_ASSERT_EQUAL_size_t(before + 1u, hal_mock_ble_stream_notify_count());
  consume_hello_ack();
}

static void test_handshake_refuses_small_mtu(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_mtu(HAL_BLE_DEFAULT_ATT_MTU));
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t length = build_hello(frame);
  const size_t before = hal_mock_ble_stream_notify_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  TEST_ASSERT_EQUAL_size_t(before, hal_mock_ble_stream_notify_count());
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, info().last_status);
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_HANDSHAKING, info().state);
}

static void test_send_rejects_payload_exceeding_negotiated_mtu(void) {
  authenticate();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_mtu(HAL_BLE_STREAM_MIN_ATT_MTU));
  uint8_t payload[51] = {};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_ble_stream_send(payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);
  TEST_ASSERT_EQUAL_size_t(0u, info().pending_tx);
  TEST_ASSERT_EQUAL_UINT64(0u, info().tx_counter);
}

static void test_tx_queue_saturation_recovers_without_counter_gaps(void) {
  authenticate();
  const uint8_t payload[] = {0x42u};
  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  for (size_t index = 0u; index < HAL_BLE_STREAM_TX_QUEUE_DEPTH; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_ble_stream_send(payload, sizeof(payload)));
  }
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        hal_ble_stream_send(payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_size_t(HAL_BLE_STREAM_TX_QUEUE_DEPTH, info().pending_tx);
  TEST_ASSERT_EQUAL_UINT32(1u, info().dropped_tx_frames);
  TEST_ASSERT_EQUAL_UINT64(0u, info().tx_counter);

  hal_mock_ble_set_stream_notify_status(HAL_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
  TEST_ASSERT_EQUAL_size_t(0u, info().pending_tx);
  TEST_ASSERT_EQUAL_UINT64(HAL_BLE_STREAM_TX_QUEUE_DEPTH, info().tx_counter);
}

static void test_tx_backend_failure_is_reported_and_closes_session(void) {
  authenticate();
  const uint8_t payload[] = {0x19u};
  hal_mock_ble_set_stream_notify_status(HAL_EIO);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_ble_stream_send(payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, info().last_status);
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);
  TEST_ASSERT_EQUAL_size_t(0u, info().pending_tx);
}

static void test_counter_exhaustion_fails_closed(void) {
  authenticate();
  jh_ble_stream_session_t session{};
  session.state = JH_BLE_STREAM_SESSION_AUTHENTICATED;
  session.rx_counter = UINT64_MAX - 1u;
  memcpy(session.key_client_to_device, s_client.key_client_to_device,
         sizeof(session.key_client_to_device));

  const uint8_t payload[] = {0x7eu};
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t frame_length =
      build_data(frame, payload, sizeof(payload), UINT64_MAX, false);
  jh_ble_stream_session_result_t result{};
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, jh_ble_stream_session_handle_frame(&session, frame,
                                                     frame_length, &result));
  TEST_ASSERT_TRUE(result.close_session);
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_CLOSE_COUNTER_EXHAUSTED,
                        result.close_reason);

  session.state = JH_BLE_STREAM_SESSION_AUTHENTICATED;
  session.tx_counter = UINT64_MAX - 1u;
  size_t out_length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, jh_ble_stream_session_build_data(
                                           &session, payload, sizeof(payload),
                                           frame, sizeof(frame), &out_length));
}

static void test_initialize_rolls_back_when_ble_is_uninitialized(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_deinitialize());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_deinitialize());
  hal_ble_stream_config_t config{};
  config.capabilities = kCapabilities;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_ble_stream_initialize(&config));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_UNINITIALIZED, info().state);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_initialize(&config));
}

static void test_repeated_failures_trigger_backoff(void) {
  for (uint32_t attempt = 0u; attempt < HAL_BLE_STREAM_AUTH_ATTEMPT_LIMIT;
       ++attempt) {
    uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
    size_t length = build_hello(frame);
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_mock_ble_inject_stream_frame(frame, length));
    consume_hello_ack();
    length = build_auth(frame, true);
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_mock_ble_inject_stream_frame(frame, length));
  }
  const hal_ble_stream_info_t state = info();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_BACKOFF, state.state);
  TEST_ASSERT_EQUAL_UINT32(HAL_BLE_STREAM_AUTH_ATTEMPT_LIMIT,
                           state.auth_failures);

  /* Further handshakes are dropped while the window is open. */
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t length = build_hello(frame);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_BACKOFF, info().state);
}

static void test_entropy_failure_is_fail_closed(void) {
  hal_mock_secure_random_set_status(HAL_EUNSUPPORTED);
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t length = build_hello(frame);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));

  /* No HELLO_ACK may leave the device without a random nonce. */
  uint8_t response[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t response_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_mock_ble_get_stream_frame(response, sizeof(response),
                                                &response_length));
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);
}

static void test_cleared_secret_refuses_handshake(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_clear_secret());
  TEST_ASSERT_FALSE(info().secret_provisioned);

  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t length = build_hello(frame);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));
  uint8_t response[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t response_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_mock_ble_get_stream_frame(response, sizeof(response),
                                                &response_length));
}

static void test_secret_rotation_invalidates_the_session(void) {
  authenticate();
  uint8_t rotated[HAL_BLE_STREAM_SECRET_MIN_LEN];
  memset(rotated, 0x5Au, sizeof(rotated));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_stream_set_secret(rotated, sizeof(rotated)));
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);

  const uint8_t payload[] = {1u};
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH,
                        hal_ble_stream_send(payload, sizeof(payload)));
}

static void test_disconnect_closes_the_session(void) {
  authenticate();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_disconnect(0x13u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  const hal_ble_stream_info_t state = info();
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, state.state);
  TEST_ASSERT_FALSE(state.subscribed);
}

static void test_receive_queue_overflow_is_reported(void) {
  authenticate();
  const uint8_t payload[] = {0x99u};
  for (size_t index = 0u; index < (HAL_BLE_STREAM_RX_QUEUE_DEPTH + 1u);
       ++index) {
    uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
    const size_t length =
        build_data(frame, payload, sizeof(payload), index + 1u, false);
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_mock_ble_inject_stream_frame(frame, length));
  }
  uint8_t received[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t received_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      hal_ble_stream_receive(received, sizeof(received), &received_length));
  /* The retained frames stay readable after the overflow is acknowledged. */
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_ble_stream_receive(received, sizeof(received), &received_length));
}

static void test_idle_session_expires(void) {
  authenticate();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);

  /* Just below the window the session survives a poll. */
  hal_mock_advance_millis(HAL_BLE_STREAM_SESSION_IDLE_TIMEOUT_MS - 1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);

  hal_mock_advance_millis(2u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);

  const uint8_t payload[] = {1u};
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH,
                        hal_ble_stream_send(payload, sizeof(payload)));
}

static void test_backoff_expires_after_its_window(void) {
  for (uint32_t attempt = 0u; attempt < HAL_BLE_STREAM_AUTH_ATTEMPT_LIMIT;
       ++attempt) {
    uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
    size_t length = build_hello(frame);
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_mock_ble_inject_stream_frame(frame, length));
    consume_hello_ack();
    length = build_auth(frame, true);
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_mock_ble_inject_stream_frame(frame, length));
  }
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_BACKOFF, info().state);

  hal_mock_advance_millis(HAL_BLE_STREAM_AUTH_BACKOFF_MS + 1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_BACKOFF, info().state);

  /* A correct handshake works again once the window closed. */
  authenticate();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);
}

static void test_unsubscribe_closes_the_session(void) {
  authenticate();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_subscription(false));
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);
}

static void test_deinitialize_unpublishes_and_discards_pending_frames(void) {
  authenticate();
  const uint8_t payload[] = {0x24u};
  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_send(payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_tx);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_deinitialize());
  const hal_ble_stream_info_t state = info();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_UNINITIALIZED, state.state);
  TEST_ASSERT_EQUAL_size_t(0u, state.pending_tx);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        hal_mock_ble_inject_stream_subscription(true));
  uint8_t version = 0xffu;
  uint16_t capabilities = 0xffffu;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_get_stream_published(&version, &capabilities));
  TEST_ASSERT_EQUAL_UINT8(0u, version);
  TEST_ASSERT_EQUAL_UINT16(0u, capabilities);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_publishes_version_and_capabilities);
  RUN_TEST(test_handshake_authenticates_and_negotiates);
  RUN_TEST(test_send_before_authentication_is_refused);
  RUN_TEST(test_authenticated_send_is_encrypted);
  RUN_TEST(test_client_data_reaches_the_application);
  RUN_TEST(test_wrong_secret_fails_authentication);
  RUN_TEST(test_forged_tag_is_rejected);
  RUN_TEST(test_replayed_counter_closes_the_session);
  RUN_TEST(test_counter_gap_is_rejected);
  RUN_TEST(test_tx_backpressure_does_not_skip_counter);
  RUN_TEST(test_hello_response_survives_backpressure);
  RUN_TEST(test_handshake_refuses_small_mtu);
  RUN_TEST(test_send_rejects_payload_exceeding_negotiated_mtu);
  RUN_TEST(test_tx_queue_saturation_recovers_without_counter_gaps);
  RUN_TEST(test_tx_backend_failure_is_reported_and_closes_session);
  RUN_TEST(test_counter_exhaustion_fails_closed);
  RUN_TEST(test_initialize_rolls_back_when_ble_is_uninitialized);
  RUN_TEST(test_repeated_failures_trigger_backoff);
  RUN_TEST(test_entropy_failure_is_fail_closed);
  RUN_TEST(test_cleared_secret_refuses_handshake);
  RUN_TEST(test_secret_rotation_invalidates_the_session);
  RUN_TEST(test_disconnect_closes_the_session);
  RUN_TEST(test_receive_queue_overflow_is_reported);
  RUN_TEST(test_idle_session_expires);
  RUN_TEST(test_backoff_expires_after_its_window);
  RUN_TEST(test_unsubscribe_closes_the_session);
  RUN_TEST(test_deinitialize_unpublishes_and_discards_pending_frames);
  return UNITY_END();
}
