#include "hal/bluetooth/hal_bluetooth_a2dp_sink.h"

#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK

#include "hal/bluetooth/jh_bluetooth_a2dp_runtime.h"
#include "hal/bluetooth/jh_bluetooth_a2dp_sbc.h"
#include "hal/bluetooth/jh_bluetooth_classic_runtime.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_target.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/system/hal_sync.h"

#include <string.h>

#define JH_BLUETOOTH_A2DP_HANDLE_KIND 17u

namespace {

struct media_packet_t {
  uint8_t data[HAL_BLUETOOTH_A2DP_PACKET_MAX_LEN];
  size_t length;
};

struct pcm_block_t {
  int16_t samples[128u * 2u];
  size_t frames;
  uint32_t sample_rate_hz;
  uint8_t channels;
};

struct a2dp_runtime_t {
  hal_mutex_t mutex;
  jh_handle_pool_t handle_pool;
  jh_handle_slot_t handle_slot;
  hal_bluetooth_classic_t classic;
  hal_bluetooth_a2dp_sink_config_t config;
  hal_bluetooth_a2dp_sink_info_t info;
  media_packet_t packets[HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH];
  pcm_block_t pcm[HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH];
  size_t packet_head;
  size_t packet_count;
  size_t pcm_head;
  size_t pcm_count;
  int32_t correction_accumulator;
  uint16_t last_sequence;
  bool handle_pool_initialized;
  bool sequence_valid;
  bool playback_started;
  bool bond_saved;
};

a2dp_runtime_t s_a2dp{};

hal_mutex_t runtime_mutex() { return jh_hal_mutex_create_once(&s_a2dp.mutex); }

hal_status_t ensure_handle_pool_locked() {
  if (s_a2dp.handle_pool_initialized) {
    return HAL_OK;
  }
  const hal_status_t status =
      jh_handle_pool_init(&s_a2dp.handle_pool, &s_a2dp.handle_slot, 1u,
                          JH_BLUETOOTH_A2DP_HANDLE_KIND);
  s_a2dp.handle_pool_initialized = status == HAL_OK;
  return status;
}

bool handle_valid_locked(hal_bluetooth_a2dp_sink_t sink) {
  if (!s_a2dp.handle_pool_initialized || s_a2dp.classic == nullptr) {
    return false;
  }
  void *runtime = nullptr;
  return jh_handle_resolve(&s_a2dp.handle_pool, sink, &runtime, nullptr) ==
             HAL_OK &&
         runtime == &s_a2dp &&
         jh_bluetooth_classic_handle_valid(s_a2dp.classic);
}

void reset_queues_locked(bool prebuffer) {
  s_a2dp.packet_head = 0u;
  s_a2dp.packet_count = 0u;
  s_a2dp.pcm_head = 0u;
  s_a2dp.pcm_count = 0u;
  s_a2dp.sequence_valid = false;
  s_a2dp.playback_started = false;
  s_a2dp.correction_accumulator = 0;
  s_a2dp.info.pending_packets = 0u;
  s_a2dp.info.pending_pcm_blocks = 0u;
  s_a2dp.info.prebuffering = prebuffer;
  s_a2dp.info.clock_correction_ppm = 0;
  memset(s_a2dp.packets, 0, sizeof(s_a2dp.packets));
  memset(s_a2dp.pcm, 0, sizeof(s_a2dp.pcm));
}

void queue_media_locked(const uint8_t *data, size_t length) {
  ++s_a2dp.info.media_packets;
  if (data == nullptr || length == 0u ||
      length > HAL_BLUETOOTH_A2DP_PACKET_MAX_LEN ||
      s_a2dp.packet_count == HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH) {
    ++s_a2dp.info.dropped_packets;
    return;
  }
  const size_t tail = (s_a2dp.packet_head + s_a2dp.packet_count) %
                      HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH;
  memcpy(s_a2dp.packets[tail].data, data, length);
  s_a2dp.packets[tail].length = length;
  ++s_a2dp.packet_count;
  s_a2dp.info.pending_packets = s_a2dp.packet_count;
  if (s_a2dp.packet_count > s_a2dp.info.packet_queue_high_water) {
    s_a2dp.info.packet_queue_high_water = s_a2dp.packet_count;
  }
}

void pop_media_locked() {
  s_a2dp.packet_head =
      (s_a2dp.packet_head + 1u) % HAL_BLUETOOTH_A2DP_PACKET_QUEUE_DEPTH;
  --s_a2dp.packet_count;
  s_a2dp.info.pending_packets = s_a2dp.packet_count;
}

void queue_pcm_locked(const jh_bluetooth_classic_backend_event_t *event) {
  if (event->pcm_data == nullptr || event->pcm_frames == 0u ||
      event->pcm_frames > 128u ||
      (event->pcm_channels != 1u && event->pcm_channels != 2u) ||
      (event->pcm_sample_rate_hz != 44100u &&
       event->pcm_sample_rate_hz != 48000u)) {
    ++s_a2dp.info.corrupt_frames;
    return;
  }
  if (s_a2dp.pcm_count == HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH) {
    ++s_a2dp.info.pcm_overflows;
    return;
  }
  const size_t tail =
      (s_a2dp.pcm_head + s_a2dp.pcm_count) % HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH;
  pcm_block_t &block = s_a2dp.pcm[tail];
  block.frames = event->pcm_frames;
  block.channels = event->pcm_channels;
  block.sample_rate_hz = event->pcm_sample_rate_hz;
  memcpy(block.samples, event->pcm_data,
         block.frames * block.channels * sizeof(block.samples[0]));
  ++s_a2dp.pcm_count;
  s_a2dp.info.pending_pcm_blocks = s_a2dp.pcm_count;
  if (s_a2dp.pcm_count > s_a2dp.info.pcm_queue_high_water) {
    s_a2dp.info.pcm_queue_high_water = s_a2dp.pcm_count;
  }
}

hal_status_t resolve(hal_bluetooth_a2dp_sink_t sink,
                     hal_bluetooth_classic_t *out_classic) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(sink)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  *out_classic = s_a2dp.classic;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

} // namespace

hal_status_t
hal_bluetooth_a2dp_sink_open(hal_bluetooth_classic_t classic,
                             const hal_bluetooth_a2dp_sink_config_t *config,
                             hal_bluetooth_a2dp_sink_t *out_sink) {
  if (classic == nullptr || config == nullptr || out_sink == nullptr ||
      config->initial_volume > 127u ||
      (config->output_mode != HAL_BLUETOOTH_A2DP_OUTPUT_NATIVE &&
       config->output_mode != HAL_BLUETOOTH_A2DP_OUTPUT_MONO)) {
    return HAL_EINVAL;
  }
  *out_sink = nullptr;
  if (!jh_bluetooth_classic_handle_valid(classic)) {
    return HAL_EUNINIT;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_a2dp.classic != nullptr) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  hal_status_t status = ensure_handle_pool_locked();
  void *handle = nullptr;
  if (status == HAL_OK) {
    status = jh_handle_allocate(&s_a2dp.handle_pool, &s_a2dp, &handle);
  }
  if (status == HAL_OK) {
    s_a2dp.classic = classic;
    s_a2dp.config = *config;
    memset(&s_a2dp.info, 0, sizeof(s_a2dp.info));
    s_a2dp.info.state = HAL_BLUETOOTH_A2DP_STATE_READY;
    s_a2dp.info.last_status = HAL_NONE;
    s_a2dp.info.volume = config->initial_volume;
    s_a2dp.bond_saved = false;
    reset_queues_locked(false);
  }
  hal_mutex_unlock(mutex);
  if (status == HAL_OK) {
    status = jh_bluetooth_classic_a2dp_attach(classic);
  }
  if (status != HAL_OK) {
    hal_mutex_lock(mutex);
    if (handle != nullptr) {
      void *runtime = nullptr;
      (void)jh_handle_release(&s_a2dp.handle_pool, handle, &runtime);
    }
    s_a2dp.classic = nullptr;
    hal_mutex_unlock(mutex);
    return status;
  }
  *out_sink = static_cast<hal_bluetooth_a2dp_sink_t>(handle);
  return HAL_OK;
}

hal_status_t hal_bluetooth_a2dp_sink_close(hal_bluetooth_a2dp_sink_t sink) {
  hal_bluetooth_classic_t classic = nullptr;
  hal_status_t status = resolve(sink, &classic);
  if (status != HAL_OK) {
    return status;
  }
  status = jh_bluetooth_classic_a2dp_detach(classic);
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(s_a2dp.mutex);
  void *runtime = nullptr;
  const hal_status_t release_status =
      jh_handle_release(&s_a2dp.handle_pool, sink, &runtime);
  reset_queues_locked(false);
  memset(&s_a2dp.info, 0, sizeof(s_a2dp.info));
  s_a2dp.classic = nullptr;
  hal_mutex_unlock(s_a2dp.mutex);
  return release_status;
}

hal_status_t hal_bluetooth_a2dp_sink_poll(hal_bluetooth_a2dp_sink_t sink) {
  hal_bluetooth_classic_t classic = nullptr;
  hal_status_t status = resolve(sink, &classic);
  if (status != HAL_OK) {
    return status;
  }
  media_packet_t packet{};
  hal_bluetooth_a2dp_sbc_format_t format{};
  hal_bluetooth_classic_address_t peer{};
  hal_mutex_lock(s_a2dp.mutex);
  if (s_a2dp.packet_count == 0u) {
    hal_mutex_unlock(s_a2dp.mutex);
    return HAL_EAGAIN;
  }
  packet = s_a2dp.packets[s_a2dp.packet_head];
  format = s_a2dp.info.format;
  peer = s_a2dp.info.peer_address;
  const bool ready = s_a2dp.info.format_valid &&
                     s_a2dp.info.state == HAL_BLUETOOTH_A2DP_STATE_STREAMING;
  hal_mutex_unlock(s_a2dp.mutex);
  if (!ready) {
    hal_mutex_lock(s_a2dp.mutex);
    pop_media_locked();
    ++s_a2dp.info.dropped_packets;
    hal_mutex_unlock(s_a2dp.mutex);
    return HAL_ESTATE;
  }

  jh_bluetooth_a2dp_media_packet_t parsed{};
  status = jh_bluetooth_a2dp_media_packet_parse(packet.data, packet.length,
                                                &format, &parsed);
  if (status != HAL_OK) {
    hal_mutex_lock(s_a2dp.mutex);
    pop_media_locked();
    ++s_a2dp.info.corrupt_frames;
    s_a2dp.info.last_status = status;
    hal_mutex_unlock(s_a2dp.mutex);
    return status;
  }

  hal_mutex_lock(s_a2dp.mutex);
  if (parsed.frame_count >
      HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH - s_a2dp.pcm_count) {
    hal_mutex_unlock(s_a2dp.mutex);
    return HAL_EBUSY;
  }
  pop_media_locked();
  if (s_a2dp.sequence_valid) {
    const uint16_t expected = (uint16_t)(s_a2dp.last_sequence + 1u);
    const uint16_t distance = (uint16_t)(parsed.sequence_number - expected);
    if (distance < UINT16_C(0x8000)) {
      s_a2dp.info.missing_packets += distance;
      s_a2dp.last_sequence = parsed.sequence_number;
    }
  } else {
    s_a2dp.last_sequence = parsed.sequence_number;
    s_a2dp.sequence_valid = true;
  }
  hal_mutex_unlock(s_a2dp.mutex);

  size_t offset = 0u;
  uint8_t decoded_frames = 0u;
  for (uint8_t index = 0u; index < parsed.frame_count; ++index) {
    jh_bluetooth_a2dp_sbc_frame_t frame{};
    status = jh_bluetooth_a2dp_sbc_frame_parse(
        parsed.frames + offset, parsed.frames_length - offset, &frame);
    if (status != HAL_OK) {
      break;
    }
    status = jh_bluetooth_classic_a2dp_decode(classic, parsed.frames + offset,
                                              frame.frame_length);
    if (status != HAL_OK) {
      break;
    }
    offset += frame.frame_length;
    ++decoded_frames;
  }

  hal_mutex_lock(s_a2dp.mutex);
  s_a2dp.info.decoded_frames += decoded_frames;
  if (decoded_frames != 0u) {
    s_a2dp.info.first_audio_valid = true;
  }
  if (status != HAL_OK) {
    ++s_a2dp.info.corrupt_frames;
    s_a2dp.info.last_status = status;
  }
  const bool save_bond = decoded_frames != 0u && !s_a2dp.bond_saved;
  hal_mutex_unlock(s_a2dp.mutex);
  if (save_bond) {
    const hal_status_t save_status = hal_bluetooth_classic_peer_save(
        classic, &peer, HAL_BLUETOOTH_A2DP_SINK_PROFILE_ID);
    if (save_status == HAL_OK) {
      hal_mutex_lock(s_a2dp.mutex);
      s_a2dp.bond_saved = true;
      hal_mutex_unlock(s_a2dp.mutex);
    } else if (save_status != HAL_EAUTH) {
      return save_status;
    }
  }
  return status;
}

hal_status_t
hal_bluetooth_a2dp_sink_get_info(hal_bluetooth_a2dp_sink_t sink,
                                 hal_bluetooth_a2dp_sink_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(sink)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  s_a2dp.info.pending_packets = s_a2dp.packet_count;
  s_a2dp.info.pending_pcm_blocks = s_a2dp.pcm_count;
  *out_info = s_a2dp.info;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_bluetooth_a2dp_sink_set_volume(hal_bluetooth_a2dp_sink_t sink,
                                                uint8_t absolute_volume) {
  if (absolute_volume > 127u) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(sink)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  s_a2dp.info.volume = absolute_volume;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t
hal_bluetooth_a2dp_sink_pcm_next(hal_bluetooth_a2dp_sink_t sink,
                                 int16_t *out_samples, size_t sample_capacity,
                                 hal_bluetooth_a2dp_pcm_block_t *out_block) {
  if (out_samples == nullptr || out_block == nullptr) {
    return HAL_EINVAL;
  }
  memset(out_block, 0, sizeof(*out_block));
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  pcm_block_t block{};
  hal_bluetooth_a2dp_output_mode_t output_mode;
  uint8_t volume;
  int8_t adjustment = 0;
  hal_mutex_lock(mutex);
  if (!handle_valid_locked(sink)) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_a2dp.info.prebuffering) {
    if (s_a2dp.pcm_count < HAL_BLUETOOTH_A2DP_PREBUFFER_BLOCKS) {
      hal_mutex_unlock(mutex);
      return HAL_EAGAIN;
    }
    s_a2dp.info.prebuffering = false;
    s_a2dp.playback_started = true;
  }
  if (s_a2dp.pcm_count == 0u) {
    if (s_a2dp.playback_started) {
      ++s_a2dp.info.pcm_underruns;
      s_a2dp.playback_started = false;
      s_a2dp.info.prebuffering = true;
    }
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  block = s_a2dp.pcm[s_a2dp.pcm_head];
  output_mode = s_a2dp.config.output_mode;
  if (s_a2dp.info.format.channels == 1u) {
    output_mode = HAL_BLUETOOTH_A2DP_OUTPUT_MONO;
  }
  volume = s_a2dp.info.volume;
  /*
   * A2DP sources commonly deliver eight SBC frames in one RTP packet. Keep
   * the correction pivot at the middle of the PCM queue so every block in a
   * normal low-water packet contributes to clock recovery. A wide dead band
   * only corrected the last few blocks of each packet and could not cover the
   * RP PWM divider quantization over a long stream.
   */
  const size_t target_level = HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH / 2u;
  const size_t remaining = s_a2dp.pcm_count - 1u;
  int32_t correction_ppm = 0;
  if (remaining < target_level) {
    correction_ppm = 2000;
  } else if (remaining > target_level) {
    correction_ppm = -2000;
  }
  int32_t next_accumulator =
      s_a2dp.correction_accumulator + correction_ppm * (int32_t)block.frames;
  if (next_accumulator >= 1000000) {
    adjustment = 1;
    next_accumulator -= 1000000;
  } else if (next_accumulator <= -1000000) {
    adjustment = -1;
    next_accumulator += 1000000;
  }
  const uint8_t prospective_channels =
      output_mode == HAL_BLUETOOTH_A2DP_OUTPUT_MONO ? 1u : block.channels;
  size_t prospective_frames = block.frames;
  if (adjustment > 0) {
    ++prospective_frames;
  } else if (adjustment < 0) {
    --prospective_frames;
  }
  if (prospective_frames * prospective_channels > sample_capacity) {
    hal_mutex_unlock(mutex);
    return HAL_EOVERFLOW;
  }
  s_a2dp.pcm_head = (s_a2dp.pcm_head + 1u) % HAL_BLUETOOTH_A2DP_PCM_QUEUE_DEPTH;
  --s_a2dp.pcm_count;
  s_a2dp.info.pending_pcm_blocks = s_a2dp.pcm_count;
  s_a2dp.info.clock_correction_ppm = correction_ppm;
  s_a2dp.correction_accumulator = next_accumulator;
  hal_mutex_unlock(mutex);

  size_t output_frames = 0u;
  uint8_t output_channels = 0u;
  const hal_status_t status = jh_bluetooth_a2dp_pcm_transform(
      block.samples, block.frames, block.channels, output_mode, volume,
      adjustment, out_samples, sample_capacity, &output_frames,
      &output_channels);
  if (status != HAL_OK) {
    return status;
  }
  out_block->sample_rate_hz = block.sample_rate_hz;
  out_block->frames = output_frames;
  out_block->channels = output_channels;
  out_block->clock_adjusted = adjustment != 0;
  return HAL_OK;
}

void jh_bluetooth_a2dp_sink_backend_event(
    const jh_bluetooth_classic_backend_event_t *event) {
  if (event == nullptr) {
    return;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_a2dp.classic == nullptr) {
    hal_mutex_unlock(mutex);
    return;
  }
  switch (event->type) {
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_CONNECTED:
    s_a2dp.info.state = HAL_BLUETOOTH_A2DP_STATE_CONNECTED;
    s_a2dp.info.last_status = event->status;
    s_a2dp.info.peer_address = event->address;
    ++s_a2dp.info.generation;
    if (s_a2dp.info.generation == 0u) {
      s_a2dp.info.generation = 1u;
    }
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_FORMAT:
    s_a2dp.info.format = event->a2dp_format;
    s_a2dp.info.format_valid = event->status == HAL_OK;
    s_a2dp.info.last_status = event->status;
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_STARTED:
    reset_queues_locked(true);
    s_a2dp.info.state = HAL_BLUETOOTH_A2DP_STATE_STREAMING;
    s_a2dp.info.last_status = HAL_OK;
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_SUSPENDED:
    reset_queues_locked(false);
    s_a2dp.info.state = HAL_BLUETOOTH_A2DP_STATE_SUSPENDED;
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_STOPPED:
    reset_queues_locked(false);
    s_a2dp.info.state = HAL_BLUETOOTH_A2DP_STATE_CONNECTED;
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_DISCONNECTED:
    reset_queues_locked(false);
    s_a2dp.info.state = HAL_BLUETOOTH_A2DP_STATE_READY;
    s_a2dp.info.last_status = event->status;
    s_a2dp.info.format_valid = false;
    memset(&s_a2dp.info.peer_address, 0, sizeof(s_a2dp.info.peer_address));
    s_a2dp.bond_saved = false;
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_MEDIA:
    if (s_a2dp.info.state == HAL_BLUETOOTH_A2DP_STATE_STREAMING) {
      queue_media_locked(event->media_data, event->media_length);
    } else {
      ++s_a2dp.info.dropped_packets;
    }
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_A2DP_PCM:
    if (s_a2dp.info.state == HAL_BLUETOOTH_A2DP_STATE_STREAMING) {
      queue_pcm_locked(event);
    }
    break;
  case JH_BLUETOOTH_CLASSIC_EVENT_ERROR:
    if (event->fatal) {
      reset_queues_locked(false);
      s_a2dp.info.state = HAL_BLUETOOTH_A2DP_STATE_FAILED;
      s_a2dp.info.last_status = event->status;
    }
    break;
  default:
    break;
  }
  hal_mutex_unlock(mutex);
}

#if HAL_TARGET_IS_MOCK
void hal_mock_bluetooth_a2dp_runtime_full_reset(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_a2dp.handle_pool_initialized) {
    jh_handle_invalidate_all(&s_a2dp.handle_pool);
  }
  reset_queues_locked(false);
  s_a2dp.classic = nullptr;
  hal_mutex_unlock(mutex);
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_A2DP_SINK */
