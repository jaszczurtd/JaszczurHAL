#include "hal/radio/jh_lora_link_frame.h"
#include "utils/unity.h"

#include <string.h>

static const uint8_t kKey[HAL_LORA_LINK_CRYPTO_KEY_BYTES] = {
    0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0Au,
    0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu, 0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u,
    0x16u, 0x17u, 0x18u, 0x19u, 0x1Au, 0x1Bu, 0x1Cu, 0x1Du, 0x1Eu, 0x1Fu,
};

static jh_lora_link_frame_header_t data_header(bool encrypted) {
  jh_lora_link_frame_header_t header = {};
  header.flags = JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST;
  if (encrypted) {
    header.flags |= JH_LORA_LINK_FRAME_FLAG_ENCRYPTED;
  }
  header.port = 7u;
  header.source = 0x1234u;
  header.destination = 0x5678u;
  header.session_id = UINT32_C(0x10203040);
  header.sequence = UINT32_C(0x50607080);
  header.fragment_index = 0u;
  header.fragment_count = 1u;
  header.message_length = 5u;
  header.integrity = encrypted ? 0u : UINT32_C(0x89ABCDEF);
  return header;
}

void setUp(void) {}
void tearDown(void) {}

void test_plaintext_frame_round_trip_and_capacity(void) {
  TEST_ASSERT_EQUAL_UINT(HAL_LORA_RADIO_MAX_PAYLOAD -
                             JH_LORA_LINK_FRAME_HEADER_SIZE,
                         jh_lora_link_frame_payload_capacity(false));
  TEST_ASSERT_EQUAL_UINT(HAL_LORA_RADIO_MAX_PAYLOAD -
                             JH_LORA_LINK_FRAME_HEADER_SIZE -
                             JH_LORA_LINK_FRAME_TAG_SIZE,
                         jh_lora_link_frame_payload_capacity(true));

  const uint8_t payload[] = {'h', 'e', 'l', 'l', 'o'};
  const jh_lora_link_frame_header_t header = data_header(false);
  uint8_t frame[HAL_LORA_RADIO_MAX_PAYLOAD] = {};
  size_t frame_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lora_link_frame_encode(&header, payload, sizeof(payload), NULL,
                                        frame, sizeof(frame), &frame_length));
  TEST_ASSERT_EQUAL_UINT(JH_LORA_LINK_FRAME_HEADER_SIZE + sizeof(payload),
                         frame_length);

  jh_lora_link_frame_header_t decoded = {};
  uint8_t output[16] = {};
  size_t output_length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lora_link_frame_decode(
                                    frame, frame_length, NULL, &decoded, output,
                                    sizeof(output), &output_length));
  TEST_ASSERT_EQUAL_MEMORY(&header, &decoded, sizeof(header));
  TEST_ASSERT_EQUAL_UINT(sizeof(payload), output_length);
  TEST_ASSERT_EQUAL_MEMORY(payload, output, sizeof(payload));
}

void test_encrypted_frame_authenticates_header_and_payload(void) {
  const uint8_t payload[] = {'s', 'e', 'c', 'r', 't'};
  const jh_lora_link_frame_header_t header = data_header(true);
  uint8_t frame[HAL_LORA_RADIO_MAX_PAYLOAD] = {};
  size_t frame_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lora_link_frame_encode(&header, payload, sizeof(payload), kKey,
                                        frame, sizeof(frame), &frame_length));

  jh_lora_link_frame_header_t decoded = {};
  uint8_t output[16] = {};
  size_t output_length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lora_link_frame_decode(
                                    frame, frame_length, kKey, &decoded, output,
                                    sizeof(output), &output_length));
  TEST_ASSERT_EQUAL_MEMORY(payload, output, sizeof(payload));

  frame[JH_LORA_LINK_FRAME_HEADER_SIZE] ^= 0x01u;
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, jh_lora_link_frame_decode(
                                       frame, frame_length, kKey, &decoded,
                                       output, sizeof(output), &output_length));
  frame[JH_LORA_LINK_FRAME_HEADER_SIZE] ^= 0x01u;
  frame[7] ^= 0x01u;
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, jh_lora_link_frame_decode(
                                       frame, frame_length, kKey, &decoded,
                                       output, sizeof(output), &output_length));
}

void test_acknowledgement_and_fragment_shapes_are_strict(void) {
  jh_lora_link_frame_header_t acknowledgement = {};
  acknowledgement.flags = JH_LORA_LINK_FRAME_FLAG_ACK;
  acknowledgement.source = 2u;
  acknowledgement.destination = 1u;
  acknowledgement.session_id = 22u;
  acknowledgement.sequence = 9u;
  acknowledgement.integrity = 11u;
  uint8_t frame[HAL_LORA_RADIO_MAX_PAYLOAD] = {};
  size_t frame_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lora_link_frame_encode(&acknowledgement, NULL, 0u, NULL, frame,
                                        sizeof(frame), &frame_length));
  TEST_ASSERT_EQUAL_UINT(JH_LORA_LINK_FRAME_HEADER_SIZE, frame_length);

  acknowledgement.port = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_lora_link_frame_encode(
                                        &acknowledgement, NULL, 0u, NULL, frame,
                                        sizeof(frame), &frame_length));
  acknowledgement.port = 0u;
  acknowledgement.destination = HAL_LORA_LINK_ADDRESS_BROADCAST;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_lora_link_frame_encode(
                                        &acknowledgement, NULL, 0u, NULL, frame,
                                        sizeof(frame), &frame_length));

  jh_lora_link_frame_header_t fragmented = data_header(false);
  fragmented.fragment_count = 2u;
  fragmented.message_length =
      (uint16_t)(jh_lora_link_frame_payload_capacity(false) + 1u);
  const uint8_t byte = 1u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, jh_lora_link_frame_encode(&fragmented, &byte, 1u, NULL, frame,
                                            sizeof(frame), &frame_length));
}

void test_decoder_rejects_truncation_magic_flags_and_small_output(void) {
  const uint8_t payload[] = {'h', 'e', 'l', 'l', 'o'};
  const jh_lora_link_frame_header_t header = data_header(false);
  uint8_t frame[HAL_LORA_RADIO_MAX_PAYLOAD] = {};
  size_t frame_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lora_link_frame_encode(&header, payload, sizeof(payload), NULL,
                                        frame, sizeof(frame), &frame_length));
  jh_lora_link_frame_header_t decoded = {};
  uint8_t output[4] = {};
  size_t output_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, jh_lora_link_frame_decode(frame, 3u, NULL, &decoded, output,
                                            sizeof(output), &output_length));
  frame[0] ^= 1u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO,
      jh_lora_link_frame_decode(frame, frame_length, NULL, &decoded, output,
                                sizeof(output), &output_length));
  frame[0] ^= 1u;
  frame[3] |= 0x80u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO,
      jh_lora_link_frame_decode(frame, frame_length, NULL, &decoded, output,
                                sizeof(output), &output_length));
  frame[3] &= 0x7Fu;
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      jh_lora_link_frame_decode(frame, frame_length, NULL, &decoded, output,
                                sizeof(output), &output_length));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_plaintext_frame_round_trip_and_capacity);
  RUN_TEST(test_encrypted_frame_authenticates_header_and_payload);
  RUN_TEST(test_acknowledgement_and_fragment_shapes_are_strict);
  RUN_TEST(test_decoder_rejects_truncation_magic_flags_and_small_output);
  return UNITY_END();
}
