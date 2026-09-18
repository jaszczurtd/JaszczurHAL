#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum { LEDC_LOW_SPEED_MODE = 0 } ledc_mode_t;
typedef int ledc_timer_bit_t;
typedef int ledc_timer_t;
typedef int ledc_channel_t;
typedef enum { LEDC_USE_APB_CLK = 1 } ledc_clk_cfg_t;
typedef enum { LEDC_SLEEP_MODE_NO_ALIVE_NO_PD = 0 } ledc_sleep_mode_t;

typedef struct {
  ledc_mode_t speed_mode;
  ledc_timer_bit_t duty_resolution;
  ledc_timer_t timer_num;
  uint32_t freq_hz;
  ledc_clk_cfg_t clk_cfg;
} ledc_timer_config_t;

typedef struct {
  int gpio_num;
  ledc_mode_t speed_mode;
  ledc_channel_t channel;
  ledc_timer_t timer_sel;
  uint32_t duty;
  int hpoint;
  ledc_sleep_mode_t sleep_mode;
  bool deconfigure;
} ledc_channel_config_t;

uint32_t ledc_find_suitable_duty_resolution(uint32_t src_clk_freq,
                                            uint32_t timer_freq);
esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf);
esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf);
esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel,
                    uint32_t idle_level);
esp_err_t ledc_set_duty_and_update(ledc_mode_t speed_mode,
                                   ledc_channel_t channel, uint32_t duty,
                                   uint32_t hpoint);
