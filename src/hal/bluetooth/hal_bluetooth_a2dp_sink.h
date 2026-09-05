#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK

/**
 * @file hal_bluetooth_a2dp_sink.h
 * @brief Bluetooth Classic A2DP Sink with bounded SBC and PCM queues.
 *
 * Transport callbacks only copy bounded media packets. SBC decoding and PCM
 * processing run when hal_bluetooth_a2dp_sink_poll() is called. The profile
 * does not select or depend on a physical audio output.
 */

#include "hal/bluetooth/hal_bluetooth_classic.h"
#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Profile identifier stored by the shared Classic bond manager. */
#define HAL_BLUETOOTH_A2DP_SINK_PROFILE_ID UINT16_C(0xA2D1)

/** Maximum PCM frames returned by one read, including clock compensation. */
#define HAL_BLUETOOTH_A2DP_PCM_MAX_FRAMES 129u

/** Maximum copied RTP/A2DP media-packet size in bytes. */
#ifndef HAL_BLUETOOTH_A2DP_PACKET_MAX_LEN
#define HAL_BLUETOOTH_A2DP_PACKET_MAX_LEN 1024u
#endif

/** Number of copied media packets retained before new packets are dropped. */
#ifndef HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH
#define HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH 8u
#endif

/** Number of decoded PCM blocks retained before new blocks are dropped. */
#ifndef HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH
#define HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH 16u
#endif

/** PCM blocks required before playback begins or resumes after an underrun. */
#ifndef HAL_BLUETOOTH_A2DP_PREBUFFER_BLOCKS
#define HAL_BLUETOOTH_A2DP_PREBUFFER_BLOCKS 4u
#endif

#if HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH < 2u
#error "HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH must be at least 2"
#endif

#if HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH < 4u
#error "HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH must be at least 4"
#endif

#if HAL_BLUETOOTH_A2DP_PREBUFFER_BLOCKS < 1u ||                                \
    HAL_BLUETOOTH_A2DP_PREBUFFER_BLOCKS >= HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH
#error "HAL_BLUETOOTH_A2DP_PREBUFFER_BLOCKS must fit the PCM queue"
#endif

/** AVDTP/A2DP lifecycle state. */
typedef enum {
  HAL_BLUETOOTH_A2DP_STATE_CLOSED = 0,
  HAL_BLUETOOTH_A2DP_STATE_READY,
  HAL_BLUETOOTH_A2DP_STATE_CONNECTED,
  HAL_BLUETOOTH_A2DP_STATE_STREAMING,
  HAL_BLUETOOTH_A2DP_STATE_SUSPENDED,
  HAL_BLUETOOTH_A2DP_STATE_FAILED,
} hal_bluetooth_a2dp_state_t;

/** SBC channel mode selected by the source. */
typedef enum {
  HAL_BLUETOOTH_A2DP_CHANNEL_MONO = 1,
  HAL_BLUETOOTH_A2DP_CHANNEL_STEREO,
  HAL_BLUETOOTH_A2DP_CHANNEL_JOINT_STEREO,
} hal_bluetooth_a2dp_channel_mode_t;

/** PCM channel policy applied during polling. */
typedef enum {
  /** Preserve the decoded mono or stereo channel count. */
  HAL_BLUETOOTH_A2DP_OUTPUT_NATIVE = 0,
  /** Produce one channel using a saturating stereo downmix. */
  HAL_BLUETOOTH_A2DP_OUTPUT_MONO,
} hal_bluetooth_a2dp_output_mode_t;

/** Negotiated SBC format, without stack-specific values. */
typedef struct {
  uint32_t sample_rate_hz; /**< 44100 or 48000 Hz. */
  uint8_t channels;        /**< One for mono, two for stereo modes. */
  hal_bluetooth_a2dp_channel_mode_t channel_mode;
  uint8_t block_length; /**< SBC blocks per frame: 4, 8, 12 or 16. */
  uint8_t subbands;     /**< Four or eight SBC subbands. */
  uint8_t min_bitpool;
  uint8_t max_bitpool;
} hal_bluetooth_a2dp_sbc_format_t;

/** Profile configuration copied during open. */
typedef struct {
  hal_bluetooth_a2dp_output_mode_t output_mode;
  uint8_t initial_volume; /**< Absolute volume from 0 through 127. */
} hal_bluetooth_a2dp_sink_config_t;

/** Metadata for a PCM block copied by the application. */
typedef struct {
  uint32_t sample_rate_hz;
  size_t frames;
  uint8_t channels;
  bool clock_adjusted;
} hal_bluetooth_a2dp_pcm_block_t;

/** A2DP state and bounded-resource diagnostics. */
typedef struct {
  hal_bluetooth_a2dp_state_t state;
  hal_status_t last_status;
  hal_bluetooth_classic_address_t peer_address;
  hal_bluetooth_a2dp_sbc_format_t format;
  uint32_t generation;
  uint32_t media_packets;
  uint32_t decoded_frames;
  uint32_t missing_packets;
  uint32_t dropped_packets;
  uint32_t corrupt_frames;
  uint32_t pcm_overflows;
  uint32_t pcm_underruns;
  size_t pending_packets;
  size_t pending_pcm_blocks;
  size_t packet_queue_high_water;
  size_t pcm_queue_high_water;
  int32_t clock_correction_ppm;
  uint8_t volume;
  bool format_valid;
  bool first_audio_valid;
  bool prebuffering;
} hal_bluetooth_a2dp_sink_info_t;

typedef struct hal_bluetooth_a2dp_sink_impl_s hal_bluetooth_a2dp_sink_impl_t;
/** Opaque handle for one attached A2DP Sink profile. */
typedef hal_bluetooth_a2dp_sink_impl_t *hal_bluetooth_a2dp_sink_t;

/**
 * @brief Attach one A2DP Sink profile to an open Classic manager.
 * @param classic Live Classic manager retained by the caller.
 * @param config Configuration copied by the profile; must not be NULL.
 * @param out_sink Receives the profile handle and is cleared on entry; must
 * not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid input, HAL_EUNINIT for an invalid
 * manager, HAL_EBUSY when already attached, HAL_ENOMEM, or a backend error.
 */
hal_status_t
hal_bluetooth_a2dp_sink_open(hal_bluetooth_classic_t classic,
                             const hal_bluetooth_a2dp_sink_config_t *config,
                             hal_bluetooth_a2dp_sink_t *out_sink);

/**
 * @brief Close the profile and discard encoded and PCM queues.
 * @param sink Live profile handle.
 * @return HAL_OK, HAL_EUNINIT for a stale handle, HAL_ENOMEM, or a backend
 * detach error.
 */
hal_status_t hal_bluetooth_a2dp_sink_close(hal_bluetooth_a2dp_sink_t sink);

/**
 * @brief Decode queued SBC packets and prepare bounded PCM blocks.
 * @param sink Live profile handle.
 * @return HAL_OK when serviced, HAL_EUNINIT for a stale handle, HAL_EAGAIN
 * when no packet is pending, HAL_EBUSY when the PCM queue must be drained
 * before decoding the next packet, HAL_EPROTO for a rejected packet/frame,
 * HAL_ENOMEM, or a backend decode error. A rejected packet is consumed.
 */
hal_status_t hal_bluetooth_a2dp_sink_poll(hal_bluetooth_a2dp_sink_t sink);

/**
 * @brief Read state, negotiated format and queue diagnostics.
 * @param sink Live profile handle.
 * @param out_info Receives a snapshot; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for a stale
 * handle, or HAL_ENOMEM.
 */
hal_status_t
hal_bluetooth_a2dp_sink_get_info(hal_bluetooth_a2dp_sink_t sink,
                                 hal_bluetooth_a2dp_sink_info_t *out_info);

/**
 * @brief Set the software PCM volume used by subsequent polling.
 * @param sink Live profile handle.
 * @param absolute_volume AVRCP absolute volume from 0 through 127.
 * @return HAL_OK, HAL_EINVAL above 127, HAL_EUNINIT, or HAL_ENOMEM.
 */
hal_status_t hal_bluetooth_a2dp_sink_set_volume(hal_bluetooth_a2dp_sink_t sink,
                                                uint8_t absolute_volume);

/**
 * @brief Pop one ready signed 16-bit PCM block in the caller's poll context.
 *
 * The first block is withheld until the configured prebuffer threshold is
 * reached. After an underrun the threshold is applied again. A small automatic
 * sample duplication/drop keeps the bounded queue near its target depth.
 *
 * @param sink Live profile handle.
 * @param out_samples Destination for interleaved signed samples; must not be
 * NULL.
 * @param sample_capacity Capacity in int16_t elements. At least
 * HAL_BLUETOOTH_A2DP_PCM_MAX_FRAMES times two supports every native block.
 * @param out_block Receives block metadata and is cleared on entry; must not be
 * NULL.
 * @return HAL_OK, HAL_EINVAL for invalid output, HAL_EUNINIT for a stale
 * handle, HAL_EAGAIN while empty or prebuffering, HAL_EOVERFLOW for a short
 * destination, or HAL_ENOMEM.
 */
hal_status_t
hal_bluetooth_a2dp_sink_pcm_next(hal_bluetooth_a2dp_sink_t sink,
                                 int16_t *out_samples, size_t sample_capacity,
                                 hal_bluetooth_a2dp_pcm_block_t *out_block);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_A2DP_SINK */
