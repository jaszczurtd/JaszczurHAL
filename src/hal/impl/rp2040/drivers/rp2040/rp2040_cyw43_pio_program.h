/*
 * Derived from pico-sdk's cyw43_bus_pio_spi.pio.
 *
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <hardware/pio.h>

#define jh_cyw43_pio_wrap_target 0u
#define jh_cyw43_pio_wrap 5u
#define jh_cyw43_pio_offset_lp1_end 2u
#define jh_cyw43_pio_offset_end 6u

static const uint16_t jh_cyw43_pio_program_instructions[] = {
    0x6001, /* out pins, 1 side 0 */
    0x1040, /* jmp x--, 0 side 1 */
    0xe080, /* set pindirs, 0 side 0 */
    0xb042, /* nop side 1 */
    0x4001, /* in pins, 1 side 0 */
    0x1084, /* jmp y--, 4 side 1 */
};

static const struct pio_program jh_cyw43_pio_program = {
    jh_cyw43_pio_program_instructions,
    6u,
    -1,
};

static inline pio_sm_config
jh_cyw43_pio_program_get_default_config(uint offset) {
  pio_sm_config config = pio_get_default_sm_config();
  sm_config_set_wrap(&config, offset + jh_cyw43_pio_wrap_target,
                     offset + jh_cyw43_pio_wrap);
  sm_config_set_sideset(&config, 1u, false, false);
  return config;
}
