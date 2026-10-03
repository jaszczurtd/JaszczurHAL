#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_MCP251XFD) && defined(HAL_ENABLE_SPI)

#include "mcp251xfd_driver.h"

#include "hal/can/jh_can_bit_timing.h"
#include "hal/core/jh_endian.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"

#include <string.h>

/* Register map: DS20006027B table 3-1. */
#define MCP251XFD_RAM_START_ADDR 0x400u
#define MCP251XFD_OBJ_HEADER 8u
#define MCP251XFD_OBJ_SIZE (MCP251XFD_OBJ_HEADER + HAL_CAN_FD_MAX_DATA_LEN)
#define MCP251XFD_TXQ_DEPTH 1u
#define MCP251XFD_RX_FIFO_IDX 1u
#define MCP251XFD_RX_FIFO_DEPTH 24u
#define MCP251XFD_FILTER_COUNT 32u
/* Last filter: accepts what no other filter took (unmatched policy). */
#define MCP251XFD_FILTER_UNMATCHED 31u

#define MCP251XFD_REG_CON 0x000u
#define MCP251XFD_REG_NBTCFG 0x004u
#define MCP251XFD_REG_DBTCFG 0x008u
#define MCP251XFD_REG_TDC 0x00Cu
#define MCP251XFD_REG_INT 0x01Cu
#define MCP251XFD_REG_TREC 0x034u
#define MCP251XFD_REG_TEFCON 0x040u
#define MCP251XFD_REG_TXQCON 0x050u
#define MCP251XFD_REG_TXQSTA 0x054u
#define MCP251XFD_REG_TXQUA 0x058u
#define MCP251XFD_REG_FIFOCON(x) (0x050u + (0x0Cu * (x)))
#define MCP251XFD_REG_FIFOSTA(x) (0x054u + (0x0Cu * (x)))
#define MCP251XFD_REG_FIFOUA(x) (0x058u + (0x0Cu * (x)))
#define MCP251XFD_REG_FLTCON_BYTE(f) (0x1D0u + (f))
#define MCP251XFD_REG_FLTOBJ(f) (0x1F0u + (0x08u * (f)))
#define MCP251XFD_REG_MASK(f) (0x1F4u + (0x08u * (f)))
#define MCP251XFD_REG_OSC 0xE00u
#define MCP251XFD_REG_CRC 0xE08u
#define MCP251XFD_REG_ECCCON 0xE0Cu

#define MCP251XFD_SPI_RESET 0x0000u
#define MCP251XFD_SPI_WRITE 0x2000u
#define MCP251XFD_SPI_READ 0x3000u
#define MCP251XFD_SPI_ADDR_MASK 0x0FFFu

/* CiCON (register 3-7). */
#define MCP251XFD_CON_OPMOD_SHIFT 21u
#define MCP251XFD_CON_OPMOD_MASK (0x07u << MCP251XFD_CON_OPMOD_SHIFT)
#define MCP251XFD_CON_TXQEN (1u << 20)
#define MCP251XFD_CON_STEF (1u << 19)
#define MCP251XFD_CON_RTXAT (1u << 16)
#define MCP251XFD_CON_WAKFIL (1u << 8)
#define MCP251XFD_CON_ISOCRCEN (1u << 5)
#define MCP251XFD_CON_REQOP_MASK (0x07u << 24)
#define MCP251XFD_CON_ABAT (1u << 27)

#define MCP251XFD_MODE_MIXED 0u
#define MCP251XFD_MODE_SLEEP 1u
#define MCP251XFD_MODE_INT_LOOPBACK 2u
#define MCP251XFD_MODE_LISTENONLY 3u
#define MCP251XFD_MODE_CONFIG 4u
#define MCP251XFD_MODE_EXT_LOOPBACK 5u
#define MCP251XFD_MODE_CAN2_0 6u
#define MCP251XFD_MODE_NONE 0xFFu

#define MCP251XFD_INT_WAKIE (1u << 30)

/* OSC (register 3-1). */
#define MCP251XFD_OSC_PLLEN (1u << 0)
#define MCP251XFD_OSC_CLKODIV_10 (3u << 5)
#define MCP251XFD_OSC_PLLRDY (1u << 8)
#define MCP251XFD_OSC_OSCRDY (1u << 10)
#define MCP251XFD_PLL_INPUT_HZ 4000000u
#define MCP251XFD_PLL_OUTPUT_HZ 40000000u

/* CiTDC (register 3-10). */
#define MCP251XFD_TDC_TDCMOD_AUTO (2u << 16)
#define MCP251XFD_TDC_TDCO_MAX 63u
#define MCP251XFD_TDC_MIN_BITRATE_HZ 1000000u /* FRM 3.4.7 */

/* CiTREC (register 3-20). */
#define MCP251XFD_TREC_TXBO (1u << 21)
#define MCP251XFD_TREC_TXBP (1u << 20)
#define MCP251XFD_TREC_RXBP (1u << 19)
#define MCP251XFD_TREC_TXWARN (1u << 18)
#define MCP251XFD_TREC_RXWARN (1u << 17)
#define MCP251XFD_TREC_EWARN (1u << 16)

/* CiFIFOCONm / CiTXQCON (registers 3-26, 3-29); byte 1 holds the controls. */
#define MCP251XFD_FIFO_PLSIZE_64 (7u << 29)
#define MCP251XFD_FIFO_FSIZE(n) (((uint32_t)(n) & 0x1Fu) << 24)
#define MCP251XFD_FIFO_TXAT_ONE_SHOT (0u << 21)
#define MCP251XFD_FIFO_TXAT_UNLIMITED (3u << 21)
#define MCP251XFD_FIFO_TXAT_MASK (3u << 21)
#define MCP251XFD_CTRL_UINC 0x01u
#define MCP251XFD_CTRL_TXREQ 0x02u
#define MCP251XFD_CTRL_FRESET 0x04u

/* CiFIFOSTAm / CiTXQSTA (registers 3-27, 3-30). */
#define MCP251XFD_FIFO_TFNRFNIF (1u << 0)
#define MCP251XFD_TXQ_TXQEIF (1u << 2)
#define MCP251XFD_FIFO_TXATIF (1u << 4)

/* Message objects (FRM tables 4-1 and 7-1). */
#define MCP251XFD_OBJ_FLAGS_ESI (1u << 8)
#define MCP251XFD_OBJ_FLAGS_FDF (1u << 7)
#define MCP251XFD_OBJ_FLAGS_BRS (1u << 6)
#define MCP251XFD_OBJ_FLAGS_RTR (1u << 5)
#define MCP251XFD_OBJ_FLAGS_IDE (1u << 4)

#define MCP251XFD_FLTOBJ_EXIDE (1u << 30)
#define MCP251XFD_MASK_MIDE (1u << 30)
#define MCP251XFD_FLTCON_FLTEN 0x80u

/* Bit-time ranges, FRM tables 3-1 and 3-2. */
static const jh_can_timing_limits_t kNominalLimits = {
    256u, 2u, 256u, 128u, 128u, JH_CAN_NOMINAL_SP_PERMILLE};
static const jh_can_timing_limits_t kDataLimits = {
    256u, 1u, 32u, 16u, 16u, JH_CAN_DATA_SP_PERMILLE};

static uint32_t encode_id(uint32_t id, uint8_t flags) {
  if ((flags & HAL_CAN_FRAME_EXTENDED) != 0u) {
    return ((id >> 18) & HAL_CAN_STD_ID_MASK) | ((id & 0x3FFFFu) << 11);
  }
  return id & HAL_CAN_STD_ID_MASK;
}

static uint32_t decode_id(uint32_t raw_id, uint32_t raw_flags, uint8_t *flags) {
  if ((raw_flags & MCP251XFD_OBJ_FLAGS_IDE) != 0u) {
    *flags |= HAL_CAN_FRAME_EXTENDED;
    return ((raw_id & HAL_CAN_STD_ID_MASK) << 18) | ((raw_id >> 11) & 0x3FFFFu);
  }
  return raw_id & HAL_CAN_STD_ID_MASK;
}

static uint32_t encode_filter_id(uint32_t id, bool ext) {
  return ext ? MCP251XFD_FLTOBJ_EXIDE | encode_id(id, HAL_CAN_FRAME_EXTENDED)
             : id & HAL_CAN_STD_ID_MASK;
}

/* MIDE set: the filter matches only its own ID kind (FRM 6.2.1). */
static uint32_t encode_filter_mask(uint32_t mask, bool ext) {
  return MCP251XFD_MASK_MIDE | (ext ? encode_id(mask, HAL_CAN_FRAME_EXTENDED)
                                    : mask & HAL_CAN_STD_ID_MASK);
}

static uint32_t encode_bit_time(const jh_can_timing_t *t) {
  return ((uint32_t)(t->prescaler - 1u) << 24) |
         ((uint32_t)(t->segment1 - 1u) << 16) |
         ((uint32_t)(t->segment2 - 1u) << 8) |
         (uint32_t)(t->sync_jump_width - 1u);
}

/* RAM is written and read in whole words (DS20006027B 4.2). */
static uint16_t round_to_word(uint16_t len) {
  return (uint16_t)((len + 3u) & ~3u);
}

JHMCP251XFD::JHMCP251XFD(uint8_t cs_pin, uint8_t spi_bus)
    : m_cs_pin(cs_pin), m_spi_bus(spi_bus), m_spi_clock_hz(10000000u),
      m_fd_enabled(false), m_one_shot(false), m_opmode(MCP251XFD_MODE_NONE),
      m_tdc(0u), m_sysclk_hz(0u), m_send_timeout_us(0u), m_policy_set(false),
      m_accept_rtr(true), m_filters(0u), m_driver_mutex(hal_mutex_create()) {}

JHMCP251XFD::~JHMCP251XFD() { end(); }

void JHMCP251XFD::select_() { hal_gpio_write(m_cs_pin, false); }
void JHMCP251XFD::deselect_() { hal_gpio_write(m_cs_pin, true); }

void JHMCP251XFD::spi_begin_() {
  hal_spi_settings_t s = {m_spi_clock_hz, HAL_SPI_MSBFIRST, HAL_SPI_MODE0};
  hal_spi_begin_transaction(m_spi_bus, &s);
  select_();
}

void JHMCP251XFD::spi_end_() {
  deselect_();
  hal_spi_end_transaction(m_spi_bus);
}

void JHMCP251XFD::lock_driver_() { hal_mutex_lock(m_driver_mutex); }
void JHMCP251XFD::unlock_driver_() { hal_mutex_unlock(m_driver_mutex); }

void JHMCP251XFD::reset_() {
  spi_begin_();
  hal_spi_transfer(m_spi_bus, jh_u16_msb(MCP251XFD_SPI_RESET));
  hal_spi_transfer(m_spi_bus, jh_u16_lsb(MCP251XFD_SPI_RESET));
  spi_end_();
  hal_delay_ms(5u);
}

uint32_t JHMCP251XFD::read_reg_(uint16_t addr) {
  uint8_t rx[4] = {};
  const uint16_t cmd =
      (uint16_t)(MCP251XFD_SPI_READ | (addr & MCP251XFD_SPI_ADDR_MASK));
  spi_begin_();
  hal_spi_transfer(m_spi_bus, jh_u16_msb(cmd));
  hal_spi_transfer(m_spi_bus, jh_u16_lsb(cmd));
  hal_spi_transfer_txrx(m_spi_bus, NULL, rx, sizeof(rx));
  spi_end_();
  return jh_load_le32(rx);
}

void JHMCP251XFD::write_reg_(uint16_t addr, uint32_t value) {
  uint8_t tx[4];
  jh_store_le32(tx, value);
  const uint16_t cmd =
      (uint16_t)(MCP251XFD_SPI_WRITE | (addr & MCP251XFD_SPI_ADDR_MASK));
  spi_begin_();
  hal_spi_transfer(m_spi_bus, jh_u16_msb(cmd));
  hal_spi_transfer(m_spi_bus, jh_u16_lsb(cmd));
  hal_spi_write(m_spi_bus, tx, sizeof(tx));
  spi_end_();
}

/* One SFR byte; control bits such as UINC and TXREQ are written this way so
 * the other bytes of the register are left alone. */
void JHMCP251XFD::write_byte_(uint16_t addr, uint8_t value) {
  const uint16_t cmd =
      (uint16_t)(MCP251XFD_SPI_WRITE | (addr & MCP251XFD_SPI_ADDR_MASK));
  spi_begin_();
  hal_spi_transfer(m_spi_bus, jh_u16_msb(cmd));
  hal_spi_transfer(m_spi_bus, jh_u16_lsb(cmd));
  hal_spi_write(m_spi_bus, &value, 1u);
  spi_end_();
}

void JHMCP251XFD::read_ram_(uint16_t addr, uint8_t *data, uint16_t len) {
  const uint16_t cmd =
      (uint16_t)(MCP251XFD_SPI_READ | (addr & MCP251XFD_SPI_ADDR_MASK));
  spi_begin_();
  hal_spi_transfer(m_spi_bus, jh_u16_msb(cmd));
  hal_spi_transfer(m_spi_bus, jh_u16_lsb(cmd));
  hal_spi_transfer_txrx(m_spi_bus, NULL, data, len);
  spi_end_();
}

void JHMCP251XFD::write_ram_(uint16_t addr, const uint8_t *data, uint16_t len) {
  const uint16_t cmd =
      (uint16_t)(MCP251XFD_SPI_WRITE | (addr & MCP251XFD_SPI_ADDR_MASK));
  spi_begin_();
  hal_spi_transfer(m_spi_bus, jh_u16_msb(cmd));
  hal_spi_transfer(m_spi_bus, jh_u16_lsb(cmd));
  hal_spi_write(m_spi_bus, data, len);
  spi_end_();
}

/* Request a mode through REQOP alone and wait for OPMOD (FRM 2.1);
 * HAL_ETIMEOUT when OPMOD did not follow within ~100 ms. */
hal_status_t JHMCP251XFD::set_mode_raw_(uint8_t mode) {
  write_byte_(MCP251XFD_REG_CON + 3u, mode);
  for (uint8_t i = 0; i < 100u; i++) {
    const uint32_t con = read_reg_(MCP251XFD_REG_CON);
    if (((con & MCP251XFD_CON_OPMOD_MASK) >> MCP251XFD_CON_OPMOD_SHIFT) ==
        mode) {
      m_opmode = mode;
      return HAL_OK;
    }
    hal_delay_ms(1u);
  }
  return HAL_ETIMEOUT;
}

/* Normal and debug modes are only entered from Configuration mode (FRM
 * 2.1.1, 2.1.2), which is also the only mode where TDC may change (FRM 2.2).
 * Entering it resets the FIFOs, so frames still queued are dropped. */
hal_status_t JHMCP251XFD::goto_mode_(uint8_t mode, bool one_shot) {
  if (mode == m_opmode && one_shot == m_one_shot) {
    return HAL_OK;
  }
  if (m_opmode != MCP251XFD_MODE_CONFIG) {
    const hal_status_t st = set_mode_raw_(MCP251XFD_MODE_CONFIG);
    if (st != HAL_OK) {
      return st;
    }
  }
  if (mode == MCP251XFD_MODE_CONFIG) {
    return HAL_OK;
  }
  write_reg_(MCP251XFD_REG_TDC, mode == MCP251XFD_MODE_MIXED ? m_tdc : 0u);
  write_byte_(MCP251XFD_REG_TXQCON + 2u,
              (uint8_t)((one_shot ? MCP251XFD_FIFO_TXAT_ONE_SHOT
                                  : MCP251XFD_FIFO_TXAT_UNLIMITED) >>
                        16));
  m_one_shot = one_shot;
  return set_mode_raw_(mode);
}

/* 40 and 20 MHz clock the controller directly; 4 MHz needs the PLL (FRM
 * 3.1). OSCRDY (and PLLRDY) must follow within ~100 ms, else HAL_ETIMEOUT. */
hal_status_t JHMCP251XFD::configure_oscillator_(uint32_t osc_hz,
                                                uint32_t *sysclk_hz) {
  const bool pll = osc_hz == MCP251XFD_PLL_INPUT_HZ;
  uint32_t ready = MCP251XFD_OSC_OSCRDY;
  if (pll) {
    write_byte_(MCP251XFD_REG_OSC,
                (uint8_t)(MCP251XFD_OSC_CLKODIV_10 | MCP251XFD_OSC_PLLEN));
    ready |= MCP251XFD_OSC_PLLRDY;
  }
  *sysclk_hz = pll ? MCP251XFD_PLL_OUTPUT_HZ : osc_hz;
  for (uint8_t i = 0; i < 100u; i++) {
    if ((read_reg_(MCP251XFD_REG_OSC) & ready) == ready) {
      return HAL_OK;
    }
    hal_delay_ms(1u);
  }
  return HAL_ETIMEOUT;
}

/* HAL_EUNSUPPORTED when a bitrate has no timing at this clock. */
hal_status_t
JHMCP251XFD::configure_bit_timing_(const hal_can_mcp251xfd_config_t *cfg,
                                   uint32_t sysclk_hz) {
  jh_can_timing_t nominal = {};
  jh_can_timing_t data = {};
  const uint32_t data_rate = cfg->data_bitrate_hz != 0u
                                 ? cfg->data_bitrate_hz
                                 : cfg->arbitration_bitrate_hz;
  hal_status_t st = jh_can_compute_timing(
      sysclk_hz, cfg->arbitration_bitrate_hz, &kNominalLimits,
      cfg->arbitration_sample_point_permille, 0u, &nominal);
  if (st == HAL_OK) {
    /* The same prescaler in both phases when it fits (FRM 3.4.7). */
    st = jh_can_compute_timing(sysclk_hz, data_rate, &kDataLimits,
                               cfg->data_sample_point_permille,
                               nominal.prescaler, &data);
  }
  if (st != HAL_OK) {
    return st;
  }
  write_reg_(MCP251XFD_REG_NBTCFG, encode_bit_time(&nominal));
  write_reg_(MCP251XFD_REG_DBTCFG, encode_bit_time(&data));
  /* Automatic TDC from 1 Mbit/s; the secondary sample point at the data
   * sample point: TDCO = DBRP x DTSEG1 (FRM 3.4.8). */
  m_tdc = 0u;
  if (m_fd_enabled && data.actual_bitrate_hz >= MCP251XFD_TDC_MIN_BITRATE_HZ) {
    uint32_t tdco = (uint32_t)data.prescaler * data.segment1;
    if (tdco > MCP251XFD_TDC_TDCO_MAX) {
      tdco = MCP251XFD_TDC_TDCO_MAX;
    }
    m_tdc = MCP251XFD_TDC_TDCMOD_AUTO | (tdco << 8);
  }
  m_send_timeout_us = 1400000000u / cfg->arbitration_bitrate_hz + 500u;
  return HAL_OK;
}

void JHMCP251XFD::configure_fifos_() {
  write_reg_(MCP251XFD_REG_TEFCON, 0u);
  write_reg_(MCP251XFD_REG_TXQCON,
             MCP251XFD_FIFO_PLSIZE_64 |
                 MCP251XFD_FIFO_FSIZE(MCP251XFD_TXQ_DEPTH - 1u) |
                 MCP251XFD_FIFO_TXAT_UNLIMITED);
  write_reg_(MCP251XFD_REG_FIFOCON(MCP251XFD_RX_FIFO_IDX),
             MCP251XFD_FIFO_PLSIZE_64 |
                 MCP251XFD_FIFO_FSIZE(MCP251XFD_RX_FIFO_DEPTH - 1u));
}

uint8_t JHMCP251XFD::op_mode_for_hal_(hal_can_mode_t mode) const {
  if ((mode & HAL_CAN_MODE_SLEEP) != 0u) {
    return MCP251XFD_MODE_SLEEP;
  }
  if ((mode & HAL_CAN_MODE_LISTEN_ONLY) != 0u) {
    return MCP251XFD_MODE_LISTENONLY;
  }
  if ((mode & HAL_CAN_MODE_LOOPBACK) != 0u) {
    return MCP251XFD_MODE_INT_LOOPBACK;
  }
  if ((mode & HAL_CAN_MODE_EXTERNAL_LOOPBACK) != 0u) {
    return MCP251XFD_MODE_EXT_LOOPBACK;
  }
  return (m_fd_enabled && ((mode & HAL_CAN_MODE_FD) != 0u))
             ? MCP251XFD_MODE_MIXED
             : MCP251XFD_MODE_CAN2_0;
}

/* The filter is disabled while its object and mask change (FRM 6.1). */
void JHMCP251XFD::program_filter_(uint8_t filter, uint32_t object,
                                  uint32_t mask) {
  write_byte_(MCP251XFD_REG_FLTCON_BYTE(filter), 0u);
  write_reg_(MCP251XFD_REG_FLTOBJ(filter), object);
  write_reg_(MCP251XFD_REG_MASK(filter), mask);
  write_byte_(MCP251XFD_REG_FLTCON_BYTE(filter),
              (uint8_t)(MCP251XFD_FLTCON_FLTEN | MCP251XFD_RX_FIFO_IDX));
}

void JHMCP251XFD::disable_filter_(uint8_t filter) {
  write_byte_(MCP251XFD_REG_FLTCON_BYTE(filter), 0u);
}

/* Filter 31 with an empty mask: both ID kinds when MIDE is clear, one kind
 * when it is set (FRM 6.2.1). */
void JHMCP251XFD::accept_unmatched_(bool accept_std, bool accept_ext) {
  if (!accept_std && !accept_ext) {
    disable_filter_(MCP251XFD_FILTER_UNMATCHED);
  } else if (accept_std && accept_ext) {
    program_filter_(MCP251XFD_FILTER_UNMATCHED, 0u, 0u);
  } else {
    program_filter_(MCP251XFD_FILTER_UNMATCHED,
                    accept_ext ? MCP251XFD_FLTOBJ_EXIDE : 0u,
                    MCP251XFD_MASK_MIDE);
  }
}

hal_status_t JHMCP251XFD::begin(const hal_can_mcp251xfd_config_t *cfg) {
  if (!cfg || cfg->arbitration_bitrate_hz == 0u || cfg->oscillator_hz == 0u) {
    return HAL_EINVAL;
  }

  lock_driver_();
  m_spi_bus = cfg->spi_bus;
  m_cs_pin = cfg->cs_pin;
  m_spi_clock_hz = cfg->spi_clock_hz ? cfg->spi_clock_hz : 10000000u;
  m_fd_enabled = cfg->enable_fd;
  m_opmode = MCP251XFD_MODE_NONE;
  m_policy_set = false;
  m_accept_rtr = true;
  m_filters = 0u;

  hal_gpio_set_mode(m_cs_pin, HAL_GPIO_OUTPUT_HIGH);
  /* RESET is only accepted in Configuration mode (DS20006027B 4.1.1); a
   * chip left running by a previous program has to go there first. */
  (void)set_mode_raw_(MCP251XFD_MODE_CONFIG);
  reset_();
  uint32_t sysclk_hz = 0u;
  const uint32_t con = read_reg_(MCP251XFD_REG_CON);
  /* After RESET the chip reports Configuration mode; anything else means it
   * did not answer. */
  hal_status_t st = ((con & MCP251XFD_CON_OPMOD_MASK) >>
                     MCP251XFD_CON_OPMOD_SHIFT) == MCP251XFD_MODE_CONFIG
                        ? HAL_OK
                        : HAL_EIO;
  if (st == HAL_OK) {
    st = configure_oscillator_(cfg->oscillator_hz, &sysclk_hz);
  }
  if (st == HAL_OK) {
    m_sysclk_hz = sysclk_hz;
    m_opmode = MCP251XFD_MODE_CONFIG;
    write_reg_(MCP251XFD_REG_CRC, 0u);
    write_reg_(MCP251XFD_REG_ECCCON, 0u);
    write_reg_(MCP251XFD_REG_INT, cfg->sleep_wakeup ? MCP251XFD_INT_WAKIE : 0u);
    /* TXQ without a transmit event FIFO; TXAT decides the attempts. */
    uint32_t c = read_reg_(MCP251XFD_REG_CON);
    c &= ~(MCP251XFD_CON_STEF | MCP251XFD_CON_WAKFIL |
           MCP251XFD_CON_REQOP_MASK | MCP251XFD_CON_ABAT);
    c |= MCP251XFD_CON_TXQEN | MCP251XFD_CON_RTXAT | MCP251XFD_CON_ISOCRCEN |
         ((uint32_t)MCP251XFD_MODE_CONFIG << 24) |
         (cfg->sleep_wakeup ? MCP251XFD_CON_WAKFIL : 0u);
    write_reg_(MCP251XFD_REG_CON, c);
    st = configure_bit_timing_(cfg, sysclk_hz);
  }
  if (st == HAL_OK) {
    configure_fifos_();
    /* Without filters everything is received, remote frames too. */
    accept_unmatched_(true, true);
    st = goto_mode_(m_fd_enabled ? MCP251XFD_MODE_MIXED : MCP251XFD_MODE_CAN2_0,
                    cfg->one_shot_tx);
  }
  unlock_driver_();
  return st;
}

void JHMCP251XFD::end() {
  if (m_driver_mutex) {
    hal_mutex_lock(m_driver_mutex);
    if (m_opmode != MCP251XFD_MODE_NONE) {
      (void)set_mode_raw_(MCP251XFD_MODE_CONFIG);
    }
    hal_mutex_unlock(m_driver_mutex);
    hal_mutex_destroy(m_driver_mutex);
    m_driver_mutex = NULL;
  }
}

/* Wait for the TXQ to empty. A failed one-shot attempt sets TXATIF and keeps
 * the object at the queue index (FRM 4.9.1): HAL_EIO. After a timeout the
 * frame is aborted: HAL_ETIMEOUT. Either way the TXQ is reset so the frame
 * never goes out later. */
hal_status_t JHMCP251XFD::finish_send_() {
  const uint32_t started = hal_micros();
  uint32_t sta = 0u;
  do {
    sta = read_reg_(MCP251XFD_REG_TXQSTA);
    if ((sta & MCP251XFD_TXQ_TXQEIF) != 0u) {
      return HAL_OK;
    }
  } while ((sta & MCP251XFD_FIFO_TXATIF) == 0u &&
           !hal_elapsed_u32(hal_micros(), started, m_send_timeout_us));
  /* The register decides, not the clock: one last look before giving up. */
  if ((read_reg_(MCP251XFD_REG_TXQSTA) & MCP251XFD_TXQ_TXQEIF) != 0u) {
    return HAL_OK;
  }
  write_byte_(MCP251XFD_REG_TXQCON + 1u, 0u); /* clear TXREQ: abort */
  write_byte_(MCP251XFD_REG_TXQCON + 1u, MCP251XFD_CTRL_FRESET);
  return (sta & MCP251XFD_FIFO_TXATIF) != 0u ? HAL_EIO : HAL_ETIMEOUT;
}

/* The facade validates the frame and the FD capability first. */
hal_status_t JHMCP251XFD::send_frame(const hal_can_frame_t *frame) {
  if (!frame) {
    return HAL_EINVAL;
  }

  uint8_t obj[MCP251XFD_OBJ_SIZE] = {};
  jh_store_le32(&obj[0], encode_id(frame->id, frame->flags));
  uint32_t flags = frame->dlc & 0x0Fu;
  if ((frame->flags & HAL_CAN_FRAME_EXTENDED) != 0u)
    flags |= MCP251XFD_OBJ_FLAGS_IDE;
  if ((frame->flags & HAL_CAN_FRAME_RTR) != 0u)
    flags |= MCP251XFD_OBJ_FLAGS_RTR;
  if ((frame->flags & HAL_CAN_FRAME_FD) != 0u)
    flags |= MCP251XFD_OBJ_FLAGS_FDF;
  if ((frame->flags & HAL_CAN_FRAME_BRS) != 0u)
    flags |= MCP251XFD_OBJ_FLAGS_BRS;
  if ((frame->flags & HAL_CAN_FRAME_ESI) != 0u)
    flags |= MCP251XFD_OBJ_FLAGS_ESI;
  jh_store_le32(&obj[4], flags);
  uint16_t len = MCP251XFD_OBJ_HEADER;
  if ((frame->flags & HAL_CAN_FRAME_RTR) == 0u && frame->len > 0u) {
    memcpy(&obj[MCP251XFD_OBJ_HEADER], frame->data, frame->len);
    len = (uint16_t)(len + round_to_word(hal_can_dlc_to_bytes(frame->dlc)));
  }

  lock_driver_();
  const bool sending = m_opmode == MCP251XFD_MODE_MIXED ||
                       m_opmode == MCP251XFD_MODE_CAN2_0 ||
                       m_opmode == MCP251XFD_MODE_INT_LOOPBACK ||
                       m_opmode == MCP251XFD_MODE_EXT_LOOPBACK;
  hal_status_t st = HAL_EBUSY;
  if (sending &&
      (read_reg_(MCP251XFD_REG_TXQSTA) & MCP251XFD_FIFO_TFNRFNIF) != 0u) {
    /* The user address is relative to the start of RAM (FRM eq. 4-1). */
    const uint16_t ua =
        (uint16_t)(read_reg_(MCP251XFD_REG_TXQUA) & MCP251XFD_SPI_ADDR_MASK);
    write_ram_((uint16_t)(MCP251XFD_RAM_START_ADDR + ua), obj, len);
    write_byte_(MCP251XFD_REG_TXQCON + 1u,
                MCP251XFD_CTRL_UINC | MCP251XFD_CTRL_TXREQ);
    st = finish_send_();
  }
  unlock_driver_();
  return st;
}

hal_status_t JHMCP251XFD::available() {
  lock_driver_();
  uint32_t sta = read_reg_(MCP251XFD_REG_FIFOSTA(MCP251XFD_RX_FIFO_IDX));
  unlock_driver_();
  return (sta & MCP251XFD_FIFO_TFNRFNIF) != 0u ? HAL_OK : HAL_EAGAIN;
}

hal_status_t JHMCP251XFD::receive_frame(hal_can_frame_t *frame) {
  if (!frame) {
    return HAL_EINVAL;
  }
  uint8_t obj[MCP251XFD_OBJ_SIZE];
  uint32_t raw_flags = 0u;
  bool found = false;
  lock_driver_();
  /* At most one FIFO's worth of skipped remote frames per call. */
  for (uint8_t i = 0; i <= MCP251XFD_RX_FIFO_DEPTH && !found; ++i) {
    const uint32_t sta =
        read_reg_(MCP251XFD_REG_FIFOSTA(MCP251XFD_RX_FIFO_IDX));
    if ((sta & MCP251XFD_FIFO_TFNRFNIF) == 0u) {
      break;
    }
    /* The user address is relative to the start of RAM (FRM eq. 7-1). */
    const uint16_t ua =
        (uint16_t)(read_reg_(MCP251XFD_REG_FIFOUA(MCP251XFD_RX_FIFO_IDX)) &
                   MCP251XFD_SPI_ADDR_MASK);
    read_ram_((uint16_t)(MCP251XFD_RAM_START_ADDR + ua), obj, sizeof(obj));
    write_byte_(MCP251XFD_REG_FIFOCON(MCP251XFD_RX_FIFO_IDX) + 1u,
                MCP251XFD_CTRL_UINC);
    raw_flags = jh_load_le32(&obj[4]);
    /* The chip cannot refuse remote frames; the policy drops them here. */
    found = m_accept_rtr || (raw_flags & MCP251XFD_OBJ_FLAGS_RTR) == 0u;
  }
  unlock_driver_();
  if (!found) {
    return HAL_EAGAIN;
  }

  memset(frame, 0, sizeof(*frame));
  frame->flags = 0u;
  frame->id = decode_id(jh_load_le32(&obj[0]), raw_flags, &frame->flags);
  if ((raw_flags & MCP251XFD_OBJ_FLAGS_RTR) != 0u)
    frame->flags |= HAL_CAN_FRAME_RTR;
  if ((raw_flags & MCP251XFD_OBJ_FLAGS_FDF) != 0u)
    frame->flags |= HAL_CAN_FRAME_FD;
  if ((raw_flags & MCP251XFD_OBJ_FLAGS_BRS) != 0u)
    frame->flags |= HAL_CAN_FRAME_BRS;
  if ((raw_flags & MCP251XFD_OBJ_FLAGS_ESI) != 0u)
    frame->flags |= HAL_CAN_FRAME_ESI;
  frame->dlc = (uint8_t)(raw_flags & 0x0Fu);
  if ((frame->flags & HAL_CAN_FRAME_FD) == 0u &&
      frame->dlc > HAL_CAN_MAX_DATA_LEN) {
    frame->dlc = HAL_CAN_MAX_DATA_LEN; /* classic DLC 9..15 means 8 bytes */
  }
  frame->len = hal_can_dlc_to_bytes(frame->dlc);
  if ((frame->flags & HAL_CAN_FRAME_RTR) == 0u && frame->len > 0u) {
    memcpy(frame->data, &obj[MCP251XFD_OBJ_HEADER], frame->len);
  }
  /* A malformed object (BRS or ESI without FDF) means a bad SPI read. */
  return hal_can_validate_frame(frame) == HAL_OK ? HAL_OK : HAL_EIO;
}

hal_status_t JHMCP251XFD::stop() {
  lock_driver_();
  const hal_status_t st = goto_mode_(MCP251XFD_MODE_CONFIG, m_one_shot);
  unlock_driver_();
  return st;
}

hal_status_t JHMCP251XFD::set_mode(hal_can_mode_t mode) {
  lock_driver_();
  const hal_status_t st =
      goto_mode_(op_mode_for_hal_(mode), (mode & HAL_CAN_MODE_ONE_SHOT) != 0u);
  unlock_driver_();
  return st;
}

hal_status_t JHMCP251XFD::get_state(hal_can_state_t *state) {
  if (!state) {
    return HAL_EINVAL;
  }
  lock_driver_();
  uint32_t trec = read_reg_(MCP251XFD_REG_TREC);
  unlock_driver_();
  if ((trec & MCP251XFD_TREC_TXBO) != 0u) {
    *state = HAL_CAN_STATE_BUS_OFF;
  } else if ((trec & (MCP251XFD_TREC_TXBP | MCP251XFD_TREC_RXBP)) != 0u) {
    *state = HAL_CAN_STATE_ERROR_PASSIVE;
  } else if ((trec & (MCP251XFD_TREC_EWARN | MCP251XFD_TREC_TXWARN |
                      MCP251XFD_TREC_RXWARN)) != 0u) {
    *state = HAL_CAN_STATE_ERROR_WARNING;
  } else {
    *state = HAL_CAN_STATE_ERROR_ACTIVE;
  }
  return HAL_OK;
}

hal_status_t
JHMCP251XFD::get_error_counters(hal_can_error_counters_t *counters) {
  if (!counters) {
    return HAL_EINVAL;
  }
  lock_driver_();
  uint32_t trec = read_reg_(MCP251XFD_REG_TREC);
  unlock_driver_();
  counters->rx = (uint8_t)(trec & 0xFFu);
  counters->tx = (uint8_t)((trec >> 8) & 0xFFu);
  return HAL_OK;
}

/* Classic slots are filters 0..5. The first one switches frames no filter
 * matches to rejected unless set_unmatched_policy() decided before. */
hal_status_t JHMCP251XFD::set_filter(uint8_t index,
                                     const hal_can_filter_t *filter) {
  if (!filter || index >= HAL_CAN_MAX_FILTERS) {
    return HAL_EINVAL;
  }
  const bool ext = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u;
  lock_driver_();
  if (!m_policy_set) {
    accept_unmatched_(false, false);
    m_policy_set = true;
  }
  program_filter_(index, encode_filter_id(filter->id, ext),
                  encode_filter_mask(filter->mask, ext));
  m_filters |= 1u << index;
  unlock_driver_();
  return HAL_OK;
}

/* Filters only steer frames into a FIFO: mask filters that accept. */
hal_status_t JHMCP251XFD::add_filter(const hal_can_filter_ex_t *filter,
                                     uint8_t *index) {
  if (filter->type != HAL_CAN_FILTER_MASK ||
      filter->action != HAL_CAN_FILTER_ACCEPT) {
    return HAL_EUNSUPPORTED;
  }
  const bool ext = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u;
  lock_driver_();
  hal_status_t st = HAL_ENOMEM;
  for (uint8_t f = HAL_CAN_FILTER_FIRST_ADDED; f < MCP251XFD_FILTER_UNMATCHED;
       ++f) {
    if ((m_filters & (1u << f)) == 0u) {
      program_filter_(f, encode_filter_id(filter->id1, ext),
                      encode_filter_mask(filter->id2, ext));
      m_filters |= 1u << f;
      *index = f;
      st = HAL_OK;
      break;
    }
  }
  unlock_driver_();
  return st;
}

hal_status_t JHMCP251XFD::remove_filter(uint8_t index) {
  if (index >= MCP251XFD_FILTER_UNMATCHED ||
      (m_filters & (1u << index)) == 0u) {
    return HAL_ENOENT;
  }
  lock_driver_();
  disable_filter_(index);
  m_filters &= ~(1u << index);
  unlock_driver_();
  return HAL_OK;
}

hal_status_t JHMCP251XFD::set_unmatched_policy(bool accept_std, bool accept_ext,
                                               bool accept_rtr) {
  lock_driver_();
  accept_unmatched_(accept_std, accept_ext);
  m_accept_rtr = accept_rtr;
  m_policy_set = true;
  unlock_driver_();
  return HAL_OK;
}

#endif /* HAL_ENABLE_MCP251XFD && HAL_ENABLE_SPI */
