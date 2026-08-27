#include "hal/commands/hal_command_wire.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_request_matches_the_version_one_golden_vector(void) {
  hal_command_message_t message{};
  message.type = HAL_COMMAND_MESSAGE_REQUEST;
  message.encoding = HAL_COMMAND_ENCODING_BINARY;
  message.request_id = 0x01020304u;
  message.status = HAL_NONE;
  memcpy(message.name, "echo", 5u);
  const uint8_t payload[] = {0x00u, 0x3au, 0x3bu};
  memcpy(message.payload, payload, sizeof(payload));
  message.payload_length = sizeof(payload);

  uint8_t encoded[32]{};
  size_t encoded_length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_message_encode(&message, encoded,
                                                           sizeof(encoded),
                                                           &encoded_length));
  const uint8_t expected[] = {
      'J', 'C', 1u, 1u, 0u,  4u,  0u,  0u,  1u, 2u,    3u,    4u,
      0u,  0u,  0u, 3u, 'e', 'c', 'h', 'o', 0u, 0x3au, 0x3bu,
  };
  TEST_ASSERT_EQUAL_UINT(sizeof(expected), encoded_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded, sizeof(expected));

  hal_command_message_t decoded{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_message_decode(encoded, encoded_length, &decoded));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_REQUEST, decoded.type);
  TEST_ASSERT_EQUAL_UINT32(message.request_id, decoded.request_id);
  TEST_ASSERT_EQUAL_STRING("echo", decoded.name);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, decoded.payload, sizeof(payload));
}

void test_response_encodes_signed_status_in_big_endian_order(void) {
  hal_command_message_t message{};
  message.type = HAL_COMMAND_MESSAGE_RESPONSE;
  message.encoding = HAL_COMMAND_ENCODING_TEXT;
  message.request_id = 9u;
  message.status = HAL_EPERM;
  memcpy(message.payload, "no", 2u);
  message.payload_length = 2u;

  uint8_t encoded[HAL_COMMAND_WIRE_HEADER_SIZE + 2u]{};
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_command_message_encode(&message, encoded, sizeof(encoded), &length));
  TEST_ASSERT_EQUAL_UINT8(0xffu, encoded[12]);
  TEST_ASSERT_EQUAL_UINT8(0xf4u, encoded[13]);

  hal_command_message_t decoded{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_message_decode(encoded, length, &decoded));
  TEST_ASSERT_EQUAL_INT(HAL_EPERM, decoded.status);
  TEST_ASSERT_EQUAL_UINT32(9u, decoded.request_id);
  TEST_ASSERT_EQUAL_UINT(2u, decoded.payload_length);
}

void test_event_supports_binary_payload_with_zero_bytes(void) {
  hal_command_message_t message{};
  message.type = HAL_COMMAND_MESSAGE_EVENT;
  message.encoding = HAL_COMMAND_ENCODING_BINARY;
  message.status = HAL_NONE;
  memcpy(message.name, "sample", 7u);
  const uint8_t payload[] = {0u, 1u, 0u, 2u};
  memcpy(message.payload, payload, sizeof(payload));
  message.payload_length = sizeof(payload);

  uint8_t encoded[64]{};
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_command_message_encode(&message, encoded, sizeof(encoded), &length));
  hal_command_message_t decoded{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_message_decode(encoded, length, &decoded));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, decoded.type);
  TEST_ASSERT_EQUAL_STRING("sample", decoded.name);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, decoded.payload, sizeof(payload));
}

void test_encoder_reports_required_size_on_overflow(void) {
  hal_command_message_t message{};
  message.type = HAL_COMMAND_MESSAGE_REQUEST;
  message.encoding = HAL_COMMAND_ENCODING_TEXT;
  message.request_id = 1u;
  message.status = HAL_NONE;
  memcpy(message.name, "x", 2u);
  message.payload[0] = 'a';
  message.payload_length = 1u;

  uint8_t encoded[4]{};
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      hal_command_message_encode(&message, encoded, sizeof(encoded), &length));
  TEST_ASSERT_EQUAL_UINT(HAL_COMMAND_WIRE_HEADER_SIZE + 2u, length);
}

void test_decoder_rejects_malformed_or_non_exact_frames(void) {
  const uint8_t valid[] = {'J', 'C', 1u, 2u, 1u, 0u, 0u, 0u,
                           0u,  0u,  0u, 1u, 0u, 1u, 0u, 0u};
  hal_command_message_t decoded{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_message_decode(valid, sizeof(valid), &decoded));

  uint8_t corrupt[sizeof(valid) + 1u]{};
  memcpy(corrupt, valid, sizeof(valid));
  corrupt[sizeof(valid)] = 0xa5u;
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, hal_command_message_decode(
                                        corrupt, sizeof(corrupt), &decoded));
  corrupt[2] = 2u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, hal_command_message_decode(corrupt, sizeof(valid), &decoded));
  memcpy(corrupt, valid, sizeof(valid));
  corrupt[6] = 1u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, hal_command_message_decode(corrupt, sizeof(valid), &decoded));
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, hal_command_message_decode(
                                        valid, sizeof(valid) - 1u, &decoded));
}

void test_frame_size_supports_incremental_stream_buffers(void) {
  const uint8_t first[] = {
      'J', 'C', 1u, 1u, 1u,  4u,  0u,  0u,  0u,  0u,  0u,  7u,
      0u,  0u,  0u, 3u, 'e', 'c', 'h', 'o', 'a', 'b', 'c',
  };
  uint8_t stream[sizeof(first) + HAL_COMMAND_WIRE_HEADER_SIZE]{};
  memcpy(stream, first, sizeof(first));

  size_t frame_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_command_message_frame_size(stream, 8u, &frame_length));
  TEST_ASSERT_EQUAL_UINT(HAL_COMMAND_WIRE_HEADER_SIZE, frame_length);
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_command_message_frame_size(
                      stream, HAL_COMMAND_WIRE_HEADER_SIZE, &frame_length));
  TEST_ASSERT_EQUAL_UINT(sizeof(first), frame_length);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_message_frame_size(
                                    stream, sizeof(first), &frame_length));
  TEST_ASSERT_EQUAL_UINT(sizeof(first), frame_length);

  stream[sizeof(first)] = 'J';
  stream[sizeof(first) + 1u] = 'C';
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_message_frame_size(
                                    stream, sizeof(stream), &frame_length));
  TEST_ASSERT_EQUAL_UINT(sizeof(first), frame_length);

  hal_command_message_t decoded{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_message_decode(stream, frame_length, &decoded));
  TEST_ASSERT_EQUAL_STRING("echo", decoded.name);
  TEST_ASSERT_EQUAL_UINT(3u, decoded.payload_length);
}

void test_message_semantics_are_validated(void) {
  hal_command_message_t message{};
  message.type = HAL_COMMAND_MESSAGE_REQUEST;
  message.encoding = HAL_COMMAND_ENCODING_BINARY;
  message.status = HAL_NONE;
  memcpy(message.name, "x", 2u);
  uint8_t output[32]{};
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_command_message_encode(&message, output, sizeof(output), &length));
  message.request_id = 1u;
  message.status = HAL_OK;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_command_message_encode(&message, output, sizeof(output), &length));
  message.type = HAL_COMMAND_MESSAGE_RESPONSE;
  message.status = HAL_OK;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_command_message_encode(&message, output, sizeof(output), &length));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_request_matches_the_version_one_golden_vector);
  RUN_TEST(test_response_encodes_signed_status_in_big_endian_order);
  RUN_TEST(test_event_supports_binary_payload_with_zero_bytes);
  RUN_TEST(test_encoder_reports_required_size_on_overflow);
  RUN_TEST(test_decoder_rejects_malformed_or_non_exact_frames);
  RUN_TEST(test_frame_size_supports_incremental_stream_buffers);
  RUN_TEST(test_message_semantics_are_validated);
  return UNITY_END();
}
