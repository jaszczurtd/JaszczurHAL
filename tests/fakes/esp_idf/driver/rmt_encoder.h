#pragma once

#include "esp_err.h"

#include <stdint.h>

typedef struct fake_rmt_encoder *rmt_encoder_handle_t;

typedef struct {
  uint16_t duration0;
  uint16_t level0;
  uint16_t duration1;
  uint16_t level1;
} rmt_symbol_word_t;

typedef struct {
  rmt_symbol_word_t bit0;
  rmt_symbol_word_t bit1;
  struct {
    uint32_t msb_first : 1;
  } flags;
} rmt_bytes_encoder_config_t;

esp_err_t rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t *config,
                                rmt_encoder_handle_t *ret_encoder);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder);
