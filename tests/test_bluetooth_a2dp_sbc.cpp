#include "hal/bluetooth/jh_bluetooth_a2dp_sbc.h"
#include "hal/core/hal_array.h"
#include "utils/unity.h"

#include <string.h>

namespace {

void make_frame(uint8_t *frame, size_t length, uint8_t header,
                uint8_t bitpool) {
  memset(frame, 0, length);
  frame[0] = 0x9cu;
  frame[1] = header;
  frame[2] = bitpool;
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_supported_sbc_headers_report_exact_sizes(void) {
  uint8_t stereo[76u];
  make_frame(stereo, sizeof(stereo), 0xb9u, 32u);
  jh_bluetooth_a2dp_sbc_frame_t frame{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_a2dp_sbc_frame_parse(
                                    stereo, sizeof(stereo), &frame));
  TEST_ASSERT_EQUAL_UINT32(44100u, frame.format.sample_rate_hz);
  TEST_ASSERT_EQUAL_UINT8(HAL_BLUETOOTH_A2DP_CHANNEL_STEREO,
                          frame.format.channel_mode);
  TEST_ASSERT_EQUAL_UINT8(2u, frame.format.channels);
  TEST_ASSERT_EQUAL_UINT8(16u, frame.format.block_length);
  TEST_ASSERT_EQUAL_UINT8(8u, frame.format.subbands);
  TEST_ASSERT_EQUAL_UINT(sizeof(stereo), frame.frame_length);
  TEST_ASSERT_EQUAL_UINT(128u, frame.pcm_frames);

  uint8_t mono[72u];
  make_frame(mono, sizeof(mono), 0xf1u, 32u);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_a2dp_sbc_frame_parse(mono, sizeof(mono), &frame));
  TEST_ASSERT_EQUAL_UINT32(48000u, frame.format.sample_rate_hz);
  TEST_ASSERT_EQUAL_UINT8(HAL_BLUETOOTH_A2DP_CHANNEL_MONO,
                          frame.format.channel_mode);
  TEST_ASSERT_EQUAL_UINT8(1u, frame.format.channels);
  TEST_ASSERT_EQUAL_UINT(sizeof(mono), frame.frame_length);

  uint8_t joint[77u];
  make_frame(joint, sizeof(joint), 0xbdu, 32u);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_a2dp_sbc_frame_parse(joint, sizeof(joint), &frame));
  TEST_ASSERT_EQUAL_UINT8(HAL_BLUETOOTH_A2DP_CHANNEL_JOINT_STEREO,
                          frame.format.channel_mode);
  TEST_ASSERT_EQUAL_UINT(sizeof(joint), frame.frame_length);
}

void test_media_packet_parses_rtp_extension_padding_and_wrap_sequence(void) {
  uint8_t packet[12u + 4u + 4u + 1u + 76u + 2u]{};
  packet[0] = 0xb0u;
  packet[2] = 0xffu;
  packet[3] = 0xffu;
  packet[14] = 0u;
  packet[15] = 1u;
  packet[20] = 1u;
  make_frame(packet + 21u, 76u, 0xb9u, 32u);
  packet[sizeof(packet) - 1u] = 2u;
  hal_bluetooth_a2dp_sbc_format_t expected{
      44100u, 2u, HAL_BLUETOOTH_A2DP_CHANNEL_STEREO, 16u, 8u, 2u, 53u};
  jh_bluetooth_a2dp_media_packet_t parsed{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_a2dp_media_packet_parse(packet, sizeof(packet),
                                                   &expected, &parsed));
  TEST_ASSERT_EQUAL_UINT16(0xffffu, parsed.sequence_number);
  TEST_ASSERT_EQUAL_UINT8(1u, parsed.frame_count);
  TEST_ASSERT_EQUAL_UINT(76u, parsed.frames_length);
  TEST_ASSERT_EQUAL_PTR(packet + 21u, parsed.frames);
}

void test_malformed_and_unsupported_frames_are_rejected(void) {
  uint8_t frame[76u];
  make_frame(frame, sizeof(frame), 0xb9u, 32u);
  jh_bluetooth_a2dp_sbc_frame_t parsed{};
  frame[0] = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, jh_bluetooth_a2dp_sbc_frame_parse(
                                        frame, sizeof(frame), &parsed));
  make_frame(frame, sizeof(frame), 0x79u, 32u);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, jh_bluetooth_a2dp_sbc_frame_parse(
                                              frame, sizeof(frame), &parsed));
  make_frame(frame, sizeof(frame), 0xb5u, 32u);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, jh_bluetooth_a2dp_sbc_frame_parse(
                                              frame, sizeof(frame), &parsed));
  make_frame(frame, sizeof(frame), 0xb9u, 1u);
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, jh_bluetooth_a2dp_sbc_frame_parse(
                                        frame, sizeof(frame), &parsed));
}

void test_fragmented_or_reserved_media_payload_headers_are_rejected(void) {
  uint8_t packet[13u + 76u]{};
  packet[0] = 0x80u;
  packet[12] = 0x11u;
  make_frame(packet + 13u, 76u, 0xb9u, 32u);
  const hal_bluetooth_a2dp_sbc_format_t expected{
      44100u, 2u, HAL_BLUETOOTH_A2DP_CHANNEL_STEREO, 16u, 8u, 2u, 53u};
  jh_bluetooth_a2dp_media_packet_t parsed{};
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, jh_bluetooth_a2dp_media_packet_parse(packet, sizeof(packet),
                                                       &expected, &parsed));
  packet[12] = 0x81u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, jh_bluetooth_a2dp_media_packet_parse(packet, sizeof(packet),
                                                       &expected, &parsed));
}

void test_pcm_transform_downmixes_scales_and_adjusts_without_overrun(void) {
  const int16_t input[] = {1000, -500, -1001, 500};
  int16_t output[6]{};
  size_t frames = 0u;
  uint8_t channels = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_a2dp_pcm_transform(
                            input, 2u, 2u, HAL_BLUETOOTH_A2DP_OUTPUT_MONO, 127u,
                            1, output, COUNTOF(output), &frames, &channels));
  TEST_ASSERT_EQUAL_UINT(3u, frames);
  TEST_ASSERT_EQUAL_UINT8(1u, channels);
  TEST_ASSERT_EQUAL_INT16(250, output[0]);
  TEST_ASSERT_EQUAL_INT16(-250, output[1]);
  TEST_ASSERT_EQUAL_INT16(-250, output[2]);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        jh_bluetooth_a2dp_pcm_transform(
                            input, 2u, 2u, HAL_BLUETOOTH_A2DP_OUTPUT_NATIVE,
                            127u, 0, output, 3u, &frames, &channels));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_supported_sbc_headers_report_exact_sizes);
  RUN_TEST(test_media_packet_parses_rtp_extension_padding_and_wrap_sequence);
  RUN_TEST(test_malformed_and_unsupported_frames_are_rejected);
  RUN_TEST(test_fragmented_or_reserved_media_payload_headers_are_rejected);
  RUN_TEST(test_pcm_transform_downmixes_scales_and_adjusts_without_overrun);
  return UNITY_END();
}
