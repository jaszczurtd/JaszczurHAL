#pragma once

#include <stdint.h>

enum clock_index { clk_sys = 5 };

/* The default system clock of the modelled chip. */
uint32_t clock_get_hz(enum clock_index clock);
