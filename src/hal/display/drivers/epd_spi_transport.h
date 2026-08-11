#pragma once

/*
 * Shared SPI/GPIO transport for electrophoretic display controllers.
 *
 * The transport is intentionally controller-neutral.  SSD16xx and UC81xx
 * drivers own their protocol/state machines while this layer owns command/data
 * framing, reset, BUSY polling and SPI transaction cleanup.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal/core/hal_status.h"
#include "hal/spi/hal_spi_device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef JH_EPD_DEFAULT_SPI_HZ
#define JH_EPD_DEFAULT_SPI_HZ 4000000UL
#endif

#ifndef JH_EPD_DEFAULT_BUSY_TIMEOUT_MS
#define JH_EPD_DEFAULT_BUSY_TIMEOUT_MS 30000UL
#endif

typedef struct {
  uint8_t bus;
  int16_t cs_pin;
  int16_t dc_pin;
  int16_t rst_pin;
  int16_t busy_pin;
  uint32_t clock_hz;
  uint32_t busy_timeout_ms;
  uint8_t spi_mode;
  bool busy_active_high;
} jh_epd_spi_config_t;

typedef struct {
  jh_epd_spi_config_t config;
  /* Effective SPI bus, CS and settings state. */
  hal_spi_device_t spi_device;
  bool initialized;
} jh_epd_spi_t;

hal_status_t jh_epd_spi_init(jh_epd_spi_t *transport,
                             const jh_epd_spi_config_t *config);
hal_status_t jh_epd_spi_reset(jh_epd_spi_t *transport, uint32_t pulse_delay_ms);
hal_status_t jh_epd_spi_wait_idle(jh_epd_spi_t *transport);
hal_status_t jh_epd_spi_command(jh_epd_spi_t *transport, uint8_t command,
                                const uint8_t *data, size_t len);
hal_status_t jh_epd_spi_command_pattern(jh_epd_spi_t *transport,
                                        uint8_t command, uint8_t pattern,
                                        size_t len);

#ifdef __cplusplus
}
#endif
