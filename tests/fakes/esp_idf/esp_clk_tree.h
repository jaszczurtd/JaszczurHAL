#pragma once

#include "esp_err.h"

#include <stdint.h>

typedef enum { SOC_MOD_CLK_APB = 1 } soc_module_clk_t;
typedef enum {
  ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED = 0
} esp_clk_tree_src_freq_precision_t;

esp_err_t
esp_clk_tree_src_get_freq_hz(soc_module_clk_t clk_src,
                             esp_clk_tree_src_freq_precision_t precision,
                             uint32_t *freq_value);
