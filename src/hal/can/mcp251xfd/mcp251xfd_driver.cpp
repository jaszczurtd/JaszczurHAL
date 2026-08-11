#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_MCP251XFD) && defined(HAL_ENABLE_SPI)

#include "mcp251xfd_driver.h"

#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"

#include <string.h>

/* Register and object definitions follow the MCP2517FD/MCP2518FD layout used
 * by Zephyr's can_mcp251xfd driver, trimmed to the polling path needed here.
 */
#define MCP251XFD_RAM_START_ADDR 0x400u
#define MCP251XFD_TXQ_START_ADDR MCP251XFD_RAM_START_ADDR
#define MCP251XFD_RX_FIFO_START_ADDR (MCP251XFD_TXQ_START_ADDR + 72u)
#define MCP251XFD_RX_FIFO_IDX 1u

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
#define MCP251XFD_REG_FLTCON(m) (0x1D0u + (m))
#define MCP251XFD_REG_FLTOBJ(x) (0x1F0u + (0x08u * (x)))
#define MCP251XFD_REG_FLTMASK(x) (0x1F4u + (0x08u * (x)))
#define MCP251XFD_REG_OSC 0xE00u
#define MCP251XFD_REG_IOCON 0xE04u
#define MCP251XFD_REG_CRC 0xE08u
#define MCP251XFD_REG_ECCCON 0xE0Cu
#define MCP251XFD_REG_ECCSTAT 0xE10u

#define MCP251XFD_SPI_RESET 0x0000u
#define MCP251XFD_SPI_WRITE 0x2000u
#define MCP251XFD_SPI_READ 0x3000u
#define MCP251XFD_SPI_ADDR_MASK 0x0FFFu

#define MCP251XFD_CON_REQOP_SHIFT 24u
#define MCP251XFD_CON_OPMOD_SHIFT 21u
#define MCP251XFD_CON_REQOP_MASK (0x07u << MCP251XFD_CON_REQOP_SHIFT)
#define MCP251XFD_CON_OPMOD_MASK (0x07u << MCP251XFD_CON_OPMOD_SHIFT)
#define MCP251XFD_CON_TXQEN (1u << 20)
#define MCP251XFD_CON_STEF (1u << 19)
#define MCP251XFD_CON_RTXAT (1u << 16)
#define MCP251XFD_CON_BRSDIS (1u << 12)
#define MCP251XFD_CON_ISOCRCEN (1u << 5)

#define MCP251XFD_MODE_MIXED 0u
#define MCP251XFD_MODE_SLEEP 1u
#define MCP251XFD_MODE_INT_LOOPBACK 2u
#define MCP251XFD_MODE_LISTENONLY 3u
#define MCP251XFD_MODE_CONFIG 4u
#define MCP251XFD_MODE_CAN2_0 6u

#define MCP251XFD_TREC_TXBO (1u << 21)
#define MCP251XFD_TREC_TXBP (1u << 20)
#define MCP251XFD_TREC_RXBP (1u << 19)
#define MCP251XFD_TREC_TXWARN (1u << 18)
#define MCP251XFD_TREC_RXWARN (1u << 17)
#define MCP251XFD_TREC_EWARN (1u << 16)

#define MCP251XFD_FIFO_PLSIZE_64 (7u << 29)
#define MCP251XFD_FIFO_FSIZE(n) (((uint32_t)(n) & 0x1Fu) << 24)
#define MCP251XFD_FIFO_TXAT_ONE_SHOT (0u << 21)
#define MCP251XFD_FIFO_TXAT_UNLIMITED (3u << 21)
#define MCP251XFD_FIFO_TXPRI(n) (((uint32_t)(n) & 0x1Fu) << 16)
#define MCP251XFD_FIFO_FRESET (1u << 10)
#define MCP251XFD_FIFO_TXREQ (1u << 9)
#define MCP251XFD_FIFO_UINC (1u << 8)
#define MCP251XFD_FIFO_TXEN (1u << 7)
#define MCP251XFD_FIFO_TFNRFNIF (1u << 0)

#define MCP251XFD_OBJ_FLAGS_ESI (1u << 8)
#define MCP251XFD_OBJ_FLAGS_FDF (1u << 7)
#define MCP251XFD_OBJ_FLAGS_BRS (1u << 6)
#define MCP251XFD_OBJ_FLAGS_RTR (1u << 5)
#define MCP251XFD_OBJ_FLAGS_IDE (1u << 4)

#define MCP251XFD_FLTOBJ_EXIDE (1u << 30)
#define MCP251XFD_FLTMASK_MIDE (1u << 30)
#define MCP251XFD_FLTCON_FLTEN (1u << 7)

static uint32_t le32_read(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void le32_write(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

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

static uint32_t encode_filter_id(const hal_can_filter_t *filter) {
  uint32_t id = filter->id;
  if ((filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u) {
    return MCP251XFD_FLTOBJ_EXIDE | ((id >> 18) & HAL_CAN_STD_ID_MASK) |
           ((id & 0x3FFFFu) << 11);
  }
  return id & HAL_CAN_STD_ID_MASK;
}

static uint32_t encode_filter_mask(const hal_can_filter_t *filter) {
  uint32_t mask = filter->mask;
  if ((filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u) {
    return MCP251XFD_FLTMASK_MIDE | ((mask >> 18) & HAL_CAN_STD_ID_MASK) |
           ((mask & 0x3FFFFu) << 11);
  }
  return MCP251XFD_FLTMASK_MIDE | (mask & HAL_CAN_STD_ID_MASK);
}

static uint32_t encode_timing(uint32_t osc_hz, uint32_t bitrate_hz,
                              uint8_t tq_target) {
  if (osc_hz == 0u || bitrate_hz == 0u || tq_target < 8u) {
    return 0u;
  }
  uint32_t brp = osc_hz / (bitrate_hz * (uint32_t)tq_target);
  if (brp == 0u) {
    brp = 1u;
  }
  uint32_t tq = osc_hz / (bitrate_hz * brp);
  if (tq < 8u) {
    tq = 8u;
  }
  if (tq > 49u) {
    tq = 49u;
  }
  const uint32_t tseg2 = tq / 5u > 1u ? tq / 5u : 2u;
  const uint32_t tseg1 = tq - 1u - tseg2;
  uint32_t sjw = tseg2;
  if (sjw > 16u) {
    sjw = 16u;
  }
  return ((brp - 1u) << 24) | ((tseg1 - 1u) << 16) | ((tseg2 - 1u) << 8) |
         (sjw - 1u);
}

JHMCP251XFD::JHMCP251XFD(uint8_t cs_pin, uint8_t spi_bus)
    : m_cs_pin(cs_pin), m_spi_bus(spi_bus), m_spi_clock_hz(10000000u),
      m_fd_enabled(false), m_one_shot(false),
      m_driver_mutex(hal_mutex_create()) {}

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
  hal_spi_transfer(m_spi_bus, (uint8_t)(MCP251XFD_SPI_RESET >> 8));
  hal_spi_transfer(m_spi_bus, (uint8_t)MCP251XFD_SPI_RESET);
  spi_end_();
  hal_delay_ms(5u);
}

uint32_t JHMCP251XFD::read_reg_(uint16_t addr) {
  uint8_t rx[4] = {};
  const uint16_t cmd =
      (uint16_t)(MCP251XFD_SPI_READ | (addr & MCP251XFD_SPI_ADDR_MASK));
  spi_begin_();
  hal_spi_transfer(m_spi_bus, (uint8_t)(cmd >> 8));
  hal_spi_transfer(m_spi_bus, (uint8_t)cmd);
  hal_spi_transfer_txrx(m_spi_bus, NULL, rx, sizeof(rx));
  spi_end_();
  return le32_read(rx);
}

void JHMCP251XFD::write_reg_(uint16_t addr, uint32_t value) {
  uint8_t tx[4];
  le32_write(tx, value);
  const uint16_t cmd =
      (uint16_t)(MCP251XFD_SPI_WRITE | (addr & MCP251XFD_SPI_ADDR_MASK));
  spi_begin_();
  hal_spi_transfer(m_spi_bus, (uint8_t)(cmd >> 8));
  hal_spi_transfer(m_spi_bus, (uint8_t)cmd);
  hal_spi_write(m_spi_bus, tx, sizeof(tx));
  spi_end_();
}

void JHMCP251XFD::write_reg_or_(uint16_t addr, uint32_t bits) {
  write_reg_(addr, read_reg_(addr) | bits);
}

void JHMCP251XFD::read_ram_(uint16_t addr, uint8_t *data, uint16_t len) {
  if (!data || len == 0u) {
    return;
  }
  const uint16_t cmd =
      (uint16_t)(MCP251XFD_SPI_READ | (addr & MCP251XFD_SPI_ADDR_MASK));
  spi_begin_();
  hal_spi_transfer(m_spi_bus, (uint8_t)(cmd >> 8));
  hal_spi_transfer(m_spi_bus, (uint8_t)cmd);
  hal_spi_transfer_txrx(m_spi_bus, NULL, data, len);
  spi_end_();
}

void JHMCP251XFD::write_ram_(uint16_t addr, const uint8_t *data, uint16_t len) {
  if (!data || len == 0u) {
    return;
  }
  const uint16_t cmd =
      (uint16_t)(MCP251XFD_SPI_WRITE | (addr & MCP251XFD_SPI_ADDR_MASK));
  spi_begin_();
  hal_spi_transfer(m_spi_bus, (uint8_t)(cmd >> 8));
  hal_spi_transfer(m_spi_bus, (uint8_t)cmd);
  hal_spi_write(m_spi_bus, data, len);
  spi_end_();
}

bool JHMCP251XFD::set_mode_raw_(uint8_t mode) {
  uint32_t con = read_reg_(MCP251XFD_REG_CON);
  con &= ~MCP251XFD_CON_REQOP_MASK;
  con |= ((uint32_t)mode << MCP251XFD_CON_REQOP_SHIFT);
  write_reg_(MCP251XFD_REG_CON, con);
  for (uint8_t i = 0; i < 100u; i++) {
    uint32_t now = read_reg_(MCP251XFD_REG_CON);
    if (((now & MCP251XFD_CON_OPMOD_MASK) >> MCP251XFD_CON_OPMOD_SHIFT) ==
        mode) {
      return true;
    }
    hal_delay_ms(1u);
  }
  return false;
}

bool JHMCP251XFD::configure_bit_timing_(uint32_t osc_hz,
                                        uint32_t arb_bitrate_hz,
                                        uint32_t data_bitrate_hz) {
  const uint32_t nbtcfg = encode_timing(osc_hz, arb_bitrate_hz, 20u);
  const uint32_t dbtcfg = encode_timing(
      osc_hz, data_bitrate_hz ? data_bitrate_hz : arb_bitrate_hz, 10u);
  if (nbtcfg == 0u || dbtcfg == 0u) {
    return false;
  }
  write_reg_(MCP251XFD_REG_NBTCFG, nbtcfg);
  write_reg_(MCP251XFD_REG_DBTCFG, dbtcfg);
  write_reg_(MCP251XFD_REG_TDC, m_fd_enabled ? (2u << 16) : 0u);
  return true;
}

bool JHMCP251XFD::configure_fifos_() {
  write_reg_(MCP251XFD_REG_TEFCON, 0u);
  const uint32_t tx_attempts =
      m_one_shot ? MCP251XFD_FIFO_TXAT_ONE_SHOT : MCP251XFD_FIFO_TXAT_UNLIMITED;
  write_reg_(MCP251XFD_REG_TXQCON,
             MCP251XFD_FIFO_PLSIZE_64 | MCP251XFD_FIFO_FSIZE(0u) | tx_attempts |
                 MCP251XFD_FIFO_TXPRI(1u) | MCP251XFD_FIFO_FRESET |
                 MCP251XFD_FIFO_TXEN);
  write_reg_(MCP251XFD_REG_FIFOCON(MCP251XFD_RX_FIFO_IDX),
             MCP251XFD_FIFO_PLSIZE_64 | MCP251XFD_FIFO_FSIZE(3u) |
                 MCP251XFD_FIFO_FRESET);
  return true;
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
  return (m_fd_enabled && ((mode & HAL_CAN_MODE_FD) != 0u))
             ? MCP251XFD_MODE_MIXED
             : MCP251XFD_MODE_CAN2_0;
}

bool JHMCP251XFD::validate_mode_(hal_can_mode_t mode) const {
  const hal_can_mode_t supported =
      HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_LISTEN_ONLY | HAL_CAN_MODE_ONE_SHOT |
      HAL_CAN_MODE_SLEEP | HAL_CAN_MODE_FD;
  if ((mode & ~supported) != 0u) {
    return false;
  }
  if ((mode & HAL_CAN_MODE_FD) != 0u && !m_fd_enabled) {
    return false;
  }
  uint8_t ops = 0u;
  ops += (mode & HAL_CAN_MODE_LOOPBACK) != 0u ? 1u : 0u;
  ops += (mode & HAL_CAN_MODE_LISTEN_ONLY) != 0u ? 1u : 0u;
  ops += (mode & HAL_CAN_MODE_SLEEP) != 0u ? 1u : 0u;
  return ops <= 1u;
}

bool JHMCP251XFD::begin(const hal_can_mcp251xfd_config_t *cfg) {
  if (!cfg || cfg->arbitration_bitrate_hz == 0u || cfg->oscillator_hz == 0u) {
    return false;
  }

  lock_driver_();
  m_spi_bus = cfg->spi_bus;
  m_cs_pin = cfg->cs_pin;
  m_spi_clock_hz = cfg->spi_clock_hz ? cfg->spi_clock_hz : 10000000u;
  m_fd_enabled = cfg->enable_fd;
  m_one_shot = cfg->one_shot_tx;

  hal_gpio_set_mode(m_cs_pin, HAL_GPIO_OUTPUT_HIGH);
  reset_();

  uint32_t osc = read_reg_(MCP251XFD_REG_OSC);
  if (osc == 0xFFFFFFFFu) {
    unlock_driver_();
    return false;
  }

  if (!set_mode_raw_(MCP251XFD_MODE_CONFIG)) {
    unlock_driver_();
    return false;
  }
  write_reg_(MCP251XFD_REG_CRC, 0u);
  write_reg_(MCP251XFD_REG_ECCCON, 0u);
  write_reg_(MCP251XFD_REG_ECCSTAT, 0u);
  write_reg_(MCP251XFD_REG_IOCON, cfg->sleep_wakeup ? 0u : (1u << 3));
  write_reg_(MCP251XFD_REG_INT, 0u);

  bool ok =
      configure_bit_timing_(cfg->oscillator_hz, cfg->arbitration_bitrate_hz,
                            cfg->data_bitrate_hz) &&
      configure_fifos_();
  uint32_t con = read_reg_(MCP251XFD_REG_CON);
  con |= MCP251XFD_CON_TXQEN | MCP251XFD_CON_ISOCRCEN;
  if (!m_fd_enabled) {
    con |= MCP251XFD_CON_BRSDIS;
  }
  if (m_one_shot) {
    con |= MCP251XFD_CON_RTXAT;
  }
  write_reg_(MCP251XFD_REG_CON, con);
  ok = ok && set_mode_raw_(m_fd_enabled ? MCP251XFD_MODE_MIXED
                                        : MCP251XFD_MODE_CAN2_0);
  unlock_driver_();
  return ok;
}

void JHMCP251XFD::end() {
  if (m_driver_mutex) {
    hal_mutex_lock(m_driver_mutex);
    (void)set_mode_raw_(MCP251XFD_MODE_CONFIG);
    hal_mutex_unlock(m_driver_mutex);
    hal_mutex_destroy(m_driver_mutex);
    m_driver_mutex = NULL;
  }
}

bool JHMCP251XFD::send_frame(const hal_can_frame_t *frame) {
  if (!hal_can_validate_frame(frame)) {
    return false;
  }
  if ((frame->flags & HAL_CAN_FRAME_FD) != 0u && !m_fd_enabled) {
    hal_derr_limited("can", "MCP251XFD CAN FD frame sent while FD disabled");
    return false;
  }

  uint8_t obj[72] = {};
  le32_write(&obj[0], encode_id(frame->id, frame->flags));
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
  le32_write(&obj[4], flags);
  if ((frame->flags & HAL_CAN_FRAME_RTR) == 0u && frame->len > 0u) {
    memcpy(&obj[8], frame->data, frame->len);
  }

  lock_driver_();
  uint32_t sta = read_reg_(MCP251XFD_REG_TXQSTA);
  if ((sta & MCP251XFD_FIFO_TFNRFNIF) == 0u) {
    unlock_driver_();
    return false;
  }
  uint16_t ua =
      (uint16_t)(read_reg_(MCP251XFD_REG_TXQUA) & MCP251XFD_SPI_ADDR_MASK);
  if (ua == 0u) {
    ua = MCP251XFD_TXQ_START_ADDR;
  }
  write_ram_(ua, obj, (uint16_t)(8u + hal_can_dlc_to_bytes(frame->dlc)));
  write_reg_or_(MCP251XFD_REG_TXQCON,
                MCP251XFD_FIFO_UINC | MCP251XFD_FIFO_TXREQ);
  unlock_driver_();
  return true;
}

bool JHMCP251XFD::available() {
  lock_driver_();
  uint32_t sta = read_reg_(MCP251XFD_REG_FIFOSTA(MCP251XFD_RX_FIFO_IDX));
  unlock_driver_();
  return (sta & MCP251XFD_FIFO_TFNRFNIF) != 0u;
}

bool JHMCP251XFD::receive_frame(hal_can_frame_t *frame) {
  if (!frame) {
    return false;
  }
  lock_driver_();
  uint32_t sta = read_reg_(MCP251XFD_REG_FIFOSTA(MCP251XFD_RX_FIFO_IDX));
  if ((sta & MCP251XFD_FIFO_TFNRFNIF) == 0u) {
    unlock_driver_();
    return false;
  }
  uint16_t ua =
      (uint16_t)(read_reg_(MCP251XFD_REG_FIFOUA(MCP251XFD_RX_FIFO_IDX)) &
                 MCP251XFD_SPI_ADDR_MASK);
  if (ua == 0u) {
    ua = MCP251XFD_RX_FIFO_START_ADDR;
  }
  uint8_t obj[72] = {};
  read_ram_(ua, obj, sizeof(obj));
  write_reg_or_(MCP251XFD_REG_FIFOCON(MCP251XFD_RX_FIFO_IDX),
                MCP251XFD_FIFO_UINC);
  unlock_driver_();

  memset(frame, 0, sizeof(*frame));
  const uint32_t raw_id = le32_read(&obj[0]);
  const uint32_t raw_flags = le32_read(&obj[4]);
  frame->flags = 0u;
  frame->id = decode_id(raw_id, raw_flags, &frame->flags);
  if ((raw_flags & MCP251XFD_OBJ_FLAGS_RTR) != 0u)
    frame->flags |= HAL_CAN_FRAME_RTR;
  if ((raw_flags & MCP251XFD_OBJ_FLAGS_FDF) != 0u)
    frame->flags |= HAL_CAN_FRAME_FD;
  if ((raw_flags & MCP251XFD_OBJ_FLAGS_BRS) != 0u)
    frame->flags |= HAL_CAN_FRAME_BRS;
  if ((raw_flags & MCP251XFD_OBJ_FLAGS_ESI) != 0u)
    frame->flags |= HAL_CAN_FRAME_ESI;
  frame->dlc = (uint8_t)(raw_flags & 0x0Fu);
  frame->len = hal_can_dlc_to_bytes(frame->dlc);
  if (frame->len > HAL_CAN_FD_MAX_DATA_LEN) {
    frame->len = HAL_CAN_FD_MAX_DATA_LEN;
  }
  if ((frame->flags & HAL_CAN_FRAME_RTR) == 0u && frame->len > 0u) {
    memcpy(frame->data, &obj[8], frame->len);
  }
  return hal_can_validate_frame(frame);
}

bool JHMCP251XFD::start(hal_can_mode_t mode) { return set_mode(mode); }

bool JHMCP251XFD::stop() {
  lock_driver_();
  bool ok = set_mode_raw_(MCP251XFD_MODE_CONFIG);
  unlock_driver_();
  return ok;
}

bool JHMCP251XFD::set_mode(hal_can_mode_t mode) {
  if (!validate_mode_(mode)) {
    return false;
  }
  lock_driver_();
  bool ok = set_mode_raw_(op_mode_for_hal_(mode));
  unlock_driver_();
  return ok;
}

bool JHMCP251XFD::get_state(bool started, hal_can_state_t *state) {
  if (!state) {
    return false;
  }
  if (!started) {
    *state = HAL_CAN_STATE_STOPPED;
    return true;
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
  return true;
}

bool JHMCP251XFD::get_error_counters(hal_can_error_counters_t *counters) {
  if (!counters) {
    return false;
  }
  lock_driver_();
  uint32_t trec = read_reg_(MCP251XFD_REG_TREC);
  unlock_driver_();
  counters->rx = (uint8_t)(trec & 0xFFu);
  counters->tx = (uint8_t)((trec >> 8) & 0xFFu);
  return true;
}

bool JHMCP251XFD::set_filter(uint8_t index, const hal_can_filter_t *filter) {
  if (index >= HAL_CAN_MAX_FILTERS || !hal_can_validate_filter(filter)) {
    return false;
  }
  lock_driver_();
  write_reg_(MCP251XFD_REG_FLTOBJ(index), encode_filter_id(filter));
  write_reg_(MCP251XFD_REG_FLTMASK(index), encode_filter_mask(filter));
  const uint8_t byte_index = index / 4u;
  const uint8_t shift = (uint8_t)((index % 4u) * 8u);
  uint32_t fltcon = read_reg_(MCP251XFD_REG_FLTCON(byte_index));
  fltcon &= ~((uint32_t)0xFFu << shift);
  fltcon |= (uint32_t)(MCP251XFD_FLTCON_FLTEN | MCP251XFD_RX_FIFO_IDX) << shift;
  write_reg_(MCP251XFD_REG_FLTCON(byte_index), fltcon);
  unlock_driver_();
  return true;
}

#endif /* HAL_ENABLE_MCP251XFD && HAL_ENABLE_SPI */
