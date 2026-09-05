#include "bluetooth.h"
#include "classic/btstack_sbc_bluedroid.h"
#include "hal/bluetooth/jh_bluetooth_a2dp_sbc.h"
#include "hal/core/hal_array.h"
#include "hal/core/jh_endian.h"
#include "utils/unity.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

struct fixture_t {
  const char *name;
  uint16_t sample_rate_hz;
  btstack_sbc_channel_mode_t channel_mode;
  hal_bluetooth_a2dp_channel_mode_t expected_mode;
  uint8_t expected_channels;
  uint32_t encoded_hash;
  uint32_t decoded_hash;
};

struct decoded_capture_t {
  int16_t samples[128u * 2u];
  size_t sample_count;
  uint32_t sample_rate_hz;
  uint8_t channels;
  uint32_t callbacks;
};

uint32_t fnv1a32(const uint8_t *data, size_t length) {
  uint32_t hash = UINT32_C(2166136261);
  for (size_t index = 0u; index < length; ++index) {
    hash ^= data[index];
    hash *= UINT32_C(16777619);
  }
  return hash;
}

uint32_t pcm_hash(const int16_t *samples, size_t count) {
  uint32_t hash = UINT32_C(2166136261);
  for (size_t index = 0u; index < count; ++index) {
    uint8_t bytes[2u];
    jh_store_le16(bytes, (uint16_t)samples[index]);
    for (size_t byte = 0u; byte < COUNTOF(bytes); ++byte) {
      hash ^= bytes[byte];
      hash *= UINT32_C(16777619);
    }
  }
  return hash;
}

void decoded_handler(int16_t *data, int frames, int channels, int sample_rate,
                     void *context) {
  auto *capture = static_cast<decoded_capture_t *>(context);
  TEST_ASSERT_NOT_NULL(data);
  TEST_ASSERT_GREATER_THAN_INT(0, frames);
  TEST_ASSERT_TRUE(channels == 1 || channels == 2);
  const size_t count = (size_t)frames * (size_t)channels;
  TEST_ASSERT_LESS_OR_EQUAL_UINT(COUNTOF(capture->samples), count);
  memcpy(capture->samples, data, count * sizeof(data[0]));
  capture->sample_count = count;
  capture->sample_rate_hz = (uint32_t)sample_rate;
  capture->channels = (uint8_t)channels;
  ++capture->callbacks;
}

void fill_pcm(int16_t *pcm, size_t count) {
  for (size_t index = 0u; index < count; ++index) {
    const uint32_t shaped = ((uint32_t)index * UINT32_C(811) +
                             ((uint32_t)index & UINT32_C(1)) * UINT32_C(1234)) %
                            UINT32_C(30001);
    pcm[index] = (int16_t)((int32_t)shaped - 15000);
  }
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_six_deterministic_sbc_fixtures_encode_parse_and_decode(void) {
  const fixture_t fixtures[] = {
      {"44k1-mono", 44100u, SBC_CHANNEL_MODE_MONO,
       HAL_BLUETOOTH_A2DP_CHANNEL_MONO, 1u, UINT32_C(0x11cdd204),
       UINT32_C(0x59757525)},
      {"44k1-stereo", 44100u, SBC_CHANNEL_MODE_STEREO,
       HAL_BLUETOOTH_A2DP_CHANNEL_STEREO, 2u, UINT32_C(0xf3194f20),
       UINT32_C(0x1ec37174)},
      {"44k1-joint", 44100u, SBC_CHANNEL_MODE_JOINT_STEREO,
       HAL_BLUETOOTH_A2DP_CHANNEL_JOINT_STEREO, 2u, UINT32_C(0xa1a9224f),
       UINT32_C(0x8ca4e566)},
      {"48k-mono", 48000u, SBC_CHANNEL_MODE_MONO,
       HAL_BLUETOOTH_A2DP_CHANNEL_MONO, 1u, UINT32_C(0xfabd0c74),
       UINT32_C(0x59757525)},
      {"48k-stereo", 48000u, SBC_CHANNEL_MODE_STEREO,
       HAL_BLUETOOTH_A2DP_CHANNEL_STEREO, 2u, UINT32_C(0x9f06572b),
       UINT32_C(0x1ec37174)},
      {"48k-joint", 48000u, SBC_CHANNEL_MODE_JOINT_STEREO,
       HAL_BLUETOOTH_A2DP_CHANNEL_JOINT_STEREO, 2u, UINT32_C(0xbab2614a),
       UINT32_C(0x8ca4e566)},
  };
  int16_t pcm[128u * 2u]{};
  fill_pcm(pcm, COUNTOF(pcm));

  for (size_t index = 0u; index < COUNTOF(fixtures); ++index) {
    const fixture_t &fixture = fixtures[index];
    btstack_sbc_encoder_bluedroid_t encoder_context{};
    const btstack_sbc_encoder_t *encoder =
        btstack_sbc_encoder_bluedroid_init_instance(&encoder_context);
    TEST_ASSERT_NOT_NULL(encoder);
    TEST_ASSERT_EQUAL_UINT8(
        ERROR_CODE_SUCCESS,
        encoder->configure(&encoder_context, SBC_MODE_STANDARD, 16u, 8u,
                           SBC_ALLOCATION_METHOD_LOUDNESS,
                           fixture.sample_rate_hz, 32u, fixture.channel_mode));
    TEST_ASSERT_EQUAL_UINT16(128u, encoder->num_audio_frames(&encoder_context));
    uint8_t encoded[128u]{};
    TEST_ASSERT_EQUAL_UINT8(
        ERROR_CODE_SUCCESS,
        encoder->encode_signed_16(&encoder_context, pcm, encoded));
    const size_t encoded_length = encoder->sbc_buffer_length(&encoder_context);
    TEST_ASSERT_GREATER_THAN_UINT32(4u, encoded_length);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(COUNTOF(encoded), encoded_length);

    jh_bluetooth_a2dp_sbc_frame_t parsed{};
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_a2dp_sbc_frame_parse(
                                      encoded, encoded_length, &parsed));
    TEST_ASSERT_EQUAL_UINT32(fixture.sample_rate_hz,
                             parsed.format.sample_rate_hz);
    TEST_ASSERT_EQUAL_UINT8(fixture.expected_mode, parsed.format.channel_mode);
    TEST_ASSERT_EQUAL_UINT8(fixture.expected_channels, parsed.format.channels);
    TEST_ASSERT_EQUAL_UINT(encoded_length, parsed.frame_length);

    decoded_capture_t capture{};
    btstack_sbc_decoder_bluedroid_t decoder_context{};
    const btstack_sbc_decoder_t *decoder =
        btstack_sbc_decoder_bluedroid_init_instance(&decoder_context);
    TEST_ASSERT_NOT_NULL(decoder);
    decoder->configure(&decoder_context, SBC_MODE_STANDARD, decoded_handler,
                       &capture);
    decoder->decode_signed_16(&decoder_context, 0u, encoded,
                              (uint16_t)encoded_length);
    TEST_ASSERT_EQUAL_INT(1, decoder_context.good_frames_nr);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.callbacks);
    TEST_ASSERT_EQUAL_UINT32(fixture.sample_rate_hz, capture.sample_rate_hz);
    TEST_ASSERT_EQUAL_UINT8(fixture.expected_channels, capture.channels);
    TEST_ASSERT_EQUAL_UINT(128u * fixture.expected_channels,
                           capture.sample_count);

    const uint32_t encoded_hash = fnv1a32(encoded, encoded_length);
    const uint32_t decoded_hash =
        pcm_hash(capture.samples, capture.sample_count);
    printf("%s encoded=0x%08lx decoded=0x%08lx len=%lu\n", fixture.name,
           (unsigned long)encoded_hash, (unsigned long)decoded_hash,
           (unsigned long)encoded_length);
    TEST_ASSERT_EQUAL_HEX32(fixture.encoded_hash, encoded_hash);
    TEST_ASSERT_EQUAL_HEX32(fixture.decoded_hash, decoded_hash);

    encoded[3] ^= UINT8_C(0x01);
    const int good_before = decoder_context.good_frames_nr;
    decoder->decode_signed_16(&decoder_context, 0u, encoded,
                              (uint16_t)encoded_length);
    TEST_ASSERT_EQUAL_INT(good_before, decoder_context.good_frames_nr);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_six_deterministic_sbc_fixtures_encode_parse_and_decode);
  return UNITY_END();
}
