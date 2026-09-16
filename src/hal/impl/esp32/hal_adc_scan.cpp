#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_S3
#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_ADC_SCAN
#include "hal/analog/jh_adc_scan_backend.h"
#include "hal/system/hal_system.h"
#include "jh_esp32_adc_scan.h"
#include "jh_esp32_status.h"

#include <esp_adc/adc_continuous.h>
#include <hal/adc_types.h>
#include <soc/soc_caps.h>

#include <string.h>

namespace {

// The ESP-IDF continuous driver keeps its own DMA ring of 4-byte records in
// pattern order. Blocks are collected by the task that calls take(): the
// stream is decoded in small fixed chunks into the caller buffer, resyncing
// on the first pattern position, so no heap is used beyond the driver's own
// pool. Completion time and marker therefore belong to the collecting task.
constexpr uint32_t kChunkRecords = 64u;
constexpr uint32_t kRecordBytes = SOC_ADC_DIGI_RESULT_BYTES;
constexpr uint32_t kChunkBytes = kChunkRecords * kRecordBytes;

struct scan_state_t {
  hal_adc_scan_config_t config;
  adc_continuous_handle_t handle;
  uint32_t samples_per_block;
  bool started;
  uint8_t position_of_channel[SOC_ADC_CHANNEL_NUM(0)];
  uint8_t writing;       /* half being filled */
  uint32_t frame;        /* frames complete in the half being filled */
  uint8_t next_position; /* expected pattern position of the next record */
  uint32_t sequence;
  uint8_t completed;
  uint32_t completed_us;
  uint32_t marker;
  uint8_t chunk[kChunkBytes] __attribute__((aligned(4)));
};

scan_state_t s = {};

uint16_t *half(uint8_t index) {
  return s.config.buffer + ((uint32_t)index * s.samples_per_block);
}

void reset_stream(void) {
  s.writing = 0u;
  s.frame = 0u;
  s.next_position = 0u;
  s.sequence = 0u;
  s.completed = 0u;
  s.completed_us = 0u;
  s.marker = 0u;
}

void complete_block(void) {
  s.marker =
      s.config.marker != NULL ? s.config.marker(s.config.marker_user) : 0u;
  s.completed_us = hal_micros();
  s.completed = s.writing;
  s.writing = (uint8_t)(1u - s.writing);
  s.frame = 0u;
  ++s.sequence;
}

/* Decode one chunk of records; returns true when a block completed. */
bool decode(const uint8_t *bytes, uint32_t length) {
  bool completed = false;
  for (uint32_t offset = 0u; offset + kRecordBytes <= length;
       offset += kRecordBytes) {
    adc_digi_output_data_t record;
    (void)memcpy(&record, bytes + offset, sizeof(record));
    if (record.type2.unit != 0u ||
        record.type2.channel >= SOC_ADC_CHANNEL_NUM(0)) {
      continue;
    }
    const uint8_t position = s.position_of_channel[record.type2.channel];
    if (position == UINT8_MAX) {
      continue;
    }
    if (position != s.next_position) {
      // Mid-pattern start or a dropped record: wait for the pattern head.
      s.next_position = 0u;
      if (position != 0u) {
        continue;
      }
    }
    half(s.writing)[(s.frame * s.config.pin_count) + position] =
        (uint16_t)record.type2.data;
    if (position + 1u == s.config.pin_count) {
      s.next_position = 0u;
      if (++s.frame == s.config.block_frames) {
        complete_block();
        completed = true;
      }
    } else {
      s.next_position = (uint8_t)(position + 1u);
    }
  }
  return completed;
}

bool scan_reader(uint8_t pin, uint16_t *raw) {
  for (uint8_t i = 0u; i < s.config.pin_count; ++i) {
    if (s.config.pins[i] == pin) {
      return jh_adc_scan_latest(i, raw) == HAL_OK;
    }
  }
  return false;
}

} // namespace

hal_status_t jh_adc_scan_start(const hal_adc_scan_config_t *config,
                               uint8_t *positions, uint32_t *frame_period_ns) {
  if (s.started) {
    return HAL_EBUSY;
  }
  adc_digi_pattern_config_t pattern[HAL_ADC_SCAN_MAX_PINS] = {};
  for (uint8_t i = 0u; i < SOC_ADC_CHANNEL_NUM(0); ++i) {
    s.position_of_channel[i] = UINT8_MAX;
  }
  for (uint8_t i = 0u; i < config->pin_count; ++i) {
    if (config->pins[i] == HAL_ADC_SCAN_PIN_TEMPERATURE) {
      return HAL_EUNSUPPORTED; /* the S3 temperature sensor is not an ADC
                                  channel */
    }
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    if (adc_continuous_io_to_channel((int)config->pins[i], &unit, &channel) !=
        ESP_OK) {
      return HAL_EINVAL;
    }
    if (unit != ADC_UNIT_1) {
      return HAL_EUNSUPPORTED; /* continuous mode is kept to ADC1 */
    }
    pattern[i].atten = ADC_ATTEN_DB_12;
    pattern[i].channel = (uint8_t)channel;
    pattern[i].unit = (uint8_t)unit;
    pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    s.position_of_channel[(uint8_t)channel] = i;
    positions[i] = i;
  }
  const uint64_t wanted_hz = 1000000000ull / config->conversion_period_ns;
  if (wanted_hz > SOC_ADC_SAMPLE_FREQ_THRES_HIGH) {
    return HAL_EUNSUPPORTED;
  }
  const uint32_t sample_hz = wanted_hz < SOC_ADC_SAMPLE_FREQ_THRES_LOW
                                 ? SOC_ADC_SAMPLE_FREQ_THRES_LOW
                                 : (uint32_t)wanted_hz;

  s.config = *config;
  s.samples_per_block = config->block_frames * (uint32_t)config->pin_count;
  reset_stream();

  adc_continuous_handle_cfg_t handle_config = {};
  handle_config.max_store_buf_size = kChunkBytes * 8u;
  handle_config.conv_frame_size = kChunkBytes;
  esp_err_t err = adc_continuous_new_handle(&handle_config, &s.handle);
  if (err != ESP_OK) {
    s.handle = NULL;
    return jh_esp32_status_from_esp_err(err);
  }
  adc_continuous_config_t stream_config = {};
  stream_config.pattern_num = config->pin_count;
  stream_config.adc_pattern = pattern;
  stream_config.sample_freq_hz = sample_hz;
  stream_config.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  err = adc_continuous_config(s.handle, &stream_config);
  if (err == ESP_OK) {
    err = adc_continuous_start(s.handle);
  }
  if (err != ESP_OK) {
    (void)adc_continuous_deinit(s.handle);
    s.handle = NULL;
    return jh_esp32_status_from_esp_err(err);
  }
  jh_esp32_adc_set_scan_reader(scan_reader);
  s.started = true;
  *frame_period_ns =
      (uint32_t)((1000000000ull * config->pin_count) / sample_hz);
  return HAL_OK;
}

hal_status_t jh_adc_scan_stop(void) {
  if (s.started) {
    jh_esp32_adc_set_scan_reader(NULL);
    (void)adc_continuous_stop(s.handle);
    s.started = false;
  }
  if (s.handle != NULL) {
    (void)adc_continuous_deinit(s.handle);
    s.handle = NULL;
  }
  return HAL_OK;
}

bool jh_adc_scan_completed(hal_adc_scan_block_t *block) {
  if (!s.started) {
    return false;
  }
  // Drain whatever the driver has; the newest completed block wins.
  for (uint32_t rounds = 0u; rounds < 64u; ++rounds) {
    uint32_t length = 0u;
    const esp_err_t err =
        adc_continuous_read(s.handle, s.chunk, kChunkBytes, &length, 0u);
    if (err != ESP_OK || length == 0u) {
      break;
    }
    (void)decode(s.chunk, length);
  }
  if (s.sequence == 0u) {
    return false;
  }
  jh_adc_scan_describe(block, half(s.completed), s.config.block_frames,
                       s.config.pin_count, s.sequence, s.completed_us,
                       s.marker);
  return true;
}

hal_status_t jh_adc_scan_latest(uint8_t position, uint16_t *raw) {
  if (!s.started || position >= s.config.pin_count) {
    return HAL_ESTATE;
  }
  if (s.frame >= 1u) {
    *raw = half(s.writing)[((s.frame - 1u) * s.config.pin_count) + position];
    return HAL_OK;
  }
  if (s.sequence == 0u) {
    return HAL_EAGAIN;
  }
  *raw = half(s.completed)[((s.config.block_frames - 1u) * s.config.pin_count) +
                           position];
  return HAL_OK;
}

#endif // HAL_ENABLE_ADC_SCAN
#endif // HAL_TARGET_IS_ESP32_S3
