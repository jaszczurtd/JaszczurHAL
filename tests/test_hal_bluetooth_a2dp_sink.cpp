#include "hal/bluetooth/hal_bluetooth_a2dp_sink.h"
#include "hal/bluetooth/hal_bluetooth_avrcp_target.h"
#include "hal/core/hal_array.h"
#include "hal/core/jh_endian.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <string.h>

namespace {

hal_bluetooth_classic_t s_classic = nullptr;
hal_bluetooth_a2dp_sink_t s_sink = nullptr;
hal_bluetooth_avrcp_target_t s_avrcp = nullptr;

const hal_bluetooth_classic_address_t kPhone = {
    {0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u}};

hal_bluetooth_a2dp_sbc_format_t stereo_format(void) {
  return {44100u, 2u, HAL_BLUETOOTH_A2DP_CHANNEL_STEREO, 16u, 8u, 2u, 53u};
}

void make_packet(uint8_t *packet, size_t length, uint16_t sequence) {
  memset(packet, 0, length);
  packet[0] = 0x80u;
  jh_store_be16(packet + 2u, sequence);
  packet[12] = 1u;
  packet[13] = 0x9cu;
  packet[14] = 0xb9u;
  packet[15] = 32u;
}

void make_multi_frame_packet(uint8_t *packet, size_t length, uint16_t sequence,
                             uint8_t frame_count) {
  const size_t frame_length = 76u;
  TEST_ASSERT_EQUAL_UINT(13u + frame_count * frame_length, length);
  make_packet(packet, length, sequence);
  packet[12] = frame_count;
  for (uint8_t index = 1u; index < frame_count; ++index) {
    memcpy(packet + 13u + index * frame_length, packet + 13u, frame_length);
  }
}

void open_stream(hal_bluetooth_a2dp_output_mode_t mode) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_open(&s_classic));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_classic_inject_ready());
  const hal_bluetooth_a2dp_sink_config_t config{mode, 127u};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_bluetooth_a2dp_sink_open(s_classic, &config, &s_sink));
  const auto format = stereo_format();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_a2dp_inject_connected(&kPhone));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_a2dp_inject_format(&format));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_a2dp_inject_started());
}

void inject_and_poll(uint16_t sequence) {
  uint8_t packet[89u];
  make_packet(packet, sizeof(packet), sequence);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_bluetooth_a2dp_inject_media(packet, sizeof(packet)));
  memset(packet, 0, sizeof(packet));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_a2dp_sink_poll(s_sink));
}

} // namespace

void setUp(void) {
  s_avrcp = nullptr;
  s_sink = nullptr;
  s_classic = nullptr;
  hal_mock_bluetooth_classic_reset();
  hal_mock_bluetooth_avrcp_runtime_full_reset();
  hal_mock_bluetooth_a2dp_runtime_full_reset();
  hal_mock_bluetooth_classic_runtime_full_reset();
}

void tearDown(void) {
  if (s_avrcp != nullptr) {
    (void)hal_bluetooth_avrcp_target_close(s_avrcp);
  }
  if (s_sink != nullptr) {
    (void)hal_bluetooth_a2dp_sink_close(s_sink);
  }
  if (s_classic != nullptr) {
    (void)hal_bluetooth_classic_close(s_classic);
  }
  s_avrcp = nullptr;
  s_sink = nullptr;
  s_classic = nullptr;
  hal_mock_bluetooth_classic_reset();
  hal_mock_bluetooth_avrcp_runtime_full_reset();
  hal_mock_bluetooth_a2dp_runtime_full_reset();
  hal_mock_bluetooth_classic_runtime_full_reset();
}

void test_media_is_copied_decoded_and_prebuffered_in_poll_context(void) {
  open_stream(HAL_BLUETOOTH_A2DP_OUTPUT_MONO);
  for (uint16_t sequence = 1u; sequence <= 4u; ++sequence) {
    inject_and_poll(sequence);
  }
  hal_bluetooth_a2dp_sink_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_UINT32(4u, info.media_packets);
  TEST_ASSERT_EQUAL_UINT32(4u, info.decoded_frames);
  TEST_ASSERT_TRUE(info.first_audio_valid);
  TEST_ASSERT_TRUE(info.prebuffering);

  int16_t samples[HAL_BLUETOOTH_A2DP_PCM_MAX_FRAMES * 2u]{};
  hal_bluetooth_a2dp_pcm_block_t block{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_a2dp_sink_pcm_next(
                                    s_sink, samples, COUNTOF(samples), &block));
  TEST_ASSERT_EQUAL_UINT32(44100u, block.sample_rate_hz);
  TEST_ASSERT_EQUAL_UINT8(1u, block.channels);
  TEST_ASSERT_EQUAL_UINT(128u, block.frames);
  TEST_ASSERT_EQUAL_INT16(250, samples[0]);
}

void test_short_pcm_destination_does_not_consume_the_block(void) {
  open_stream(HAL_BLUETOOTH_A2DP_OUTPUT_NATIVE);
  for (uint16_t sequence = 1u; sequence <= 4u; ++sequence) {
    inject_and_poll(sequence);
  }
  int16_t samples[HAL_BLUETOOTH_A2DP_PCM_MAX_FRAMES * 2u]{};
  hal_bluetooth_a2dp_pcm_block_t block{};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_bluetooth_a2dp_sink_pcm_next(
                                           s_sink, samples, 1u, &block));
  hal_bluetooth_a2dp_sink_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_UINT(4u, info.pending_pcm_blocks);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_a2dp_sink_pcm_next(
                                    s_sink, samples, COUNTOF(samples), &block));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_UINT(3u, info.pending_pcm_blocks);
}

void test_one_valid_media_packet_can_hold_thirteen_sbc_frames(void) {
  open_stream(HAL_BLUETOOTH_A2DP_OUTPUT_NATIVE);
  uint8_t packet[13u + 13u * 76u];
  make_multi_frame_packet(packet, sizeof(packet), 1u, 13u);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_bluetooth_a2dp_inject_media(packet, sizeof(packet)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_a2dp_sink_poll(s_sink));

  hal_bluetooth_a2dp_sink_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_UINT32(13u, info.decoded_frames);
  TEST_ASSERT_EQUAL_UINT(13u, info.pending_pcm_blocks);
  TEST_ASSERT_EQUAL_UINT32(0u, info.pcm_overflows);

  uint8_t blocked_packet[13u + 4u * 76u];
  make_multi_frame_packet(blocked_packet, sizeof(blocked_packet), 2u, 4u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_a2dp_inject_media(
                                    blocked_packet, sizeof(blocked_packet)));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_bluetooth_a2dp_sink_poll(s_sink));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_UINT(1u, info.pending_packets);
  TEST_ASSERT_EQUAL_UINT(13u, info.pending_pcm_blocks);
  TEST_ASSERT_EQUAL_UINT32(13u, info.decoded_frames);
  TEST_ASSERT_EQUAL_UINT32(0u, info.pcm_overflows);

  int16_t samples[HAL_BLUETOOTH_A2DP_PCM_MAX_FRAMES * 2u] = {0};
  hal_bluetooth_a2dp_pcm_block_t block = {0};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_a2dp_sink_pcm_next(
                                    s_sink, samples, COUNTOF(samples), &block));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_a2dp_sink_poll(s_sink));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_UINT(0u, info.pending_packets);
  TEST_ASSERT_EQUAL_UINT(16u, info.pending_pcm_blocks);
  TEST_ASSERT_EQUAL_UINT32(17u, info.decoded_frames);
  TEST_ASSERT_EQUAL_UINT32(0u, info.pcm_overflows);
}

void test_packet_loss_overflow_suspend_and_restart_are_observable(void) {
  open_stream(HAL_BLUETOOTH_A2DP_OUTPUT_NATIVE);
  uint8_t packet[89u];
  for (size_t index = 0u; index < HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH + 1u;
       ++index) {
    make_packet(packet, sizeof(packet), (uint16_t)(index * 2u));
    TEST_ASSERT_EQUAL_INT(
        HAL_OK, hal_mock_bluetooth_a2dp_inject_media(packet, sizeof(packet)));
  }
  hal_bluetooth_a2dp_sink_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_UINT32(1u, info.dropped_packets);
  for (size_t index = 0u; index < HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH;
       ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_a2dp_sink_poll(s_sink));
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_UINT32(HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH - 1u,
                           info.missing_packets);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_a2dp_inject_suspended());
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_INT(HAL_BLUETOOTH_A2DP_STATE_SUSPENDED, info.state);
  TEST_ASSERT_EQUAL_UINT(0u, info.pending_pcm_blocks);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_a2dp_inject_started());
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_INT(HAL_BLUETOOTH_A2DP_STATE_STREAMING, info.state);
  TEST_ASSERT_TRUE(info.prebuffering);
}

void test_late_packet_does_not_create_false_forward_loss(void) {
  open_stream(HAL_BLUETOOTH_A2DP_OUTPUT_NATIVE);
  inject_and_poll(10u);
  inject_and_poll(12u);
  inject_and_poll(11u);
  inject_and_poll(13u);
  hal_bluetooth_a2dp_sink_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_UINT32(1u, info.missing_packets);
  TEST_ASSERT_EQUAL_UINT32(4u, info.decoded_frames);
}

void test_eight_frame_packets_apply_clock_recovery_to_every_low_block(void) {
  open_stream(HAL_BLUETOOTH_A2DP_OUTPUT_MONO);
  uint32_t adjusted_blocks = 0u;
  uint8_t packet[13u + 8u * 76u];
  int16_t samples[HAL_BLUETOOTH_A2DP_PCM_MAX_FRAMES * 2u] = {0};
  hal_bluetooth_a2dp_pcm_block_t block = {0};

  for (uint16_t sequence = 1u; sequence <= 4u; ++sequence) {
    make_multi_frame_packet(packet, sizeof(packet), sequence, 8u);
    TEST_ASSERT_EQUAL_INT(
        HAL_OK, hal_mock_bluetooth_a2dp_inject_media(packet, sizeof(packet)));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_a2dp_sink_poll(s_sink));
    for (uint8_t frame = 0u; frame < 8u; ++frame) {
      TEST_ASSERT_EQUAL_INT(
          HAL_OK, hal_bluetooth_a2dp_sink_pcm_next(s_sink, samples,
                                                   COUNTOF(samples), &block));
      if (block.clock_adjusted) {
        ++adjusted_blocks;
      }
    }
  }

  hal_bluetooth_a2dp_sink_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_a2dp_sink_get_info(s_sink, &info));
  TEST_ASSERT_EQUAL_INT32(2000, info.clock_correction_ppm);
  TEST_ASSERT_EQUAL_UINT32(8u, adjusted_blocks);
}

void test_avrcp_coalesces_volume_and_enforces_close_order(void) {
  open_stream(HAL_BLUETOOTH_A2DP_OUTPUT_NATIVE);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_bluetooth_avrcp_target_open(s_classic, 64u, &s_avrcp));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_avrcp_inject_connected(&kPhone));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_avrcp_inject_volume(30u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_avrcp_inject_volume(31u));
  hal_bluetooth_avrcp_target_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_avrcp_target_get_info(s_avrcp, &info));
  TEST_ASSERT_EQUAL_UINT32(2u, info.volume_changes);
  TEST_ASSERT_EQUAL_UINT32(1u, info.overwritten_volume_changes);
  uint8_t volume = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_bluetooth_avrcp_target_volume_next(s_avrcp, &volume));
  TEST_ASSERT_EQUAL_UINT8(31u, volume);
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_bluetooth_avrcp_target_volume_next(s_avrcp, &volume));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_avrcp_target_set_volume(s_avrcp, 63u));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_bluetooth_a2dp_sink_close(s_sink));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_avrcp_target_close(s_avrcp));
  s_avrcp = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_a2dp_sink_close(s_sink));
  s_sink = nullptr;
}

void test_shared_bond_is_saved_only_after_first_valid_audio(void) {
  open_stream(HAL_BLUETOOTH_A2DP_OUTPUT_NATIVE);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_classic_inject_pairing_request(
                            &kPhone, HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_pairing_authorize(s_classic));
  uint8_t link_key[16u];
  memset(link_key, 0x5au, sizeof(link_key));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_classic_inject_link_key(
                                    &kPhone, link_key, 4u));

  size_t peers = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_peer_count(s_classic, &peers));
  TEST_ASSERT_EQUAL_UINT(0u, peers);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_poll(s_classic));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_peer_count(s_classic, &peers));
  TEST_ASSERT_EQUAL_UINT(0u, peers);

  inject_and_poll(1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_poll(s_classic));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_peer_count(s_classic, &peers));
  TEST_ASSERT_EQUAL_UINT(1u, peers);
  hal_bluetooth_classic_peer_t peer{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_peer_get(s_classic, 0u, &peer));
  TEST_ASSERT_EQUAL_UINT16(HAL_BLUETOOTH_A2DP_SINK_PROFILE_ID, peer.profile_id);

  inject_and_poll(2u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_poll(s_classic));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_peer_count(s_classic, &peers));
  TEST_ASSERT_EQUAL_UINT(1u, peers);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_media_is_copied_decoded_and_prebuffered_in_poll_context);
  RUN_TEST(test_short_pcm_destination_does_not_consume_the_block);
  RUN_TEST(test_one_valid_media_packet_can_hold_thirteen_sbc_frames);
  RUN_TEST(test_packet_loss_overflow_suspend_and_restart_are_observable);
  RUN_TEST(test_late_packet_does_not_create_false_forward_loss);
  RUN_TEST(test_eight_frame_packets_apply_clock_recovery_to_every_low_block);
  RUN_TEST(test_avrcp_coalesces_volume_and_enforces_close_order);
  RUN_TEST(test_shared_bond_is_saved_only_after_first_valid_audio);
  return UNITY_END();
}
