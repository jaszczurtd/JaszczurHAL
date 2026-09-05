#pragma once

#include "hal/bluetooth/hal_bluetooth_a2dp_sink.h"
#include "hal/core/hal_status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  hal_bluetooth_a2dp_sbc_format_t format;
  size_t frame_length;
  size_t pcm_frames;
  uint8_t bitpool;
} jh_bluetooth_a2dp_sbc_frame_t;

typedef struct {
  const uint8_t *frames;
  size_t frames_length;
  uint16_t sequence_number;
  uint8_t frame_count;
} jh_bluetooth_a2dp_media_packet_t;

hal_status_t
jh_bluetooth_a2dp_sbc_frame_parse(const uint8_t *data, size_t length,
                                  jh_bluetooth_a2dp_sbc_frame_t *out_frame);

hal_status_t jh_bluetooth_a2dp_media_packet_parse(
    const uint8_t *data, size_t length,
    const hal_bluetooth_a2dp_sbc_format_t *expected_format,
    jh_bluetooth_a2dp_media_packet_t *out_packet);

hal_status_t jh_bluetooth_a2dp_pcm_transform(
    const int16_t *input, size_t frames, uint8_t input_channels,
    hal_bluetooth_a2dp_output_mode_t output_mode, uint8_t volume,
    int8_t frame_adjustment, int16_t *output, size_t output_capacity,
    size_t *out_frames, uint8_t *out_channels);

#ifdef __cplusplus
}
#endif
