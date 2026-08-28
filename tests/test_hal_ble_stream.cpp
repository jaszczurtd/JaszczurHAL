#include "ble_stream_test_fixture.h"

#include <thread>

using namespace jh_test_ble_stream;

void setUp(void) { setup_stream(); }

void tearDown(void) {
  (void)hal_ble_stream_deinitialize();
  (void)hal_ble_deinitialize();
  hal_mock_secure_random_reset();
  hal_mock_ble_stream_runtime_full_reset();
  hal_mock_ble_runtime_full_reset();
  hal_mock_board_runtime_full_reset();
}

static void test_publishes_version_and_capabilities(void) {
  uint8_t version = 0u;
  uint16_t capabilities = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_get_stream_published(&version, &capabilities));
  TEST_ASSERT_EQUAL_UINT8(HAL_BLE_STREAM_PROTOCOL_VERSION, version);
  TEST_ASSERT_EQUAL_UINT16(kCapabilities, capabilities);
}

static void test_initial_generation_matches_active_ble_runtime(void) {
  hal_ble_info_t ble{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&ble));
  const hal_ble_stream_info_t stream = info();
  TEST_ASSERT_NOT_EQUAL(0u, ble.generation);
  TEST_ASSERT_EQUAL_UINT32(ble.generation, stream.generation);

  hal_ble_stream_config_t config{};
  config.capabilities = kCapabilities;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_initialize(&config));
  TEST_ASSERT_EQUAL_UINT32(ble.generation, info().generation);

  authenticate();
  const uint8_t payload[] = {0x01u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(payload, sizeof(payload)));
  uint8_t received[HAL_BLE_STREAM_MAX_PAYLOAD]{};
  size_t received_length = 0u;
  hal_ble_stream_payload_info_t payload_info{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_stream_receive_ex(received, sizeof(received),
                                        &received_length, &payload_info));
  TEST_ASSERT_EQUAL_size_t(sizeof(payload), received_length);
  TEST_ASSERT_NOT_EQUAL(0u, payload_info.generation);
  TEST_ASSERT_EQUAL_UINT32(ble.generation, payload_info.generation);
  TEST_ASSERT_NOT_EQUAL(0u, payload_info.session_id);
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
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_tx);
  complete_notification();
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
  for (uint64_t expected_counter = 1u;
       expected_counter <= HAL_BLE_STREAM_TX_QUEUE_DEPTH; ++expected_counter) {
    uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
    size_t frame_length = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_get_stream_frame(
                                      frame, sizeof(frame), &frame_length));
    TEST_ASSERT_EQUAL_UINT64(expected_counter, load_u64(&frame[4]));
    TEST_ASSERT_EQUAL_size_t(HAL_BLE_STREAM_TX_QUEUE_DEPTH -
                                 (size_t)expected_counter + 1u,
                             info().pending_tx);
    complete_notification();
  }
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

static void test_publish_failure_rolls_back_stream_initialization(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_deinitialize());
  hal_ble_stream_config_t config{};
  config.capabilities = kCapabilities;
  hal_mock_ble_set_stream_publish_status(HAL_EIO);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_ble_stream_initialize(&config));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_UNINITIALIZED, info().state);

  uint8_t version = 0xffu;
  uint16_t capabilities = 0xffffu;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_get_stream_published(&version, &capabilities));
  TEST_ASSERT_EQUAL_UINT8(0u, version);
  TEST_ASSERT_EQUAL_UINT16(0u, capabilities);

  hal_mock_ble_set_stream_publish_status(HAL_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_initialize(&config));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_IDLE, info().state);
}

static void test_stream_lifecycle_operations_are_serialized(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_deinitialize());
  hal_ble_stream_config_t config{};
  config.capabilities = kCapabilities;

  hal_status_t initialize_status = HAL_NONE;
  hal_mock_ble_block_stream_publish(true);
  std::thread initializer(
      [&]() { initialize_status = hal_ble_stream_initialize(&config); });
  while (!hal_mock_ble_stream_publish_entered()) {
    std::this_thread::yield();
  }
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_ble_stream_initialize(&config));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_ble_stream_deinitialize());

  hal_mock_ble_block_stream_publish(false);
  initializer.join();
  TEST_ASSERT_EQUAL_INT(HAL_OK, initialize_status);
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_IDLE, info().state);

  hal_status_t deinitialize_status = HAL_NONE;
  hal_mock_ble_block_stream_unpublish(true);
  std::thread deinitializer(
      [&]() { deinitialize_status = hal_ble_stream_deinitialize(); });
  while (!hal_mock_ble_stream_unpublish_entered()) {
    std::this_thread::yield();
  }
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_ble_stream_initialize(&config));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_ble_stream_deinitialize());

  hal_mock_ble_block_stream_unpublish(false);
  deinitializer.join();
  TEST_ASSERT_EQUAL_INT(HAL_OK, deinitialize_status);
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_UNINITIALIZED, info().state);
}

static void test_ble_deinitialize_during_stream_publish_rolls_back(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_deinitialize());
  hal_ble_stream_config_t config{};
  config.capabilities = kCapabilities;

  hal_status_t initialize_status = HAL_NONE;
  hal_mock_ble_block_stream_publish(true);
  std::thread initializer(
      [&]() { initialize_status = hal_ble_stream_initialize(&config); });
  while (!hal_mock_ble_stream_publish_entered()) {
    std::this_thread::yield();
  }

  uint8_t version = 0u;
  uint16_t capabilities = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_get_stream_published(&version, &capabilities));
  TEST_ASSERT_EQUAL_UINT8(HAL_BLE_STREAM_PROTOCOL_VERSION, version);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_deinitialize());

  hal_mock_ble_block_stream_publish(false);
  initializer.join();
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, initialize_status);
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_UNINITIALIZED, info().state);
}

static void test_deinitialize_ignores_late_notification_completion(void) {
  authenticate();
  hal_mock_ble_set_stream_notifications_deferred(true);
  const uint8_t payload[] = {0x19u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_send(payload, sizeof(payload)));
  const size_t notifications = hal_mock_ble_stream_notify_count();
  hal_mock_ble_set_stream_notification_in_progress(true);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_deinitialize());
  const hal_ble_stream_info_t cleared = info();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_UNINITIALIZED, cleared.state);
  TEST_ASSERT_FALSE(cleared.secret_provisioned);
  TEST_ASSERT_EQUAL_size_t(0u, cleared.pending_tx);
  TEST_ASSERT_FALSE(hal_mock_ble_stream_notification_pending());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
  TEST_ASSERT_EQUAL_size_t(notifications, hal_mock_ble_stream_notify_count());
  hal_ble_stream_config_t config{};
  config.capabilities = kCapabilities;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_stream_initialize(&config));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_IDLE, info().state);
}

static void test_unpublish_failure_still_clears_stream_runtime(void) {
  authenticate();
  const uint8_t rx_payload[] = {0x21u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(rx_payload, sizeof(rx_payload)));
  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  const uint8_t tx_payload[] = {0x31u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_stream_send(tx_payload, sizeof(tx_payload)));
  TEST_ASSERT_TRUE(info().secret_provisioned);
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_rx);
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_tx);

  hal_mock_ble_set_stream_unpublish_status(HAL_EIO);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_ble_stream_deinitialize());
  const hal_ble_stream_info_t cleared = info();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_UNINITIALIZED, cleared.state);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, cleared.last_status);
  TEST_ASSERT_FALSE(cleared.secret_provisioned);
  TEST_ASSERT_EQUAL_size_t(0u, cleared.pending_rx);
  TEST_ASSERT_EQUAL_size_t(0u, cleared.pending_tx);
  TEST_ASSERT_EQUAL_UINT64(0u, cleared.session_id);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_UNINITIALIZED, info().state);
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

static void test_all_zero_random_session_id_is_rejected(void) {
  hal_mock_secure_random_set_force_zero(true);
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t length = build_hello(frame);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(frame, length));

  uint8_t response[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t response_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_mock_ble_get_stream_frame(response, sizeof(response),
                                                &response_length));
  const hal_ble_stream_info_t failed = info();
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_HANDSHAKING, failed.state);
  TEST_ASSERT_EQUAL_INT(HAL_EINTERNAL, failed.last_status);
  TEST_ASSERT_EQUAL_UINT64(0u, failed.session_id);

  hal_mock_secure_random_reset();
  hal_mock_secure_random_set_seed(kRandomSeed);
  hal_mock_secure_random_set_status(HAL_OK);
  authenticate();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);
  TEST_ASSERT_NOT_EQUAL(0u, info().session_id);
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

static void test_new_handshake_discards_queued_tx_from_previous_session(void) {
  authenticate();
  const uint8_t old_payload[] = {0x41u, 0x42u, 0x43u};
  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_stream_send(old_payload, sizeof(old_payload)));
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_tx);

  hal_mock_ble_set_stream_notify_status(HAL_OK);
  authenticate();
  const size_t notifications = hal_mock_ble_stream_notify_count();
  TEST_ASSERT_EQUAL_size_t(0u, info().pending_tx);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
  TEST_ASSERT_EQUAL_size_t(notifications, hal_mock_ble_stream_notify_count());
}

static void test_new_handshake_discards_backend_staged_notification(void) {
  authenticate();
  hal_mock_ble_set_stream_notifications_deferred(true);
  const uint8_t old_payload[] = {0x44u, 0x45u, 0x46u};
  const size_t notifications = hal_mock_ble_stream_notify_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_stream_send(old_payload, sizeof(old_payload)));
  TEST_ASSERT_TRUE(hal_mock_ble_stream_notification_pending());
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_tx);
  TEST_ASSERT_EQUAL_size_t(notifications, hal_mock_ble_stream_notify_count());

  uint8_t hello[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t hello_length = build_hello(hello);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(hello, hello_length));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_HANDSHAKING, info().state);
  TEST_ASSERT_TRUE(hal_mock_ble_stream_notification_pending());
  TEST_ASSERT_EQUAL_size_t(notifications, hal_mock_ble_stream_notify_count());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
  TEST_ASSERT_EQUAL_size_t(0u, info().pending_tx);
  TEST_ASSERT_EQUAL_size_t(notifications + 1u,
                           hal_mock_ble_stream_notify_count());
  uint8_t delivered[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t delivered_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_get_stream_frame(delivered, sizeof(delivered),
                                            &delivered_length));
  TEST_ASSERT_EQUAL_UINT8(JH_BLE_STREAM_FRAME_HELLO_ACK, delivered[1]);
}

static void test_new_handshake_retries_when_submission_is_in_progress(void) {
  authenticate();
  const hal_ble_stream_info_t first_session = info();
  hal_mock_ble_set_stream_notifications_deferred(true);
  const uint8_t old_payload[] = {0x54u, 0x55u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_stream_send(old_payload, sizeof(old_payload)));
  hal_mock_ble_set_stream_notification_in_progress(true);

  uint8_t hello[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t hello_length = build_hello(hello);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(hello, hello_length));
  const hal_ble_stream_info_t blocked = info();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, blocked.state);
  TEST_ASSERT_EQUAL_UINT64(first_session.session_id, blocked.session_id);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, blocked.last_status);
  TEST_ASSERT_EQUAL_size_t(1u, blocked.pending_tx);

  hal_mock_ble_set_stream_notification_in_progress(false);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
  TEST_ASSERT_EQUAL_size_t(0u, info().pending_tx);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(hello, hello_length));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_HANDSHAKING, info().state);
}

static void test_discard_failure_closes_session_without_hello_ack(void) {
  authenticate();
  hal_mock_ble_set_stream_notifications_deferred(true);
  const uint8_t old_payload[] = {0x64u};
  const size_t notifications = hal_mock_ble_stream_notify_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_stream_send(old_payload, sizeof(old_payload)));
  hal_mock_ble_set_stream_discard_status(HAL_EIO);

  uint8_t hello[HAL_BLE_STREAM_MAX_FRAME_LEN];
  const size_t hello_length = build_hello(hello);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_stream_frame(hello, hello_length));
  const hal_ble_stream_info_t failed = info();
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, failed.state);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, failed.last_status);
  TEST_ASSERT_EQUAL_UINT64(0u, failed.session_id);
  TEST_ASSERT_EQUAL_size_t(notifications, hal_mock_ble_stream_notify_count());
}

static void test_new_handshake_discards_queued_rx_from_previous_session(void) {
  authenticate();
  const uint8_t old_payload[] = {0x51u, 0x52u, 0x53u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(old_payload, sizeof(old_payload)));
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_rx);

  authenticate();
  TEST_ASSERT_EQUAL_size_t(0u, info().pending_rx);
  uint8_t received[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t received_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN,
      hal_ble_stream_receive(received, sizeof(received), &received_length));
}

static void test_disconnect_closes_the_session(void) {
  authenticate();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_disconnect(0x13u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  const hal_ble_stream_info_t state = info();
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, state.state);
  TEST_ASSERT_FALSE(state.subscribed);
}

static void test_stale_disconnect_does_not_close_reconnected_stream(void) {
  authenticate();
  const uint16_t first_native = hal_mock_ble_native_connection();
  TEST_ASSERT_NOT_EQUAL(0u, first_native);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_disconnect(0x13u));

  connect_and_subscribe();
  const uint16_t second_native = hal_mock_ble_native_connection();
  TEST_ASSERT_NOT_EQUAL(first_native, second_native);
  authenticate();
  const hal_ble_stream_info_t second_session = info();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED,
                        second_session.state);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_inject_delayed_disconnect(first_native, 0x16u));
  const hal_ble_stream_info_t after = info();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, after.state);
  TEST_ASSERT_EQUAL_UINT32(second_session.generation, after.generation);
  TEST_ASSERT_EQUAL_UINT64(second_session.session_id, after.session_id);
  TEST_ASSERT_TRUE(after.subscribed);
}

static void test_ble_deinitialize_clears_authenticated_stream_queues(void) {
  authenticate();
  const uint8_t rx_payload[] = {0x61u, 0x62u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(rx_payload, sizeof(rx_payload)));
  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  const uint8_t tx_payload[] = {0x71u, 0x72u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_stream_send(tx_payload, sizeof(tx_payload)));
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_rx);
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_tx);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_deinitialize());
  const hal_ble_stream_info_t state = info();
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, state.state);
  TEST_ASSERT_EQUAL_size_t(0u, state.pending_rx);
  TEST_ASSERT_EQUAL_size_t(0u, state.pending_tx);
  TEST_ASSERT_EQUAL_UINT64(0u, state.rx_counter);
  TEST_ASSERT_EQUAL_UINT64(0u, state.tx_counter);
  TEST_ASSERT_EQUAL_UINT64(0u, state.session_id);
}

static void test_fatal_ble_service_failure_clears_authenticated_stream(void) {
  authenticate();
  const uint8_t rx_payload[] = {0x81u, 0x82u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(rx_payload, sizeof(rx_payload)));
  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  const uint8_t tx_payload[] = {0x91u, 0x92u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_stream_send(tx_payload, sizeof(tx_payload)));

  hal_ble_info_t before{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&before));
  hal_mock_ble_set_service_status(HAL_EIO);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_ble_poll());

  hal_ble_info_t after{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&after));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_FAILED, after.state);
  TEST_ASSERT_EQUAL_UINT32(before.generation + 1u, after.generation);
  const hal_ble_stream_info_t state = info();
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, state.state);
  TEST_ASSERT_EQUAL_size_t(0u, state.pending_rx);
  TEST_ASSERT_EQUAL_size_t(0u, state.pending_tx);
  TEST_ASSERT_EQUAL_UINT64(0u, state.rx_counter);
  TEST_ASSERT_EQUAL_UINT64(0u, state.tx_counter);
  TEST_ASSERT_EQUAL_UINT64(0u, state.session_id);
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
  RUN_TEST(test_initial_generation_matches_active_ble_runtime);
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
  RUN_TEST(test_publish_failure_rolls_back_stream_initialization);
  RUN_TEST(test_stream_lifecycle_operations_are_serialized);
  RUN_TEST(test_ble_deinitialize_during_stream_publish_rolls_back);
  RUN_TEST(test_deinitialize_ignores_late_notification_completion);
  RUN_TEST(test_unpublish_failure_still_clears_stream_runtime);
  RUN_TEST(test_repeated_failures_trigger_backoff);
  RUN_TEST(test_entropy_failure_is_fail_closed);
  RUN_TEST(test_all_zero_random_session_id_is_rejected);
  RUN_TEST(test_cleared_secret_refuses_handshake);
  RUN_TEST(test_secret_rotation_invalidates_the_session);
  RUN_TEST(test_new_handshake_discards_queued_tx_from_previous_session);
  RUN_TEST(test_new_handshake_discards_backend_staged_notification);
  RUN_TEST(test_new_handshake_retries_when_submission_is_in_progress);
  RUN_TEST(test_discard_failure_closes_session_without_hello_ack);
  RUN_TEST(test_new_handshake_discards_queued_rx_from_previous_session);
  RUN_TEST(test_disconnect_closes_the_session);
  RUN_TEST(test_stale_disconnect_does_not_close_reconnected_stream);
  RUN_TEST(test_ble_deinitialize_clears_authenticated_stream_queues);
  RUN_TEST(test_fatal_ble_service_failure_clears_authenticated_stream);
  RUN_TEST(test_receive_queue_overflow_is_reported);
  RUN_TEST(test_idle_session_expires);
  RUN_TEST(test_backoff_expires_after_its_window);
  RUN_TEST(test_unsubscribe_closes_the_session);
  RUN_TEST(test_deinitialize_unpublishes_and_discards_pending_frames);
  return UNITY_END();
}
