#include "hal/impl/.mock/hal_mock.h"
#include "hal/radio/hal_lora_link.h"
#include "hal/radio/jh_lora_link_frame.h"
#include "hal/security/hal_crc.h"
#include "lora_test_fixture.h"
#include "utils/unity.h"

#include <atomic>
#include <string.h>
#include <thread>

static const uint8_t kKey[HAL_LORA_LINK_CRYPTO_KEY_BYTES] = {
    0x80u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u, 0x88u, 0x89u, 0x8Au,
    0x8Bu, 0x8Cu, 0x8Du, 0x8Eu, 0x8Fu, 0x90u, 0x91u, 0x92u, 0x93u, 0x94u, 0x95u,
    0x96u, 0x97u, 0x98u, 0x99u, 0x9Au, 0x9Bu, 0x9Cu, 0x9Du, 0x9Eu, 0x9Fu,
};

typedef struct {
  hal_lora_radio_t radio;
  hal_lora_link_t link;
} endpoint_t;

static endpoint_t create_endpoint_from(uint16_t address, uint32_t session_id,
                                       bool encrypted, uint8_t max_retries,
                                       uint32_t ack_timeout_ms,
                                       uint32_t initial_sequence) {
  endpoint_t endpoint = {};
  const hal_lora_radio_config_t hardware = jh_test_lora_radio_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_create(&hardware, &endpoint.radio));
  const hal_lora_modem_config_t modem = hal_lora_default_eu868();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_configure(endpoint.radio, &modem));
  hal_lora_link_config_t link_config =
      hal_lora_link_config_defaults(endpoint.radio, address, session_id);
  link_config.max_retries = max_retries;
  link_config.acknowledgement_timeout_ms = ack_timeout_ms;
  link_config.retry_backoff_ms = 0u;
  link_config.initial_sequence = initial_sequence;
  if (encrypted) {
    link_config.security = HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305;
    link_config.key = kKey;
    link_config.key_length = sizeof(kKey);
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_create(&link_config, &endpoint.link));
  return endpoint;
}

static endpoint_t create_endpoint(uint16_t address, uint32_t session_id,
                                  bool encrypted, uint8_t max_retries,
                                  uint32_t ack_timeout_ms) {
  return create_endpoint_from(address, session_id, encrypted, max_retries,
                              ack_timeout_ms, 1u);
}

static void destroy_endpoint(endpoint_t *endpoint) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_destroy(endpoint->link));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(endpoint->radio));
  *endpoint = {};
}

static hal_lora_operation_state_t send_state(hal_lora_link_t link) {
  hal_lora_link_send_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_get_send_status(link, &status));
  return status.state;
}

static void pump(endpoint_t *first, endpoint_t *second, uint32_t iterations) {
  for (uint32_t iteration = 0u; iteration < iterations; ++iteration) {
    const hal_status_t first_status = hal_lora_link_process(first->link);
    const hal_status_t second_status = hal_lora_link_process(second->link);
    TEST_ASSERT_TRUE(first_status == HAL_OK || first_status == HAL_EAGAIN ||
                     first_status == HAL_ETIMEOUT ||
                     first_status == HAL_IGNORED);
    TEST_ASSERT_TRUE(second_status == HAL_OK || second_status == HAL_EAGAIN ||
                     second_status == HAL_ETIMEOUT ||
                     second_status == HAL_IGNORED);
    hal_mock_advance_millis(1u);
  }
}

void setUp(void) {
  hal_mock_set_millis(0u);
  hal_mock_lora_reset();
}

void tearDown(void) {
  hal_mock_lora_reset();
  hal_mock_lora_link_full_reset();
  hal_mock_lora_radio_dispatch_full_reset();
}

void test_defaults_validation_lifecycle_and_stale_handles(void) {
  const hal_lora_link_config_t defaults =
      hal_lora_link_config_defaults(NULL, 1u, 2u);
  TEST_ASSERT_EQUAL_UINT32(1u, defaults.initial_sequence);
  TEST_ASSERT_EQUAL_UINT8(3u, defaults.max_retries);
  TEST_ASSERT_EQUAL_UINT32(1500u, defaults.acknowledgement_timeout_ms);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_LINK_SECURITY_NONE, defaults.security);

  const hal_lora_radio_config_t hardware = jh_test_lora_radio_config();
  hal_lora_radio_t radio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_create(&hardware, &radio));
  const hal_lora_modem_config_t modem = hal_lora_default_eu868();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_configure(radio, &modem));
  hal_lora_link_config_t config = hal_lora_link_config_defaults(radio, 1u, 2u);
  hal_lora_link_t link = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_link_create(NULL, &link));
  config.session_id = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_link_create(&config, &link));
  config.session_id = 2u;
  config.security = HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_link_create(&config, &link));
  config.security = HAL_LORA_LINK_SECURITY_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_create(&config, &link));
  const hal_lora_link_t stale = link;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_destroy(link));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_lora_link_destroy(stale));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(radio));
}

void test_fragmented_plaintext_message_without_acknowledgement(void) {
  endpoint_t first = create_endpoint(1u, 101u, false, 0u, 10u);
  endpoint_t second = create_endpoint(2u, 202u, false, 0u, 10u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));
  uint8_t message[600] = {};
  for (size_t index = 0u; index < sizeof(message); ++index) {
    message[index] = (uint8_t)(index * 7u);
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_send_start(first.link, 2u, 9u, message,
                                                 sizeof(message), false));
  pump(&first, &second, 12u);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_SUCCEEDED, send_state(first.link));

  uint8_t received[sizeof(message)] = {};
  size_t received_length = 0u;
  hal_lora_link_message_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_receive(second.link, received,
                                                      sizeof(received),
                                                      &received_length, &info));
  TEST_ASSERT_EQUAL_UINT(sizeof(message), received_length);
  TEST_ASSERT_EQUAL_MEMORY(message, received, sizeof(message));
  TEST_ASSERT_EQUAL_UINT16(1u, info.source);
  TEST_ASSERT_EQUAL_UINT16(2u, info.destination);
  TEST_ASSERT_EQUAL_UINT8(9u, info.port);
  TEST_ASSERT_EQUAL_UINT8(3u, info.fragment_count);
  TEST_ASSERT_FALSE(info.encrypted);

  hal_lora_link_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_link_get_diagnostics(first.link, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.transmitted_messages);
  TEST_ASSERT_EQUAL_UINT32(3u, diagnostics.transmitted_frames);
  destroy_endpoint(&first);
  destroy_endpoint(&second);
}

void test_lost_ack_retransmits_and_duplicate_is_delivered_once(void) {
  endpoint_t first = create_endpoint(1u, 101u, false, 2u, 5u);
  endpoint_t second = create_endpoint(2u, 202u, false, 2u, 5u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_drop_next_transmits(second.radio, 1u));
  uint8_t message[400] = {};
  memset(message, 0xA5, sizeof(message));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_send_start(first.link, 2u, 1u, message,
                                                 sizeof(message), true));
  pump(&first, &second, 30u);
  hal_lora_link_send_status_t send = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_get_send_status(first.link, &send));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_SUCCEEDED, send.state);
  TEST_ASSERT_EQUAL_UINT8(2u, send.attempts);

  uint8_t received[sizeof(message)] = {};
  size_t received_length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_receive(second.link, received,
                                                      sizeof(received),
                                                      &received_length, NULL));
  TEST_ASSERT_EQUAL_MEMORY(message, received, sizeof(message));
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_lora_link_receive(second.link, received, sizeof(received),
                                        &received_length, NULL));

  hal_lora_link_diagnostics_t first_diagnostics = {};
  hal_lora_link_diagnostics_t second_diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_link_get_diagnostics(first.link, &first_diagnostics));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_link_get_diagnostics(second.link, &second_diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, first_diagnostics.retransmissions);
  TEST_ASSERT_EQUAL_UINT32(1u, first_diagnostics.acknowledgements_received);
  TEST_ASSERT_EQUAL_UINT32(1u, second_diagnostics.received_messages);
  TEST_ASSERT_EQUAL_UINT32(1u, second_diagnostics.duplicate_messages);
  TEST_ASSERT_EQUAL_UINT32(2u, second_diagnostics.acknowledgements_sent);
  destroy_endpoint(&first);
  destroy_endpoint(&second);
}

void test_missing_ack_reaches_bounded_timeout(void) {
  endpoint_t first = create_endpoint(1u, 101u, false, 1u, 5u);
  endpoint_t second = create_endpoint(2u, 202u, false, 1u, 5u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_drop_next_transmits(second.radio, 2u));
  const uint8_t message[] = {'n', 'o', '-', 'a', 'c', 'k'};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_send_start(first.link, 2u, 1u, message,
                                                 sizeof(message), true));
  pump(&first, &second, 30u);
  hal_lora_link_send_status_t send = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_get_send_status(first.link, &send));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_TIMED_OUT, send.state);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, send.result);
  TEST_ASSERT_EQUAL_UINT8(2u, send.attempts);
  destroy_endpoint(&first);
  destroy_endpoint(&second);
}

void test_later_fragment_start_failure_finishes_send(void) {
  endpoint_t first = create_endpoint(1u, 101u, false, 0u, 10u);
  endpoint_t second = create_endpoint(2u, 202u, false, 0u, 10u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));
  uint8_t message[400] = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_send_start(first.link, 2u, 1u, message,
                                                 sizeof(message), false));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_lora_set_next_status(first.radio, HAL_MOCK_LORA_TRANSMIT,
                                            HAL_EIO));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_lora_link_process(first.link));

  hal_lora_link_send_status_t send = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_get_send_status(first.link, &send));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_FAILED, send.state);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, send.result);
  hal_lora_link_state_t state = HAL_LORA_LINK_STATE_RECEIVING;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_get_state(first.link, &state));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_LINK_STATE_RECEIVING, state);
  destroy_endpoint(&first);
  destroy_endpoint(&second);
}

void test_reassembly_completed_out_of_order_defers_ack_until_last_fragment(
    void) {
  endpoint_t endpoint = create_endpoint(2u, 202u, false, 0u, 10u);
  uint8_t first_payload[JH_LORA_LINK_FRAME_MAX_PLAINTEXT] = {};
  memset(first_payload, 0x5Au, sizeof(first_payload));
  const uint8_t last_payload[] = {0xC3u};
  uint8_t complete_message[sizeof(first_payload) + sizeof(last_payload)] = {};
  memcpy(complete_message, first_payload, sizeof(first_payload));
  memcpy(&complete_message[sizeof(first_payload)], last_payload,
         sizeof(last_payload));

  jh_lora_link_frame_header_t header = {};
  header.flags = JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST;
  header.source = 1u;
  header.destination = 2u;
  header.session_id = 101u;
  header.sequence = 9u;
  header.fragment_count = 2u;
  header.message_length = sizeof(complete_message);
  header.integrity = hal_crc32(complete_message, sizeof(complete_message));
  uint8_t first_frame[HAL_LORA_RADIO_MAX_PAYLOAD] = {};
  uint8_t last_frame[HAL_LORA_RADIO_MAX_PAYLOAD] = {};
  size_t first_frame_length = 0u;
  size_t last_frame_length = 0u;
  header.fragment_index = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lora_link_frame_encode(
                                    &header, first_payload,
                                    sizeof(first_payload), NULL, first_frame,
                                    sizeof(first_frame), &first_frame_length));
  header.fragment_index = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lora_link_frame_encode(
                                    &header, last_payload, sizeof(last_payload),
                                    NULL, last_frame, sizeof(last_frame),
                                    &last_frame_length));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_inject_receive(endpoint.radio, last_frame,
                                                     last_frame_length, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_process(endpoint.link));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_lora_inject_receive(endpoint.radio, first_frame,
                                           first_frame_length, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_process(endpoint.link));
  hal_lora_link_state_t state = HAL_LORA_LINK_STATE_ERROR;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_get_state(endpoint.link, &state));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_LINK_STATE_RECEIVING, state);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_inject_receive(endpoint.radio, last_frame,
                                                     last_frame_length, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_process(endpoint.link));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_get_state(endpoint.link, &state));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_LINK_STATE_SENDING_ACKNOWLEDGEMENT, state);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_process(endpoint.link));
  hal_lora_link_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_link_get_diagnostics(endpoint.link, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.acknowledgements_sent);
  destroy_endpoint(&endpoint);
}

void test_encrypted_fragmented_message_and_authenticated_ack(void) {
  endpoint_t first = create_endpoint(1u, 1001u, true, 1u, 10u);
  endpoint_t second = create_endpoint(2u, 2002u, true, 1u, 10u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));
  uint8_t message[500] = {};
  for (size_t index = 0u; index < sizeof(message); ++index) {
    message[index] = (uint8_t)(index ^ 0x5Au);
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_send_start(first.link, 2u, 4u, message,
                                                 sizeof(message), true));
  pump(&first, &second, 20u);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_SUCCEEDED, send_state(first.link));
  uint8_t received[sizeof(message)] = {};
  size_t received_length = 0u;
  hal_lora_link_message_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_receive(second.link, received,
                                                      sizeof(received),
                                                      &received_length, &info));
  TEST_ASSERT_EQUAL_MEMORY(message, received, sizeof(message));
  TEST_ASSERT_TRUE(info.encrypted);
  TEST_ASSERT_EQUAL_UINT8(3u, info.fragment_count);
  destroy_endpoint(&first);
  destroy_endpoint(&second);
}

void test_invalid_message_crc_is_rejected_and_counted(void) {
  endpoint_t endpoint = create_endpoint(2u, 202u, false, 0u, 10u);
  const uint8_t payload[] = {'b', 'a', 'd'};
  jh_lora_link_frame_header_t header = {};
  header.source = 1u;
  header.destination = 2u;
  header.session_id = 101u;
  header.sequence = 7u;
  header.fragment_count = 1u;
  header.message_length = sizeof(payload);
  header.integrity = 1u;
  uint8_t frame[HAL_LORA_RADIO_MAX_PAYLOAD] = {};
  size_t frame_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lora_link_frame_encode(&header, payload, sizeof(payload), NULL,
                                        frame, sizeof(frame), &frame_length));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_lora_inject_receive(
                                    endpoint.radio, frame, frame_length, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, hal_lora_link_process(endpoint.link));
  hal_lora_link_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_link_get_diagnostics(endpoint.link, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.integrity_failures);
  uint8_t received[8] = {};
  size_t received_length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        hal_lora_link_receive(endpoint.link, received,
                                              sizeof(received),
                                              &received_length, NULL));
  destroy_endpoint(&endpoint);
}

void test_incomplete_reassembly_expires(void) {
  endpoint_t endpoint = create_endpoint(2u, 202u, false, 0u, 10u);
  uint8_t payload[JH_LORA_LINK_FRAME_MAX_PLAINTEXT] = {};
  jh_lora_link_frame_header_t header = {};
  header.source = 1u;
  header.destination = 2u;
  header.session_id = 101u;
  header.sequence = 8u;
  header.fragment_count = 2u;
  header.message_length = sizeof(payload) + 1u;
  header.integrity = 1u;
  uint8_t frame[HAL_LORA_RADIO_MAX_PAYLOAD] = {};
  size_t frame_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lora_link_frame_encode(&header, payload, sizeof(payload), NULL,
                                        frame, sizeof(frame), &frame_length));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_lora_inject_receive(
                                    endpoint.radio, frame, frame_length, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_process(endpoint.link));
  hal_mock_advance_millis(5000u);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_lora_link_process(endpoint.link));

  hal_lora_link_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_link_get_diagnostics(endpoint.link, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.reassembly_timeouts);
  destroy_endpoint(&endpoint);
}

void test_corrupt_packet_during_ack_wait_does_not_fail_send(void) {
  endpoint_t endpoint = create_endpoint(1u, 101u, false, 0u, 5u);
  const uint8_t payload[] = {0x42u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_send_start(endpoint.link, 2u, 1u, payload,
                                                 sizeof(payload), true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_process(endpoint.link));
  const uint8_t corrupt_packet[] = {0xFFu};
  hal_lora_packet_info_t packet_info = {};
  packet_info.crc_valid = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_lora_inject_receive(
                                    endpoint.radio, corrupt_packet,
                                    sizeof(corrupt_packet), &packet_info));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_lora_link_process(endpoint.link));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_IN_PROGRESS,
                        send_state(endpoint.link));
  hal_mock_advance_millis(5u);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, hal_lora_link_process(endpoint.link));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_TIMED_OUT,
                        send_state(endpoint.link));
  hal_lora_link_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_link_get_diagnostics(endpoint.link, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.malformed_frames);
  destroy_endpoint(&endpoint);
}

void test_maximum_retry_count_reports_all_attempts(void) {
  endpoint_t endpoint = create_endpoint(1u, 101u, false, UINT8_MAX, 1u);
  const uint8_t payload[] = {0x42u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_send_start(endpoint.link, 2u, 1u, payload,
                                                 sizeof(payload), true));
  for (uint16_t attempt = 0u; attempt <= UINT8_MAX; ++attempt) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_process(endpoint.link));
    hal_mock_advance_millis(1u);
    const hal_status_t expected = attempt == UINT8_MAX ? HAL_ETIMEOUT : HAL_OK;
    TEST_ASSERT_EQUAL_INT(expected, hal_lora_link_process(endpoint.link));
  }
  hal_lora_link_send_status_t send = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_get_send_status(endpoint.link, &send));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_TIMED_OUT, send.state);
  TEST_ASSERT_EQUAL_UINT16(256u, send.attempts);
  destroy_endpoint(&endpoint);
}

void test_concurrent_send_start_serializes_one_link(void) {
  endpoint_t first = create_endpoint(1u, 101u, false, 0u, 10u);
  endpoint_t second = create_endpoint(2u, 202u, false, 0u, 10u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));
  const uint8_t payload[] = {0x42u};
  std::atomic<bool> start{false};
  hal_status_t results[2] = {HAL_NONE, HAL_NONE};
  auto worker = [&](size_t index) {
    while (!start.load(std::memory_order_acquire)) {
    }
    results[index] = hal_lora_link_send_start(first.link, 2u, 1u, payload,
                                              sizeof(payload), false);
  };
  std::thread left(worker, 0u);
  std::thread right(worker, 1u);
  start.store(true, std::memory_order_release);
  left.join();
  right.join();
  const bool serialized = (results[0] == HAL_OK && results[1] == HAL_EBUSY) ||
                          (results[1] == HAL_OK && results[0] == HAL_EBUSY);
  TEST_ASSERT_TRUE(serialized);
  pump(&first, &second, 5u);
  destroy_endpoint(&first);
  destroy_endpoint(&second);
}

/* Decode the acknowledgement the receiver transmitted last. */
static jh_lora_link_frame_header_t
last_acknowledgement(endpoint_t *receiver, bool encrypted,
                     uint32_t *out_acknowledged_session,
                     uint32_t *out_acknowledged_sequence) {
  uint8_t frame[HAL_LORA_RADIO_MAX_PAYLOAD] = {};
  size_t frame_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_lora_get_last_transmit(receiver->radio, frame,
                                              sizeof(frame), &frame_length));
  jh_lora_link_frame_header_t header = {};
  uint8_t body[JH_LORA_LINK_FRAME_ACK_PAYLOAD_SIZE] = {};
  size_t body_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      jh_lora_link_frame_decode(frame, frame_length, encrypted ? kKey : NULL,
                                &header, body, sizeof(body), &body_length));
  TEST_ASSERT_EQUAL_HEX8(JH_LORA_LINK_FRAME_FLAG_ACK,
                         header.flags & JH_LORA_LINK_FRAME_FLAG_ACK);
  TEST_ASSERT_TRUE(jh_lora_link_frame_decode_ack_body(
      body, body_length, out_acknowledged_session, out_acknowledged_sequence));
  return header;
}

static void send_acknowledged_byte(endpoint_t *sender, endpoint_t *receiver,
                                   uint8_t marker) {
  const uint8_t message[] = {marker, 22u, 33u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_send_start(sender->link, 2u, 9u, message,
                                                 sizeof(message), true));
  pump(sender, receiver, 16u);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_SUCCEEDED, send_state(sender->link));
  uint8_t received[8] = {};
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_receive(receiver->link, received,
                                              sizeof(received), &length, NULL));
  TEST_ASSERT_EQUAL_UINT8(marker, received[0]);
}

/* Audit F01: the acknowledgement nonce used to copy the acknowledged
 * sequence, so a restarted sender (new session, sequence 1 again) made the
 * receiver protect a different header under a nonce it had already used. */
void test_acknowledgements_take_the_acknowledger_sequence(void) {
  endpoint_t receiver = create_endpoint(2u, 202u, true, 0u, 20u);
  endpoint_t sender = create_endpoint(1u, 101u, true, 0u, 20u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(sender.radio, receiver.radio));
  uint32_t acknowledged_session = 0u;
  uint32_t acknowledged_sequence = 0u;
  send_acknowledged_byte(&sender, &receiver, 1u);
  const jh_lora_link_frame_header_t first = last_acknowledgement(
      &receiver, true, &acknowledged_session, &acknowledged_sequence);
  TEST_ASSERT_EQUAL_UINT32(101u, acknowledged_session);
  TEST_ASSERT_EQUAL_UINT32(1u, acknowledged_sequence);
  send_acknowledged_byte(&sender, &receiver, 2u);
  const jh_lora_link_frame_header_t second = last_acknowledgement(
      &receiver, true, &acknowledged_session, &acknowledged_sequence);
  TEST_ASSERT_EQUAL_UINT32(2u, acknowledged_sequence);

  destroy_endpoint(&sender);
  sender = create_endpoint(1u, 303u, true, 0u, 20u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(sender.radio, receiver.radio));
  send_acknowledged_byte(&sender, &receiver, 3u);
  const jh_lora_link_frame_header_t third = last_acknowledgement(
      &receiver, true, &acknowledged_session, &acknowledged_sequence);
  TEST_ASSERT_EQUAL_UINT32(303u, acknowledged_session);
  TEST_ASSERT_EQUAL_UINT32(1u, acknowledged_sequence);

  /* One acknowledger session and address, three distinct nonces. */
  TEST_ASSERT_EQUAL_UINT32(202u, first.session_id);
  TEST_ASSERT_EQUAL_UINT32(202u, third.session_id);
  TEST_ASSERT_EQUAL_UINT16(2u, third.source);
  TEST_ASSERT_NOT_EQUAL_UINT32(first.sequence, second.sequence);
  TEST_ASSERT_NOT_EQUAL_UINT32(first.sequence, third.sequence);
  TEST_ASSERT_NOT_EQUAL_UINT32(second.sequence, third.sequence);
  TEST_ASSERT_EQUAL_UINT32(0u, third.integrity);
  destroy_endpoint(&sender);
  destroy_endpoint(&receiver);
}

/* Deliver one crafted acknowledgement to a waiting sender. */
static void inject_acknowledgement(endpoint_t *sender,
                                   uint32_t acknowledged_session,
                                   uint32_t acknowledged_sequence,
                                   uint32_t acknowledgement_sequence) {
  uint8_t body[JH_LORA_LINK_FRAME_ACK_PAYLOAD_SIZE] = {};
  jh_lora_link_frame_encode_ack_body(acknowledged_session,
                                     acknowledged_sequence, body);
  jh_lora_link_frame_header_t header = {};
  header.flags =
      JH_LORA_LINK_FRAME_FLAG_ACK | JH_LORA_LINK_FRAME_FLAG_ENCRYPTED;
  header.source = 2u;
  header.destination = 1u;
  header.session_id = 202u;
  header.sequence = acknowledgement_sequence;
  header.fragment_count = 1u;
  header.message_length = JH_LORA_LINK_FRAME_ACK_PAYLOAD_SIZE;
  uint8_t frame[HAL_LORA_RADIO_MAX_PAYLOAD] = {};
  size_t frame_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lora_link_frame_encode(&header, body, sizeof(body), kKey,
                                        frame, sizeof(frame), &frame_length));
  hal_lora_packet_info_t info = {};
  info.rssi_dbm = -70;
  info.snr_db = 8;
  info.crc_valid = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_lora_inject_receive(
                                    sender->radio, frame, frame_length, &info));
  const hal_status_t status = hal_lora_link_process(sender->link);
  TEST_ASSERT_TRUE(status == HAL_OK || status == HAL_EAGAIN);
  hal_mock_advance_millis(1u);
}

/* Only the body naming this link's session and the message in flight
 * completes the send; a valid acknowledgement of anything else is ignored. */
void test_acknowledgement_of_another_session_or_message_is_ignored(void) {
  endpoint_t sender = create_endpoint(1u, 101u, true, 0u, 50u);
  endpoint_t receiver = create_endpoint(2u, 202u, true, 0u, 50u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(sender.radio, receiver.radio));
  /* The receiver's own acknowledgement is lost; crafted ones follow. */
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_drop_next_transmits(receiver.radio, 1u));
  const uint8_t message[] = {5u, 6u, 7u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_send_start(sender.link, 2u, 9u, message,
                                                 sizeof(message), true));
  hal_lora_link_state_t state = HAL_LORA_LINK_STATE_ERROR;
  for (int iteration = 0;
       iteration < 8 && state != HAL_LORA_LINK_STATE_WAITING_ACKNOWLEDGEMENT;
       ++iteration) {
    pump(&sender, &receiver, 1u);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_get_state(sender.link, &state));
  }
  TEST_ASSERT_EQUAL_INT(HAL_LORA_LINK_STATE_WAITING_ACKNOWLEDGEMENT, state);
  hal_lora_link_send_status_t send = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_get_send_status(sender.link, &send));

  inject_acknowledgement(&sender, 999u, send.sequence, 70u);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_IN_PROGRESS,
                        send_state(sender.link));
  inject_acknowledgement(&sender, 101u, send.sequence + 1u, 71u);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_IN_PROGRESS,
                        send_state(sender.link));
  inject_acknowledgement(&sender, 101u, send.sequence, 72u);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_SUCCEEDED, send_state(sender.link));
  hal_lora_link_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_link_get_diagnostics(sender.link, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.acknowledgements_received);
  destroy_endpoint(&sender);
  destroy_endpoint(&receiver);
}

/* Acknowledgements spend the same sequence space as messages. Once an
 * encrypted link has used UINT32_MAX it still delivers but no longer
 * acknowledges or sends, instead of repeating a nonce. */
void test_exhausted_sequence_space_stops_acknowledgements(void) {
  endpoint_t receiver =
      create_endpoint_from(2u, 202u, true, 0u, 20u, UINT32_MAX - 1u);
  endpoint_t sender = create_endpoint(1u, 101u, true, 0u, 20u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(sender.radio, receiver.radio));
  send_acknowledged_byte(&sender, &receiver, 1u); /* ACK UINT32_MAX - 1 */
  send_acknowledged_byte(&sender, &receiver, 2u); /* ACK UINT32_MAX */

  const uint8_t message[] = {3u, 0u, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_send_start(sender.link, 2u, 9u, message,
                                                 sizeof(message), true));
  bool refused = false;
  for (uint32_t iteration = 0u; iteration < 40u; ++iteration) {
    (void)hal_lora_link_process(sender.link);
    const hal_status_t receiver_status = hal_lora_link_process(receiver.link);
    if (receiver_status == HAL_EOVERFLOW) {
      refused = true;
    } else {
      TEST_ASSERT_TRUE(receiver_status == HAL_OK ||
                       receiver_status == HAL_EAGAIN ||
                       receiver_status == HAL_IGNORED);
    }
    hal_mock_advance_millis(1u);
  }
  TEST_ASSERT_TRUE(refused);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_TIMED_OUT, send_state(sender.link));
  uint8_t received[8] = {};
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_receive(receiver.link, received,
                                              sizeof(received), &length, NULL));
  TEST_ASSERT_EQUAL_UINT8(3u, received[0]);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_lora_link_send_start(receiver.link, 1u, 9u, message,
                                                 sizeof(message), false));
  hal_lora_link_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_link_get_diagnostics(receiver.link, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(2u, diagnostics.acknowledgements_sent);
  TEST_ASSERT_EQUAL_UINT32(3u, diagnostics.received_messages);
  destroy_endpoint(&sender);
  destroy_endpoint(&receiver);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_validation_lifecycle_and_stale_handles);
  RUN_TEST(test_fragmented_plaintext_message_without_acknowledgement);
  RUN_TEST(test_lost_ack_retransmits_and_duplicate_is_delivered_once);
  RUN_TEST(test_missing_ack_reaches_bounded_timeout);
  RUN_TEST(test_later_fragment_start_failure_finishes_send);
  RUN_TEST(
      test_reassembly_completed_out_of_order_defers_ack_until_last_fragment);
  RUN_TEST(test_encrypted_fragmented_message_and_authenticated_ack);
  RUN_TEST(test_invalid_message_crc_is_rejected_and_counted);
  RUN_TEST(test_incomplete_reassembly_expires);
  RUN_TEST(test_corrupt_packet_during_ack_wait_does_not_fail_send);
  RUN_TEST(test_maximum_retry_count_reports_all_attempts);
  RUN_TEST(test_concurrent_send_start_serializes_one_link);
  RUN_TEST(test_acknowledgements_take_the_acknowledger_sequence);
  RUN_TEST(test_acknowledgement_of_another_session_or_message_is_ignored);
  RUN_TEST(test_exhausted_sequence_space_stops_acknowledgements);
  return UNITY_END();
}
