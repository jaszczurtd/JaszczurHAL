#pragma once

#include <stdint.h>

/* ESP32-S3 ADC types the continuous driver and the HAL backend share. */

typedef enum { ADC_UNIT_1 = 0, ADC_UNIT_2 = 1 } adc_unit_t;

typedef enum {
  ADC_CHANNEL_0 = 0,
  ADC_CHANNEL_1,
  ADC_CHANNEL_2,
  ADC_CHANNEL_3,
  ADC_CHANNEL_4,
  ADC_CHANNEL_5,
  ADC_CHANNEL_6,
  ADC_CHANNEL_7,
  ADC_CHANNEL_8,
  ADC_CHANNEL_9,
} adc_channel_t;

typedef enum {
  ADC_ATTEN_DB_0 = 0,
  ADC_ATTEN_DB_2_5 = 1,
  ADC_ATTEN_DB_6 = 2,
  ADC_ATTEN_DB_12 = 3,
} adc_atten_t;

typedef enum {
  ADC_CONV_SINGLE_UNIT_1 = 1,
  ADC_CONV_SINGLE_UNIT_2 = 2,
} adc_digi_convert_mode_t;

typedef enum { ADC_DIGI_OUTPUT_FORMAT_TYPE2 = 1 } adc_digi_output_format_t;

typedef struct {
  uint8_t atten;
  uint8_t channel;
  uint8_t unit;
  uint8_t bit_width;
} adc_digi_pattern_config_t;

/* One 4-byte record of the continuous stream (type2 layout). */
typedef union {
  struct {
    uint32_t data : 12;
    uint32_t channel : 4;
    uint32_t unit : 1;
    uint32_t reserved : 15;
  } type2;
  uint32_t val;
} adc_digi_output_data_t;
