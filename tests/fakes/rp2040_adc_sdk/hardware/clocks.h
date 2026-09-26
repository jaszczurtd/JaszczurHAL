#pragma once

#include <stdint.h>

enum clock_index { clk_adc = 8 };

uint32_t clock_get_hz(enum clock_index clock);
