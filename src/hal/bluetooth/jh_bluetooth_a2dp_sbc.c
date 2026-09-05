#include "hal/bluetooth/jh_bluetooth_a2dp_sbc.h"

#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK

#include "hal/core/jh_endian.h"

#include <limits.h>
#include <string.h>

static int16_t saturate_i16(int32_t value) {
  if (value > INT16_MAX) {
    return INT16_MAX;
  }
  if (value < INT16_MIN) {
    return INT16_MIN;
  }
  return (int16_t)value;
}

static int16_t scale_sample(int32_t sample, uint8_t volume) {
  const int32_t scaled = sample * volume;
  return saturate_i16((scaled + (scaled >= 0 ? 63 : -63)) / 127);
}

static bool format_equal(const hal_bluetooth_a2dp_sbc_format_t *first,
                         const hal_bluetooth_a2dp_sbc_format_t *second) {
  return first->sample_rate_hz == second->sample_rate_hz &&
         first->channels == second->channels &&
         first->channel_mode == second->channel_mode &&
         first->block_length == second->block_length &&
         first->subbands == second->subbands &&
         first->min_bitpool <= second->max_bitpool &&
         second->max_bitpool <= first->max_bitpool;
}

hal_status_t
jh_bluetooth_a2dp_sbc_frame_parse(const uint8_t *data, size_t length,
                                  jh_bluetooth_a2dp_sbc_frame_t *out_frame) {
  if (data == NULL || out_frame == NULL) {
    return HAL_EINVAL;
  }
  memset(out_frame, 0, sizeof(*out_frame));
  if (length < 4u || data[0] != UINT8_C(0x9c)) {
    return HAL_EPROTO;
  }

  const uint8_t header = data[1];
  const uint8_t frequency_index = (uint8_t)(header >> 6u);
  const uint8_t blocks_index = (uint8_t)((header >> 4u) & 0x03u);
  const uint8_t channel_index = (uint8_t)((header >> 2u) & 0x03u);
  const uint8_t subbands = (header & 0x01u) != 0u ? 8u : 4u;
  const uint8_t channels = channel_index == 0u ? 1u : 2u;
  const uint8_t blocks = (uint8_t)(4u * (blocks_index + 1u));
  const uint8_t bitpool = data[2];
  uint32_t sample_rate_hz = 0u;
  hal_bluetooth_a2dp_channel_mode_t channel_mode;

  if (frequency_index == 2u) {
    sample_rate_hz = 44100u;
  } else if (frequency_index == 3u) {
    sample_rate_hz = 48000u;
  } else {
    return HAL_EUNSUPPORTED;
  }
  switch (channel_index) {
  case 0u:
    channel_mode = HAL_BLUETOOTH_A2DP_CHANNEL_MONO;
    break;
  case 2u:
    channel_mode = HAL_BLUETOOTH_A2DP_CHANNEL_STEREO;
    break;
  case 3u:
    channel_mode = HAL_BLUETOOTH_A2DP_CHANNEL_JOINT_STEREO;
    break;
  default:
    return HAL_EUNSUPPORTED;
  }
  if (bitpool < 2u || bitpool > 53u) {
    return HAL_EPROTO;
  }

  const size_t scale_factor_bytes =
      ((size_t)4u * subbands * channels + 7u) / 8u;
  size_t audio_bits;
  if (channel_index == 0u) {
    audio_bits = (size_t)blocks * bitpool;
  } else if (channel_index == 2u) {
    audio_bits = (size_t)blocks * bitpool;
  } else {
    audio_bits = (size_t)subbands + (size_t)blocks * bitpool;
  }
  const size_t frame_length = 4u + scale_factor_bytes + (audio_bits + 7u) / 8u;
  if (frame_length > length) {
    return HAL_EPROTO;
  }

  out_frame->format.sample_rate_hz = sample_rate_hz;
  out_frame->format.channels = channels;
  out_frame->format.channel_mode = channel_mode;
  out_frame->format.block_length = blocks;
  out_frame->format.subbands = subbands;
  out_frame->format.min_bitpool = bitpool;
  out_frame->format.max_bitpool = bitpool;
  out_frame->frame_length = frame_length;
  out_frame->pcm_frames = (size_t)blocks * subbands;
  out_frame->bitpool = bitpool;
  return HAL_OK;
}

hal_status_t jh_bluetooth_a2dp_media_packet_parse(
    const uint8_t *data, size_t length,
    const hal_bluetooth_a2dp_sbc_format_t *expected_format,
    jh_bluetooth_a2dp_media_packet_t *out_packet) {
  if (data == NULL || expected_format == NULL || out_packet == NULL) {
    return HAL_EINVAL;
  }
  memset(out_packet, 0, sizeof(*out_packet));
  if (length < 13u || (data[0] >> 6u) != 2u) {
    return HAL_EPROTO;
  }

  const size_t csrc_count = data[0] & 0x0fu;
  size_t offset = 12u + 4u * csrc_count;
  if (offset > length) {
    return HAL_EPROTO;
  }
  if ((data[0] & 0x10u) != 0u) {
    if (length - offset < 4u) {
      return HAL_EPROTO;
    }
    const size_t extension_words = jh_load_be16(data + offset + 2u);
    if (extension_words > (SIZE_MAX - offset - 4u) / 4u) {
      return HAL_EOVERFLOW;
    }
    offset += 4u + extension_words * 4u;
    if (offset > length) {
      return HAL_EPROTO;
    }
  }
  size_t payload_end = length;
  if ((data[0] & 0x20u) != 0u) {
    const uint8_t padding = data[length - 1u];
    if (padding == 0u || padding > payload_end - offset) {
      return HAL_EPROTO;
    }
    payload_end -= padding;
  }
  if (payload_end <= offset) {
    return HAL_EPROTO;
  }

  const uint8_t sbc_header = data[offset++];
  const uint8_t frame_count = sbc_header & 0x0fu;
  if ((sbc_header & 0xf0u) != 0u || frame_count == 0u ||
      offset >= payload_end) {
    return HAL_EPROTO;
  }

  size_t cursor = offset;
  for (uint8_t index = 0u; index < frame_count; ++index) {
    jh_bluetooth_a2dp_sbc_frame_t frame;
    const hal_status_t status = jh_bluetooth_a2dp_sbc_frame_parse(
        data + cursor, payload_end - cursor, &frame);
    if (status != HAL_OK || !format_equal(expected_format, &frame.format)) {
      return status == HAL_OK ? HAL_EPROTO : status;
    }
    cursor += frame.frame_length;
  }
  if (cursor != payload_end) {
    return HAL_EPROTO;
  }

  out_packet->frames = data + offset;
  out_packet->frames_length = payload_end - offset;
  out_packet->sequence_number = jh_load_be16(data + 2u);
  out_packet->frame_count = frame_count;
  return HAL_OK;
}

hal_status_t jh_bluetooth_a2dp_pcm_transform(
    const int16_t *input, size_t frames, uint8_t input_channels,
    hal_bluetooth_a2dp_output_mode_t output_mode, uint8_t volume,
    int8_t frame_adjustment, int16_t *output, size_t output_capacity,
    size_t *out_frames, uint8_t *out_channels) {
  if (input == NULL || output == NULL || out_frames == NULL ||
      out_channels == NULL || frames == 0u || frames > 128u ||
      (input_channels != 1u && input_channels != 2u) || volume > 127u ||
      (output_mode != HAL_BLUETOOTH_A2DP_OUTPUT_NATIVE &&
       output_mode != HAL_BLUETOOTH_A2DP_OUTPUT_MONO) ||
      (frame_adjustment < -1 || frame_adjustment > 1)) {
    return HAL_EINVAL;
  }
  const uint8_t channels =
      output_mode == HAL_BLUETOOTH_A2DP_OUTPUT_MONO ? 1u : input_channels;
  size_t adjusted_frames = frames;
  if (frame_adjustment > 0) {
    ++adjusted_frames;
  } else if (frame_adjustment < 0) {
    --adjusted_frames;
  }
  if (adjusted_frames * channels > output_capacity) {
    return HAL_EOVERFLOW;
  }

  for (size_t frame = 0u; frame < adjusted_frames; ++frame) {
    size_t source_frame = frame;
    if (frame_adjustment < 0 && frames > 1u && frame >= frames / 2u) {
      source_frame = frame + 1u;
    } else if (frame_adjustment > 0 && frame >= frames) {
      source_frame = frames - 1u;
    }
    if (channels == 1u) {
      int32_t sample = input[source_frame * input_channels];
      if (input_channels == 2u) {
        sample = (sample + input[source_frame * 2u + 1u]) / 2;
      }
      output[frame] = scale_sample(sample, volume);
    } else {
      for (uint8_t channel = 0u; channel < channels; ++channel) {
        const int32_t sample = input[source_frame * channels + channel];
        output[frame * channels + channel] = scale_sample(sample, volume);
      }
    }
  }
  *out_frames = adjusted_frames;
  *out_channels = channels;
  return HAL_OK;
}

#endif /* HAL_ENABLE_BLUETOOTH_A2DP_SINK */
