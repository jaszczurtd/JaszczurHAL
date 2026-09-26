#pragma once

#include "esp_err.h"
#include "hal/adc_types.h"

#include <stdint.h>

/* Fake of the ESP-IDF continuous ADC driver: a test feeds records into the
 * driver ring and the code under test reads them in frames. */

typedef struct adc_continuous_ctx_t *adc_continuous_handle_t;

typedef struct {
  uint32_t max_store_buf_size;
  uint32_t conv_frame_size;
} adc_continuous_handle_cfg_t;

typedef struct {
  uint32_t pattern_num;
  adc_digi_pattern_config_t *adc_pattern;
  uint32_t sample_freq_hz;
  adc_digi_convert_mode_t conv_mode;
  adc_digi_output_format_t format;
} adc_continuous_config_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t adc_continuous_new_handle(const adc_continuous_handle_cfg_t *config,
                                    adc_continuous_handle_t *out_handle);
esp_err_t adc_continuous_config(adc_continuous_handle_t handle,
                                const adc_continuous_config_t *config);
esp_err_t adc_continuous_start(adc_continuous_handle_t handle);
esp_err_t adc_continuous_stop(adc_continuous_handle_t handle);
esp_err_t adc_continuous_deinit(adc_continuous_handle_t handle);
esp_err_t adc_continuous_read(adc_continuous_handle_t handle, uint8_t *buf,
                              uint32_t length_max, uint32_t *out_length,
                              uint32_t timeout_ms);
esp_err_t adc_continuous_io_to_channel(int io_num, adc_unit_t *unit_id,
                                       adc_channel_t *channel);

#ifdef __cplusplus
}
#endif
