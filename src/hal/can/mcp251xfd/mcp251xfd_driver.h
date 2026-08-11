#pragma once

/*
 * HAL-only MCP251XFD driver inspired by the Zephyr MCP251XFD register model.
 * The implementation intentionally avoids Zephyr device/devicetree/IRQ glue and
 * exposes only the polling operations needed by JaszczurHAL's CAN facade.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal/can/hal_can.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/spi/hal_spi.h"
#include "hal/system/hal_sync.h"

class JHMCP251XFD {
private:
  uint8_t m_cs_pin;
  uint8_t m_spi_bus;
  uint32_t m_spi_clock_hz;
  bool m_fd_enabled;
  bool m_one_shot;
  hal_mutex_t m_driver_mutex;

  void select_();
  void deselect_();
  void spi_begin_();
  void spi_end_();
  void lock_driver_();
  void unlock_driver_();

  void reset_();
  uint32_t read_reg_(uint16_t addr);
  void write_reg_(uint16_t addr, uint32_t value);
  void write_reg_or_(uint16_t addr, uint32_t bits);
  void read_ram_(uint16_t addr, uint8_t *data, uint16_t len);
  void write_ram_(uint16_t addr, const uint8_t *data, uint16_t len);

  bool set_mode_raw_(uint8_t mode);
  bool configure_bit_timing_(uint32_t osc_hz, uint32_t arb_bitrate_hz,
                             uint32_t data_bitrate_hz);
  bool configure_fifos_();
  uint8_t op_mode_for_hal_(hal_can_mode_t mode) const;
  bool validate_mode_(hal_can_mode_t mode) const;

public:
  JHMCP251XFD(uint8_t cs_pin, uint8_t spi_bus = 0);
  ~JHMCP251XFD();

  bool begin(const hal_can_mcp251xfd_config_t *cfg);
  void end();
  bool send_frame(const hal_can_frame_t *frame);
  bool receive_frame(hal_can_frame_t *frame);
  bool available();
  bool start(hal_can_mode_t mode);
  bool stop();
  bool set_mode(hal_can_mode_t mode);
  bool get_state(bool started, hal_can_state_t *state);
  bool get_error_counters(hal_can_error_counters_t *counters);
  bool set_filter(uint8_t index, const hal_can_filter_t *filter);
};
