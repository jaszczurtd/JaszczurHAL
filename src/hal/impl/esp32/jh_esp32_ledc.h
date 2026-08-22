#pragma once

#include <stdbool.h>
#include <stdint.h>

struct jh_esp32_ledc_channel_s;
typedef struct jh_esp32_ledc_channel_s jh_esp32_ledc_channel_t;

jh_esp32_ledc_channel_t *
jh_esp32_ledc_acquire(uint8_t pin, uint32_t frequency_hz, uint32_t logical_max);
bool jh_esp32_ledc_write(jh_esp32_ledc_channel_t *channel,
                         uint32_t logical_value);
void jh_esp32_ledc_stop(jh_esp32_ledc_channel_t *channel);
bool jh_esp32_ledc_release(jh_esp32_ledc_channel_t *channel);
uint32_t jh_esp32_ledc_source_clock_hz(void);
