#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_RP_CYW43_PIO_PROGRAM_HIGH_SPEED = 0,
  JH_RP_CYW43_PIO_PROGRAM_LOW_SPEED = 1,
} jh_rp2040_cyw43_pio_program_t;

typedef struct {
  uint32_t clk_sys_hz;
  uint32_t target_gspi_hz;
  uint16_t divider_int;
  uint8_t divider_frac8;
  uint32_t actual_gspi_hz;
  jh_rp2040_cyw43_pio_program_t program;
} jh_rp2040_cyw43_gspi_clock_t;

/*
 * spi_gap01_sample0 is hardware-validated down to 20.8 MHz gSPI
 * (125 MHz clk_sys, divider 3). Divider 4 failed at 15.6 MHz by sampling the
 * first response bit one clock late, so lower rates use upstream's
 * spi_gap0_sample1 timing instead. That low-speed path is hardware-validated
 * at 15.625 MHz on RP2040 with an external PIM730/RM2.
 */
#define JH_RP_CYW43_HIGH_SPEED_MIN_GSPI_HZ 20000000u

static inline bool jh_rp2040_cyw43_gspi_clock_calculate(
    uint32_t clk_sys_hz, uint32_t target_gspi_hz,
    uint32_t divider_override_x256,
    jh_rp2040_cyw43_gspi_clock_t *clock_config) {
  if (clk_sys_hz == 0u || target_gspi_hz == 0u || clock_config == NULL) {
    return false;
  }

  uint64_t divider_x256 = divider_override_x256;
  if (divider_x256 == 0u) {
    const uint64_t denominator = (uint64_t)target_gspi_hz * 2u;
    divider_x256 =
        ((uint64_t)clk_sys_hz * 256u + denominator - 1u) / denominator;
  }

  const uint64_t min_divider_x256 = 1u * 256u;
  const uint64_t max_divider_x256 = 65535u * 256u + 255u;
  if (divider_x256 < min_divider_x256 || divider_x256 > max_divider_x256) {
    return false;
  }

  const uint32_t actual_gspi_hz =
      (uint32_t)(((uint64_t)clk_sys_hz * 128u) / divider_x256);
  if (actual_gspi_hz == 0u ||
      (divider_override_x256 == 0u && actual_gspi_hz > target_gspi_hz)) {
    return false;
  }

  clock_config->clk_sys_hz = clk_sys_hz;
  clock_config->target_gspi_hz = target_gspi_hz;
  clock_config->divider_int = (uint16_t)(divider_x256 >> 8u);
  clock_config->divider_frac8 = (uint8_t)divider_x256;
  clock_config->actual_gspi_hz = actual_gspi_hz;
  clock_config->program = actual_gspi_hz < JH_RP_CYW43_HIGH_SPEED_MIN_GSPI_HZ
                              ? JH_RP_CYW43_PIO_PROGRAM_LOW_SPEED
                              : JH_RP_CYW43_PIO_PROGRAM_HIGH_SPEED;
  return true;
}

#ifdef __cplusplus
}
#endif
