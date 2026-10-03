#pragma once

/*
 * HAL-only MCP2517FD/MCP2518FD driver, polled over SPI. The register model
 * follows the MCP25XXFD Family Reference Manual (DS20005678E) and the
 * MCP2518FD data sheet (DS20006027B); Zephyr's can_mcp251xfd driver was the
 * starting point. It does not depend on Zephyr's device, devicetree or IRQ
 * infrastructure.
 *
 * RAM layout: no transmit event FIFO, a one-object TXQ, then FIFO 1 as the
 * receive FIFO; every object has a 64-byte payload. Filters 0..30 belong to
 * the application (classic slots first), filter 31 accepts the frames no
 * other filter took while the channel accepts unmatched frames.
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
  bool m_one_shot;      /* retransmission limited to one attempt */
  uint8_t m_opmode;     /* CiCON.OPMOD last requested and confirmed */
  uint32_t m_tdc;       /* CiTDC used in Normal CAN FD mode */
  uint32_t m_sysclk_hz; /* controller clock after the optional PLL */
  uint32_t m_send_timeout_us;
  bool m_policy_set; /* set_unmatched_policy() or a classic filter ran */
  bool m_accept_rtr;
  uint32_t m_filters; /* filters 0..30 in use */
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
  void write_byte_(uint16_t addr, uint8_t value);
  void read_ram_(uint16_t addr, uint8_t *data, uint16_t len);
  void write_ram_(uint16_t addr, const uint8_t *data, uint16_t len);

  hal_status_t set_mode_raw_(uint8_t mode);
  hal_status_t goto_mode_(uint8_t mode, bool one_shot);
  hal_status_t configure_oscillator_(uint32_t osc_hz, uint32_t *sysclk_hz);
  hal_status_t configure_bit_timing_(const hal_can_mcp251xfd_config_t *cfg,
                                     uint32_t sysclk_hz);
  void configure_fifos_();
  uint8_t op_mode_for_hal_(hal_can_mode_t mode) const;
  void program_filter_(uint8_t filter, uint32_t object, uint32_t mask);
  void disable_filter_(uint8_t filter);
  void accept_unmatched_(bool accept_std, bool accept_ext);
  hal_status_t finish_send_();

public:
  JHMCP251XFD(uint8_t cs_pin, uint8_t spi_bus = 0);
  ~JHMCP251XFD();

  hal_status_t begin(const hal_can_mcp251xfd_config_t *cfg);
  void end();
  /** Controller clock after begin(): the oscillator, or 40 MHz from the PLL
   *  of a 4 MHz crystal. */
  uint32_t sysclk_hz() const { return m_sysclk_hz; }
  /** Send and wait until the frame went out.
   *  @return HAL_OK, HAL_EBUSY when the mode does not send or the TXQ is
   *          full, HAL_EIO when the frame did not go out. */
  hal_status_t send_frame(const hal_can_frame_t *frame);
  hal_status_t receive_frame(hal_can_frame_t *frame);
  hal_status_t available();
  hal_status_t stop();
  hal_status_t set_mode(hal_can_mode_t mode);
  hal_status_t get_state(hal_can_state_t *state);
  hal_status_t get_error_counters(hal_can_error_counters_t *counters);
  hal_status_t set_filter(uint8_t index, const hal_can_filter_t *filter);
  hal_status_t add_filter(const hal_can_filter_ex_t *filter, uint8_t *index);
  hal_status_t remove_filter(uint8_t index);
  hal_status_t set_unmatched_policy(bool accept_std, bool accept_ext,
                                    bool accept_rtr);
};
