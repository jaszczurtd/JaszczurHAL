#include "hal/core/hal_memory.h"
#include "hal/core/hal_target.h"

#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_MCP2515) && defined(HAL_ENABLE_SPI)

#include "mcp2515_driver.h"

#include "hal/gpio/hal_gpio.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"
#include <string.h>

static const hal_spi_settings_t kMcp2515SpiSettings = {
    10000000u,
    HAL_SPI_MSBFIRST,
    HAL_SPI_MODE0,
};

class JHMCP2515Guard {
public:
  explicit JHMCP2515Guard(JHMCP2515 &driver, bool spi = true) : d(driver) {
    d.lock_driver_(spi);
  }
  ~JHMCP2515Guard() { d.unlock_driver_(); }

private:
  JHMCP2515 &d;
};

void HAL_RAM_FUNC(JHMCP2515::lock_driver_)(bool spi) {
  hal_mutex_lock(m_driver_mutex);
  if (spi) {
    lock_spi_();
  }
}
void HAL_RAM_FUNC(JHMCP2515::lock_spi_)(void) {
  // Keep register setup, RX reads and TX completion polls together. A
  // display between these instructions can fill both receive buffers.
  hal_spi_lock(m_spiBus);
  hal_spi_begin_transaction(m_spiBus, &kMcp2515SpiSettings);
  m_spiLease = true;
}
void HAL_RAM_FUNC(JHMCP2515::unlock_driver_)(void) {
  if (m_spiLease) {
    m_spiLease = false;
    hal_spi_end_transaction(m_spiBus);
    hal_spi_unlock(m_spiBus);
  }
  hal_mutex_unlock(m_driver_mutex);
}

void HAL_RAM_FUNC(JHMCP2515::spi_begin_)(void) {
  if (!m_spiLease) {
    hal_spi_lock(m_spiBus);
    hal_spi_begin_transaction(m_spiBus, &kMcp2515SpiSettings);
  }
  hal_gpio_write(m_csPin, false);
}

void HAL_RAM_FUNC(JHMCP2515::spi_end_)(void) {
  hal_gpio_write(m_csPin, true);
  if (m_spiLease) {
    // The next instruction can follow without ending the bus transaction.
    // Preserve a CS-high interval between the two commands.
    hal_delay_us(1u);
  }
  if (!m_spiLease) {
    hal_spi_end_transaction(m_spiBus);
    hal_spi_unlock(m_spiBus);
  }
}

void JHMCP2515::mcp2515_reset(void) {
  spi_begin_();
  hal_spi_transfer(m_spiBus, MCP_RESET);
  spi_end_();
  hal_delay_ms(5);
}

INT8U HAL_RAM_FUNC(JHMCP2515::mcp2515_readRegister)(const INT8U address) {
  INT8U bytes[] = {MCP_READ, address, 0u};
  spi_begin_();
  (void)hal_spi_transfer_buffer(m_spiBus, bytes, sizeof(bytes));
  spi_end_();
  return bytes[2];
}

void HAL_RAM_FUNC(JHMCP2515::mcp2515_readRegisterS)(const INT8U address,
                                                    INT8U values[],
                                                    const INT8U n) {
  const INT8U command[] = {MCP_READ, address};
  spi_begin_();
  (void)hal_spi_write(m_spiBus, command, sizeof(command));
  (void)hal_spi_transfer_txrx(m_spiBus, NULL, values, n);
  spi_end_();
}

void HAL_RAM_FUNC(JHMCP2515::mcp2515_setRegister)(const INT8U address,
                                                  const INT8U value) {
  const INT8U bytes[] = {MCP_WRITE, address, value};
  spi_begin_();
  (void)hal_spi_write(m_spiBus, bytes, sizeof(bytes));
  spi_end_();
}

void HAL_RAM_FUNC(JHMCP2515::mcp2515_setRegisterS)(const INT8U address,
                                                   const INT8U values[],
                                                   const INT8U n) {
  const INT8U command[] = {MCP_WRITE, address};
  spi_begin_();
  (void)hal_spi_write(m_spiBus, command, sizeof(command));
  (void)hal_spi_write(m_spiBus, values, n);
  spi_end_();
}

void HAL_RAM_FUNC(JHMCP2515::mcp2515_modifyRegister)(const INT8U address,
                                                     const INT8U mask,
                                                     const INT8U data) {
  const INT8U bytes[] = {MCP_BITMOD, address, mask, data};
  spi_begin_();
  (void)hal_spi_write(m_spiBus, bytes, sizeof(bytes));
  spi_end_();
}

INT8U HAL_RAM_FUNC(JHMCP2515::mcp2515_readStatus)(void) {
  INT8U bytes[] = {MCP_READ_STATUS, 0u};
  spi_begin_();
  (void)hal_spi_transfer_buffer(m_spiBus, bytes, sizeof(bytes));
  spi_end_();
  return bytes[1];
}

void JHMCP2515::setSleepWakeup(const INT8U enable) {
  JHMCP2515Guard guard(*this);
  mcp2515_modifyRegister(MCP_CANINTE, MCP_WAKIF, enable ? MCP_WAKIF : 0u);
}

INT8U JHMCP2515::setMode(const INT8U opMode) {
  JHMCP2515Guard guard(*this);
  const INT8U result = mcp2515_setCANCTRL_Mode(opMode);
  if (result == MCP2515_OK) {
    mcpMode = opMode;
  }
  return result;
}

INT8U JHMCP2515::mcp2515_setCANCTRL_Mode(const INT8U newmode) {
  if ((mcp2515_readRegister(MCP_CANSTAT) & MODE_MASK) == MCP_SLEEP &&
      newmode != MCP_SLEEP) {
    INT8U wakeIntEnabled =
        (INT8U)(mcp2515_readRegister(MCP_CANINTE) & MCP_WAKIF);
    if (!wakeIntEnabled) {
      mcp2515_modifyRegister(MCP_CANINTE, MCP_WAKIF, MCP_WAKIF);
    }
    mcp2515_modifyRegister(MCP_CANINTF, MCP_WAKIF, MCP_WAKIF);
    if (mcp2515_requestNewMode(MCP_LISTENONLY) != MCP2515_OK) {
      return MCP2515_FAIL;
    }
    if (!wakeIntEnabled) {
      mcp2515_modifyRegister(MCP_CANINTE, MCP_WAKIF, 0u);
    }
  }
  mcp2515_modifyRegister(MCP_CANINTF, MCP_WAKIF, 0u);
  return mcp2515_requestNewMode(newmode);
}

INT8U JHMCP2515::mcp2515_requestNewMode(const INT8U newmode) {
  const INT8U startTime = (INT8U)hal_millis();
  while (1) {
    mcp2515_modifyRegister(MCP_CANCTRL, MODE_MASK, newmode);
    INT8U statReg = mcp2515_readRegister(MCP_CANSTAT);
    if ((statReg & MODE_MASK) == newmode) {
      return MCP2515_OK;
    }
    if ((INT8U)(hal_millis() - startTime) > 200u) {
      return MCP2515_FAIL;
    }
  }
}

INT8U JHMCP2515::mcp2515_configRate(const INT8U canSpeed,
                                    const INT8U canClock) {
  INT8U cfg1 = 0, cfg2 = 0, cfg3 = 0;
  switch (canClock & MCP_CLOCK_SELECT) {
  case MCP_8MHZ:
    switch (canSpeed) {
    case CAN_5KBPS:
      cfg1 = MCP_8MHz_5kBPS_CFG1;
      cfg2 = MCP_8MHz_5kBPS_CFG2;
      cfg3 = MCP_8MHz_5kBPS_CFG3;
      break;
    case CAN_10KBPS:
      cfg1 = MCP_8MHz_10kBPS_CFG1;
      cfg2 = MCP_8MHz_10kBPS_CFG2;
      cfg3 = MCP_8MHz_10kBPS_CFG3;
      break;
    case CAN_20KBPS:
      cfg1 = MCP_8MHz_20kBPS_CFG1;
      cfg2 = MCP_8MHz_20kBPS_CFG2;
      cfg3 = MCP_8MHz_20kBPS_CFG3;
      break;
    case CAN_31K25BPS:
      cfg1 = MCP_8MHz_31k25BPS_CFG1;
      cfg2 = MCP_8MHz_31k25BPS_CFG2;
      cfg3 = MCP_8MHz_31k25BPS_CFG3;
      break;
    case CAN_33K3BPS:
      cfg1 = MCP_8MHz_33k3BPS_CFG1;
      cfg2 = MCP_8MHz_33k3BPS_CFG2;
      cfg3 = MCP_8MHz_33k3BPS_CFG3;
      break;
    case CAN_40KBPS:
      cfg1 = MCP_8MHz_40kBPS_CFG1;
      cfg2 = MCP_8MHz_40kBPS_CFG2;
      cfg3 = MCP_8MHz_40kBPS_CFG3;
      break;
    case CAN_50KBPS:
      cfg1 = MCP_8MHz_50kBPS_CFG1;
      cfg2 = MCP_8MHz_50kBPS_CFG2;
      cfg3 = MCP_8MHz_50kBPS_CFG3;
      break;
    case CAN_80KBPS:
      cfg1 = MCP_8MHz_80kBPS_CFG1;
      cfg2 = MCP_8MHz_80kBPS_CFG2;
      cfg3 = MCP_8MHz_80kBPS_CFG3;
      break;
    case CAN_100KBPS:
      cfg1 = MCP_8MHz_100kBPS_CFG1;
      cfg2 = MCP_8MHz_100kBPS_CFG2;
      cfg3 = MCP_8MHz_100kBPS_CFG3;
      break;
    case CAN_125KBPS:
      cfg1 = MCP_8MHz_125kBPS_CFG1;
      cfg2 = MCP_8MHz_125kBPS_CFG2;
      cfg3 = MCP_8MHz_125kBPS_CFG3;
      break;
    case CAN_200KBPS:
      cfg1 = MCP_8MHz_200kBPS_CFG1;
      cfg2 = MCP_8MHz_200kBPS_CFG2;
      cfg3 = MCP_8MHz_200kBPS_CFG3;
      break;
    case CAN_250KBPS:
      cfg1 = MCP_8MHz_250kBPS_CFG1;
      cfg2 = MCP_8MHz_250kBPS_CFG2;
      cfg3 = MCP_8MHz_250kBPS_CFG3;
      break;
    case CAN_500KBPS:
      cfg1 = MCP_8MHz_500kBPS_CFG1;
      cfg2 = MCP_8MHz_500kBPS_CFG2;
      cfg3 = MCP_8MHz_500kBPS_CFG3;
      break;
    case CAN_1000KBPS:
      cfg1 = MCP_8MHz_1000kBPS_CFG1;
      cfg2 = MCP_8MHz_1000kBPS_CFG2;
      cfg3 = MCP_8MHz_1000kBPS_CFG3;
      break;
    default:
      return MCP2515_FAIL;
    }
    break;
  case MCP_16MHZ:
    switch (canSpeed) {
    case CAN_5KBPS:
      cfg1 = MCP_16MHz_5kBPS_CFG1;
      cfg2 = MCP_16MHz_5kBPS_CFG2;
      cfg3 = MCP_16MHz_5kBPS_CFG3;
      break;
    case CAN_10KBPS:
      cfg1 = MCP_16MHz_10kBPS_CFG1;
      cfg2 = MCP_16MHz_10kBPS_CFG2;
      cfg3 = MCP_16MHz_10kBPS_CFG3;
      break;
    case CAN_20KBPS:
      cfg1 = MCP_16MHz_20kBPS_CFG1;
      cfg2 = MCP_16MHz_20kBPS_CFG2;
      cfg3 = MCP_16MHz_20kBPS_CFG3;
      break;
    case CAN_33K3BPS:
      cfg1 = MCP_16MHz_33k3BPS_CFG1;
      cfg2 = MCP_16MHz_33k3BPS_CFG2;
      cfg3 = MCP_16MHz_33k3BPS_CFG3;
      break;
    case CAN_40KBPS:
      cfg1 = MCP_16MHz_40kBPS_CFG1;
      cfg2 = MCP_16MHz_40kBPS_CFG2;
      cfg3 = MCP_16MHz_40kBPS_CFG3;
      break;
    case CAN_50KBPS:
      cfg2 = MCP_16MHz_50kBPS_CFG2;
      cfg3 = MCP_16MHz_50kBPS_CFG3;
      break;
    case CAN_80KBPS:
      cfg1 = MCP_16MHz_80kBPS_CFG1;
      cfg2 = MCP_16MHz_80kBPS_CFG2;
      cfg3 = MCP_16MHz_80kBPS_CFG3;
      break;
    case CAN_100KBPS:
      cfg1 = MCP_16MHz_100kBPS_CFG1;
      cfg2 = MCP_16MHz_100kBPS_CFG2;
      cfg3 = MCP_16MHz_100kBPS_CFG3;
      break;
    case CAN_125KBPS:
      cfg1 = MCP_16MHz_125kBPS_CFG1;
      cfg2 = MCP_16MHz_125kBPS_CFG2;
      cfg3 = MCP_16MHz_125kBPS_CFG3;
      break;
    case CAN_200KBPS:
      cfg1 = MCP_16MHz_200kBPS_CFG1;
      cfg2 = MCP_16MHz_200kBPS_CFG2;
      cfg3 = MCP_16MHz_200kBPS_CFG3;
      break;
    case CAN_250KBPS:
      cfg1 = MCP_16MHz_250kBPS_CFG1;
      cfg2 = MCP_16MHz_250kBPS_CFG2;
      cfg3 = MCP_16MHz_250kBPS_CFG3;
      break;
    case CAN_500KBPS:
      cfg1 = MCP_16MHz_500kBPS_CFG1;
      cfg2 = MCP_16MHz_500kBPS_CFG2;
      cfg3 = MCP_16MHz_500kBPS_CFG3;
      break;
    case CAN_1000KBPS:
      cfg1 = MCP_16MHz_1000kBPS_CFG1;
      cfg2 = MCP_16MHz_1000kBPS_CFG2;
      cfg3 = MCP_16MHz_1000kBPS_CFG3;
      break;
    default:
      return MCP2515_FAIL;
    }
    break;
  case MCP_20MHZ:
    switch (canSpeed) {
    case CAN_40KBPS:
      cfg1 = MCP_20MHz_40kBPS_CFG1;
      cfg2 = MCP_20MHz_40kBPS_CFG2;
      cfg3 = MCP_20MHz_40kBPS_CFG3;
      break;
    case CAN_50KBPS:
      cfg1 = MCP_20MHz_50kBPS_CFG1;
      cfg2 = MCP_20MHz_50kBPS_CFG2;
      cfg3 = MCP_20MHz_50kBPS_CFG3;
      break;
    case CAN_80KBPS:
      cfg1 = MCP_20MHz_80kBPS_CFG1;
      cfg2 = MCP_20MHz_80kBPS_CFG2;
      cfg3 = MCP_20MHz_80kBPS_CFG3;
      break;
    case CAN_100KBPS:
      cfg1 = MCP_20MHz_100kBPS_CFG1;
      cfg2 = MCP_20MHz_100kBPS_CFG2;
      cfg3 = MCP_20MHz_100kBPS_CFG3;
      break;
    case CAN_125KBPS:
      cfg1 = MCP_20MHz_125kBPS_CFG1;
      cfg2 = MCP_20MHz_125kBPS_CFG2;
      cfg3 = MCP_20MHz_125kBPS_CFG3;
      break;
    case CAN_200KBPS:
      cfg1 = MCP_20MHz_200kBPS_CFG1;
      cfg2 = MCP_20MHz_200kBPS_CFG2;
      cfg3 = MCP_20MHz_200kBPS_CFG3;
      break;
    case CAN_250KBPS:
      cfg1 = MCP_20MHz_250kBPS_CFG1;
      cfg2 = MCP_20MHz_250kBPS_CFG2;
      cfg3 = MCP_20MHz_250kBPS_CFG3;
      break;
    case CAN_500KBPS:
      cfg1 = MCP_20MHz_500kBPS_CFG1;
      cfg2 = MCP_20MHz_500kBPS_CFG2;
      cfg3 = MCP_20MHz_500kBPS_CFG3;
      break;
    case CAN_1000KBPS:
      cfg1 = MCP_20MHz_1000kBPS_CFG1;
      cfg2 = MCP_20MHz_1000kBPS_CFG2;
      cfg3 = MCP_20MHz_1000kBPS_CFG3;
      break;
    default:
      return MCP2515_FAIL;
    }
    break;
  default:
    return MCP2515_FAIL;
  }
  if (canClock & MCP_CLKOUT_ENABLE) {
    cfg3 = (INT8U)(cfg3 & ~0x80u);
  }
  mcp2515_setRegister(MCP_CNF1, cfg1);
  mcp2515_setRegister(MCP_CNF2, cfg2);
  mcp2515_setRegister(MCP_CNF3, cfg3);
  return MCP2515_OK;
}

void JHMCP2515::mcp2515_initCANBuffers(void) {
  INT8U std = 0, ext = 1;
  INT32U ulMask = 0, ulFilt = 0;
  mcp2515_write_mf(MCP_RXM0SIDH, ext, ulMask);
  mcp2515_write_mf(MCP_RXM1SIDH, ext, ulMask);
  mcp2515_write_mf(MCP_RXF0SIDH, ext, ulFilt);
  mcp2515_write_mf(MCP_RXF1SIDH, std, ulFilt);
  mcp2515_write_mf(MCP_RXF2SIDH, ext, ulFilt);
  mcp2515_write_mf(MCP_RXF3SIDH, std, ulFilt);
  mcp2515_write_mf(MCP_RXF4SIDH, ext, ulFilt);
  mcp2515_write_mf(MCP_RXF5SIDH, std, ulFilt);
  INT8U a1 = MCP_TXB0CTRL, a2 = MCP_TXB1CTRL, a3 = MCP_TXB2CTRL;
  for (INT8U i = 0; i < 14; ++i) {
    mcp2515_setRegister(a1++, 0);
    mcp2515_setRegister(a2++, 0);
    mcp2515_setRegister(a3++, 0);
  }
  mcp2515_setRegister(MCP_RXB0CTRL, 0);
  mcp2515_setRegister(MCP_RXB1CTRL, 0);
}

INT8U JHMCP2515::mcp2515_init(const INT8U canIDMode, const INT8U canSpeed,
                              const INT8U canClock) {
  INT8U res;
  mcp2515_reset();
  mcpMode = MCP_LOOPBACK;
  res = mcp2515_setCANCTRL_Mode(MODE_CONFIG);
  if (res > 0) {
#if DEBUG_MODE
    hal_derr("%s", "Entering Configuration Mode Failure...");
#endif
    return res;
  }
#if DEBUG_MODE
  hal_deb("%s", "Entering Configuration Mode Successful!");
#endif
  res = mcp2515_configRate(canSpeed, canClock);
  if (res != MCP2515_OK) {
#if DEBUG_MODE
    hal_derr("%s", "Setting Baudrate Failure...");
#endif
    return res;
  }
#if DEBUG_MODE
  hal_deb("%s", "Setting Baudrate Successful!");
#endif
  if (res == MCP2515_OK) {
    mcp2515_initCANBuffers();
    mcp2515_setRegister(MCP_CANINTE, MCP_RX0IF | MCP_RX1IF);
    mcp2515_setRegister(MCP_BFPCTRL, MCP_BxBFS_MASK | MCP_BxBFE_MASK);
    mcp2515_setRegister(MCP_TXRTSCTRL, 0x00);
    switch (canIDMode) {
    case MCP_ANY:
      mcp2515_modifyRegister(MCP_RXB0CTRL, MCP_RXB_RX_MASK | MCP_RXB_BUKT_MASK,
                             MCP_RXB_RX_ANY | MCP_RXB_BUKT_MASK);
      mcp2515_modifyRegister(MCP_RXB1CTRL, MCP_RXB_RX_MASK, MCP_RXB_RX_ANY);
      break;
    case MCP_STDEXT:
      mcp2515_modifyRegister(MCP_RXB0CTRL, MCP_RXB_RX_MASK | MCP_RXB_BUKT_MASK,
                             MCP_RXB_RX_STDEXT | MCP_RXB_BUKT_MASK);
      mcp2515_modifyRegister(MCP_RXB1CTRL, MCP_RXB_RX_MASK, MCP_RXB_RX_STDEXT);
      break;
    default:
#if DEBUG_MODE
      hal_derr("%s", "Setting ID Mode Failure...");
#endif
      return MCP2515_FAIL;
    }
    res = mcp2515_setCANCTRL_Mode(mcpMode);
    if (res) {
#if DEBUG_MODE
      hal_derr("%s", "Returning to Previous Mode Failure...");
#endif
      return res;
    }
  }
  return res;
}

static void mcp2515_encode_extended_id(INT8U *buffer, const INT32U id) {
  uint16_t canid = (uint16_t)(id & 0x0FFFFu);
  buffer[MCP_EID0] = (INT8U)(canid & 0xFFu);
  buffer[MCP_EID8] = (INT8U)(canid >> 8);
  canid = (uint16_t)(id >> 16);
  buffer[MCP_SIDL] = (INT8U)(canid & 0x03u);
  buffer[MCP_SIDL] += (INT8U)((canid & 0x1Cu) << 3);
  buffer[MCP_SIDL] |= MCP_TXB_EXIDE_M;
  buffer[MCP_SIDH] = (INT8U)(canid >> 5);
}

void JHMCP2515::mcp2515_write_id(const INT8U mcp_addr, const INT8U ext,
                                 const INT32U id) {
  INT8U tbufdata[4];
  if (ext == 1u) {
    mcp2515_encode_extended_id(tbufdata, id);
  } else {
    const uint16_t canid = (uint16_t)(id & 0x0FFFFu);
    tbufdata[MCP_SIDH] = (INT8U)(canid >> 3);
    tbufdata[MCP_SIDL] = (INT8U)((canid & 0x07u) << 5);
    tbufdata[MCP_EID0] = 0;
    tbufdata[MCP_EID8] = 0;
  }
  mcp2515_setRegisterS(mcp_addr, tbufdata, 4);
}

void JHMCP2515::mcp2515_write_mf(const INT8U mcp_addr, const INT8U ext,
                                 const INT32U id) {
  INT8U tbufdata[4];
  if (ext == 1u) {
    mcp2515_encode_extended_id(tbufdata, id);
  } else {
    uint16_t canid = (uint16_t)(id & 0x0FFFFu);
    tbufdata[MCP_EID0] = (INT8U)(canid & 0xFFu);
    tbufdata[MCP_EID8] = (INT8U)(canid >> 8);
    canid = (uint16_t)(id >> 16);
    tbufdata[MCP_SIDL] = (INT8U)((canid & 0x07u) << 5);
    tbufdata[MCP_SIDH] = (INT8U)(canid >> 3);
  }
  mcp2515_setRegisterS(mcp_addr, tbufdata, 4);
}

/* Identifier from the SIDH, SIDL, EID8 and EID0 register bytes. */
static void decode_id(const INT8U tbufdata[4], INT8U *ext, INT32U *id) {
  *ext = 0;
  *id = ((INT32U)tbufdata[MCP_SIDH] << 3) + (tbufdata[MCP_SIDL] >> 5);
  if ((tbufdata[MCP_SIDL] & MCP_TXB_EXIDE_M) == MCP_TXB_EXIDE_M) {
    *id = (*id << 2) + (tbufdata[MCP_SIDL] & 0x03u);
    *id = (*id << 8) + tbufdata[MCP_EID8];
    *id = (*id << 8) + tbufdata[MCP_EID0];
    *ext = 1;
  }
}

void JHMCP2515::mcp2515_read_id(const INT8U mcp_addr, INT8U *ext, INT32U *id) {
  INT8U tbufdata[4];
  mcp2515_readRegisterS(mcp_addr, tbufdata, 4);
  decode_id(tbufdata, ext, id);
}

void HAL_RAM_FUNC(JHMCP2515::mcp2515_write_canMsg)(
    const INT8U buffer_sidh_addr) {
  INT8U mcp_addr = buffer_sidh_addr;
  mcp2515_setRegisterS((INT8U)(mcp_addr + 5), m_nDta, m_nDlc);
  if (m_nRtr == 1) {
    m_nDlc |= MCP_RTR_MASK;
  }
  mcp2515_setRegister((INT8U)(mcp_addr + 4), m_nDlc);
  mcp2515_write_id(mcp_addr, m_nExtFlg, m_nID);
}

/* Read the oldest receive buffer that *status reports as full and replace
 * *status with a READ STATUS taken right after that buffer was freed. With
 * rollover a frame lands in RXB1 only while RXB0 is full, so RXB1 is older
 * than the next RXB0 frame when it holds a frame right after RXB0 is freed;
 * it may have filled during the read itself. Interrupts stay off and the bus
 * stays taken from the release until that status answers, too short for one
 * frame to reach the freed RXB0 and a newer one RXB1 (a CAN frame lasts at
 * least 47 bit times). This order needs RXB1 to fill by rollover only: a
 * frame that only its own filters (slots 2..5) accept lands there at once
 * and may come out after a newer RXB0 frame, since the chip does not time
 * its buffers. READ RX BUFFER sends SIDH, SIDL, EID8, EID0, DLC and
 * the data in one transaction, and raising CS clears that buffer's RXnIF.
 * RTR comes from SIDL.SRR for a standard frame and from DLC.RTR for an
 * extended one. */
bool HAL_RAM_FUNC(JHMCP2515::mcp2515_read_rx)(INT8U *status,
                                              jh_mcp2515_rx_frame_t *frame) {
  const bool rx0 = (*status & MCP_STAT_RX0IF) != 0u;
  const bool rx1 = (*status & MCP_STAT_RX1IF) != 0u;
  INT8U instruction = 0u;
  if (rx1 && (!rx0 || m_rxb1Older)) {
    instruction = MCP_READ_RX1;
  } else if (rx0) {
    instruction = MCP_READ_RX0;
  } else {
    return false;
  }

  // One FIFO transfer avoids the per-byte HAL overhead while RX buffers fill.
  // READ RX BUFFER permits clocking the full eight data registers for any DLC.
  INT8U bytes[1u + 5u + MAX_CHAR_IN_MESSAGE] = {};
  bytes[0] = instruction;
  spi_begin_();
  (void)hal_spi_transfer_buffer(m_spiBus, bytes, sizeof(bytes));
  const INT8U *header = &bytes[1];
  INT8U dlc = (INT8U)(header[4] & MCP_DLC_MASK);
  if (dlc > MAX_CHAR_IN_MESSAGE) {
    dlc = MAX_CHAR_IN_MESSAGE;
  }
  memcpy(frame->data, &bytes[6], dlc);
  hal_critical_section_enter();
  hal_gpio_write(m_csPin, true);
  hal_delay_us(1u);
  hal_gpio_write(m_csPin, false);
  INT8U statusBytes[] = {MCP_READ_STATUS, 0u};
  (void)hal_spi_transfer_buffer(m_spiBus, statusBytes, sizeof(statusBytes));
  *status = statusBytes[1];
  hal_critical_section_exit();
  spi_end_();
  m_rxb1Older =
      (instruction == MCP_READ_RX0) && ((*status & MCP_STAT_RX1IF) != 0u);
  frame->dlc = dlc;

  decode_id(header, &frame->ext, &frame->id);
  frame->rtr = (frame->ext != 0u) ? (INT8U)((header[4] & MCP_RTR_MASK) != 0u)
                                  : (INT8U)((header[MCP_SIDL] & 0x10u) != 0u);
  return true;
}

/* Move one waiting frame into the ring while a transmit occupies the caller;
 * a full ring leaves it in the hardware buffer. True when a frame moved, and
 * *status is then the fresh status that followed it. */
bool HAL_RAM_FUNC(JHMCP2515::mcp2515_keep_rx)(INT8U *status) {
  if (m_rxRingCount >= MCP2515_RX_RING_FRAMES) {
    return false;
  }
  const INT8U slot =
      (INT8U)((m_rxRingHead + m_rxRingCount) % MCP2515_RX_RING_FRAMES);
  if (!mcp2515_read_rx(status, &m_rxRing[slot])) {
    return false;
  }
  m_rxRingCount++;
  return true;
}

void HAL_RAM_FUNC(JHMCP2515::mcp2515_keep_pending)(void) {
  INT8U status = mcp2515_readStatus();
  while (mcp2515_keep_rx(&status)) {
  }
}

void HAL_RAM_FUNC(JHMCP2515::mcp2515_load_rx)(
    const jh_mcp2515_rx_frame_t *frame) {
  m_nID = frame->id;
  m_nExtFlg = frame->ext;
  m_nRtr = frame->rtr;
  m_nDlc = frame->dlc;
  for (INT8U i = 0; i < frame->dlc; ++i) {
    m_nDta[i] = frame->data[i];
  }
}

INT8U HAL_RAM_FUNC(JHMCP2515::mcp2515_getNextFreeTXBuf)(INT8U *txbuf_n) {
  INT8U ctrlregs[MCP_N_TXBUFFERS] = {MCP_TXB0CTRL, MCP_TXB1CTRL, MCP_TXB2CTRL};
  *txbuf_n = 0x00;
  for (INT8U i = 0; i < MCP_N_TXBUFFERS; ++i) {
    INT8U ctrlval = mcp2515_readRegister(ctrlregs[i]);
    if ((ctrlval & MCP_TXB_TXREQ_M) == 0) {
      *txbuf_n = (INT8U)(ctrlregs[i] + 1);
      return MCP2515_OK;
    }
  }
  return MCP_ALLTXBUSY;
}

JHMCP2515::JHMCP2515(INT8U cs_pin, INT8U spi_bus)
    : m_nExtFlg(0), m_nID(0), m_nDlc(0), m_nRtr(0), m_nfilhit(0),
      m_csPin(cs_pin), mcpMode(MCP_NORMAL), m_spiBus(spi_bus),
      m_driver_mutex(hal_mutex_create()), m_rxRing(), m_rxRingHead(0),
      m_rxRingCount(0), m_rxb1Older(false), m_rxHwLost(0u), m_spiLease(false) {
  hal_gpio_set_mode(m_csPin, HAL_GPIO_OUTPUT);
  hal_gpio_write(m_csPin, true);
}

JHMCP2515::~JHMCP2515() {
  if (m_driver_mutex) {
    hal_mutex_destroy(m_driver_mutex);
    m_driver_mutex = NULL;
  }
}

INT8U JHMCP2515::begin(INT8U idmodeset, INT8U speedset, INT8U clockset) {
  JHMCP2515Guard guard(*this);
  m_rxRingHead = 0u;
  m_rxRingCount = 0u;
  m_rxb1Older = false;
  m_rxHwLost = 0u;
  INT8U res = mcp2515_init(idmodeset, speedset, clockset);
  return (res == MCP2515_OK) ? CAN_OK : CAN_FAILINIT;
}

INT8U JHMCP2515::init_Mask(INT8U num, INT8U ext, INT32U ulData) {
  JHMCP2515Guard guard(*this);
  INT8U res = mcp2515_setCANCTRL_Mode(MODE_CONFIG);
  bool valid = true;
  if (res > 0)
    return res;
  if (num == 0)
    mcp2515_write_mf(MCP_RXM0SIDH, ext, ulData);
  else if (num == 1)
    mcp2515_write_mf(MCP_RXM1SIDH, ext, ulData);
  else
    valid = false;
  res = mcp2515_setCANCTRL_Mode(mcpMode);
  if (res > 0)
    return res;
  return valid ? MCP2515_OK : MCP2515_FAIL;
}

INT8U JHMCP2515::init_Mask(INT8U num, INT32U ulData) {
  return init_Mask(num, (ulData & CAN_IS_EXTENDED) ? 1u : 0u, ulData);
}

INT8U JHMCP2515::init_Filt(INT8U num, INT8U ext, INT32U ulData) {
  JHMCP2515Guard guard(*this);
  INT8U res = mcp2515_setCANCTRL_Mode(MODE_CONFIG);
  if (res > 0)
    return res;
  switch (num) {
  case 0:
    mcp2515_write_mf(MCP_RXF0SIDH, ext, ulData);
    break;
  case 1:
    mcp2515_write_mf(MCP_RXF1SIDH, ext, ulData);
    break;
  case 2:
    mcp2515_write_mf(MCP_RXF2SIDH, ext, ulData);
    break;
  case 3:
    mcp2515_write_mf(MCP_RXF3SIDH, ext, ulData);
    break;
  case 4:
    mcp2515_write_mf(MCP_RXF4SIDH, ext, ulData);
    break;
  case 5:
    mcp2515_write_mf(MCP_RXF5SIDH, ext, ulData);
    break;
  default:
    res = MCP2515_FAIL;
    break;
  }
  const INT8U filterResult = res;
  if (filterResult == MCP2515_OK) {
    mcp2515_modifyRegister(MCP_RXB0CTRL, MCP_RXB_RX_MASK, MCP_RXB_RX_STDEXT);
    mcp2515_modifyRegister(MCP_RXB1CTRL, MCP_RXB_RX_MASK, MCP_RXB_RX_STDEXT);
  }
  const INT8U modeResult = mcp2515_setCANCTRL_Mode(mcpMode);
  return modeResult == MCP2515_OK ? filterResult : modeResult;
}

INT8U JHMCP2515::init_Filt(INT8U num, INT32U ulData) {
  return init_Filt(num, (ulData & CAN_IS_EXTENDED) ? 1u : 0u, ulData);
}

INT8U HAL_RAM_FUNC(JHMCP2515::setMsg)(INT32U id, INT8U rtr, INT8U ext,
                                      INT8U len, INT8U *pData) {
  m_nID = id;
  m_nRtr = rtr;
  m_nExtFlg = ext;
  m_nDlc = (len > MAX_CHAR_IN_MESSAGE) ? MAX_CHAR_IN_MESSAGE : len;
  for (INT8U i = 0; i < m_nDlc; ++i) {
    m_nDta[i] = (pData != NULL) ? *(pData + i) : 0x00u;
  }
  for (INT8U i = m_nDlc; i < MAX_CHAR_IN_MESSAGE; ++i) {
    m_nDta[i] = 0x00u;
  }
  return MCP2515_OK;
}

INT8U JHMCP2515::clearMsg() {
  m_nID = 0;
  m_nDlc = 0;
  m_nExtFlg = 0;
  m_nRtr = 0;
  m_nfilhit = 0;
  for (int i = 0; i < m_nDlc; ++i) {
    m_nDta[i] = 0x00;
  }
  return MCP2515_OK;
}

INT8U HAL_RAM_FUNC(JHMCP2515::sendMsg)() {
  // Free both RX buffers before register setup can delay the first TX poll.
  mcp2515_keep_pending();
  INT8U res, txctrl, txbuf_n;
  uint32_t uiTimeOut, temp;
  temp = hal_micros();
  do {
    res = mcp2515_getNextFreeTXBuf(&txbuf_n);
    if (res == MCP_ALLTXBUSY) {
      // Waiting for TX space must keep receiving as well.
      mcp2515_keep_pending();
    }
    uiTimeOut = hal_micros() - temp;
  } while (res == MCP_ALLTXBUSY && (uiTimeOut < TIMEOUTVALUE));
  if (res == MCP_ALLTXBUSY)
    return CAN_GETTXBFTIMEOUT;
  mcp2515_write_canMsg(txbuf_n);
  mcp2515_modifyRegister((INT8U)(txbuf_n - 1), MCP_TXB_TXREQ_M,
                         MCP_TXB_TXREQ_M);
  /* READ STATUS shows the TXREQ of this buffer and both receive flags, so
   * frames arriving during the wait go to the ring and not to an overflow.
   * Reading a frame ends with a fresh status; without one, ask again. */
  const INT8U txreq = (INT8U)MCP_STAT_TXREQ((txbuf_n - 1 - MCP_TXB0CTRL) >> 4);
  temp = hal_micros();
  INT8U status = mcp2515_readStatus();
  while ((status & txreq) != 0u && (hal_micros() - temp) < TIMEOUTVALUE) {
    if (!mcp2515_keep_rx(&status)) {
      status = mcp2515_readStatus();
    }
  }
  // RX may be ready in the very status that clears TXREQ. Retain it before
  // another SPI device can take the bus; the polling loop does not run then.
  while (mcp2515_keep_rx(&status)) {
  }
  txctrl = mcp2515_readRegister((INT8U)(txbuf_n - 1));
  /* The register decides, not the clock: a task preempted past the timeout
   * after the frame went out must not report a failure. */
  if ((txctrl & MCP_TXB_TXREQ_M) != 0u) {
    /* Clearing TXREQ aborts the frame unless it is on the wire right now;
     * left set, a frame reported as failed would still go out later. */
    mcp2515_modifyRegister((INT8U)(txbuf_n - 1), MCP_TXB_TXREQ_M, 0u);
    return CAN_SENDMSGTIMEOUT;
  }
  /* Normal mode can retain MLOA/TXERR from an attempt that was later retried
   * successfully. In one-shot mode, cleared TXREQ plus any failure flag means
   * the frame was not delivered. */
  if ((txctrl & MCP_TXB_TX_FAILURE_MASK) != 0u &&
      (mcp2515_readRegister(MCP_CANCTRL) & MODE_ONESHOT) != 0u)
    return CAN_FAILTX;
  return CAN_OK;
}

INT8U HAL_RAM_FUNC(JHMCP2515::sendMsgBuf)(INT32U id, INT8U ext, INT8U len,
                                          INT8U *buf) {
  JHMCP2515Guard guard(*this);
  setMsg(id, 0, ext, len, buf);
  return sendMsg();
}

INT8U HAL_RAM_FUNC(JHMCP2515::sendMsgBuf)(INT32U id, INT8U len, INT8U *buf) {
  JHMCP2515Guard guard(*this);
  INT8U ext = 0, rtr = 0;
  if ((id & CAN_IS_EXTENDED) == CAN_IS_EXTENDED) {
    ext = 1;
    id &= ~CAN_IS_EXTENDED;
  }
  if ((id & CAN_IS_REMOTE_REQUEST) == CAN_IS_REMOTE_REQUEST) {
    rtr = 1;
    id &= ~CAN_IS_REMOTE_REQUEST;
  }
  setMsg(id, rtr, ext, len, buf);
  return sendMsg();
}

/* Frames kept during a transmit are older than those still in hardware. */
INT8U HAL_RAM_FUNC(JHMCP2515::readMsg)() {
  if (m_rxRingCount == 0u) {
    mcp2515_keep_pending();
  }
  if (m_rxRingCount == 0u) {
    return CAN_NOMSG;
  }
  mcp2515_load_rx(&m_rxRing[m_rxRingHead]);
  m_rxRingHead = (INT8U)((m_rxRingHead + 1u) % MCP2515_RX_RING_FRAMES);
  m_rxRingCount--;
  return CAN_OK;
}

INT8U HAL_RAM_FUNC(JHMCP2515::readMsgBuf)(INT32U *id, INT8U *ext, INT8U *len,
                                          INT8U buf[]) {
  JHMCP2515Guard guard(*this, false);
  if (m_rxRingCount == 0u) {
    lock_spi_();
  }
  if (readMsg() == CAN_NOMSG)
    return CAN_NOMSG;
  *id = m_nRtr ? (m_nID | CAN_IS_REMOTE_REQUEST) : m_nID;
  *len = m_nDlc;
  *ext = m_nExtFlg;
  for (int i = 0; i < m_nDlc; ++i)
    buf[i] = m_nDta[i];
  return CAN_OK;
}

INT8U HAL_RAM_FUNC(JHMCP2515::readMsgBuf)(INT32U *id, INT8U *len, INT8U buf[]) {
  JHMCP2515Guard guard(*this, false);
  if (m_rxRingCount == 0u) {
    lock_spi_();
  }
  if (readMsg() == CAN_NOMSG)
    return CAN_NOMSG;
  if (m_nExtFlg)
    m_nID |= CAN_IS_EXTENDED;
  if (m_nRtr)
    m_nID |= CAN_IS_REMOTE_REQUEST;
  *id = m_nID;
  *len = m_nDlc;
  for (int i = 0; i < m_nDlc; ++i)
    buf[i] = m_nDta[i];
  return CAN_OK;
}

INT8U HAL_RAM_FUNC(JHMCP2515::checkReceive)(void) {
  JHMCP2515Guard guard(*this, false);
  if (m_rxRingCount > 0u) {
    return CAN_MSGAVAIL;
  }
  lock_spi_();
  // HAL callers poll and read in separate operations. Empty the hardware
  // into the ring before SPI can pass to a display between those calls.
  mcp2515_keep_pending();
  return m_rxRingCount > 0u ? CAN_MSGAVAIL : CAN_NOMSG;
}

INT8U JHMCP2515::checkError(void) {
  JHMCP2515Guard guard(*this);
  return (mcp2515_readRegister(MCP_EFLG) & MCP_EFLG_ERRORMASK) ? CAN_CTRLERROR
                                                               : CAN_OK;
}

INT8U JHMCP2515::getError(void) {
  JHMCP2515Guard guard(*this);
  return mcp2515_readRegister(MCP_EFLG);
}

void HAL_RAM_FUNC(JHMCP2515::getStatus)(jh_mcp2515_status_t *status) {
  JHMCP2515Guard guard(*this);
  mcp2515_keep_pending();
  const INT8U error_flags = mcp2515_readRegister(MCP_EFLG);
  INT8U counters[2];
  mcp2515_readRegisterS(MCP_TEC, counters, (INT8U)COUNTOF(counters));
  const INT8U flags =
      (INT8U)(error_flags & (MCP_EFLG_RX0OVR | MCP_EFLG_RX1OVR));
  if (flags != 0u) {
    m_rxHwLost += ((flags & MCP_EFLG_RX0OVR) != 0u) ? 1u : 0u;
    m_rxHwLost += ((flags & MCP_EFLG_RX1OVR) != 0u) ? 1u : 0u;
    mcp2515_modifyRegister(MCP_EFLG, flags, 0u);
  }
  status->error_flags = error_flags;
  status->transmit_errors = counters[0];
  status->receive_errors = counters[1];
  status->rx_overflows = m_rxHwLost;
  mcp2515_keep_pending();
}

INT8U JHMCP2515::errorCountRX(void) {
  JHMCP2515Guard guard(*this);
  return mcp2515_readRegister(MCP_REC);
}
INT8U JHMCP2515::errorCountTX(void) {
  JHMCP2515Guard guard(*this);
  return mcp2515_readRegister(MCP_TEC);
}

INT8U JHMCP2515::enOneShotTX(void) {
  JHMCP2515Guard guard(*this);
  mcp2515_modifyRegister(MCP_CANCTRL, MODE_ONESHOT, MODE_ONESHOT);
  return ((mcp2515_readRegister(MCP_CANCTRL) & MODE_ONESHOT) == MODE_ONESHOT)
             ? CAN_OK
             : CAN_FAIL;
}

INT8U JHMCP2515::disOneShotTX(void) {
  JHMCP2515Guard guard(*this);
  mcp2515_modifyRegister(MCP_CANCTRL, MODE_ONESHOT, 0);
  return ((mcp2515_readRegister(MCP_CANCTRL) & MODE_ONESHOT) == 0) ? CAN_OK
                                                                   : CAN_FAIL;
}

INT8U JHMCP2515::abortTX(void) {
  JHMCP2515Guard guard(*this);
  static const INT8U kTxCtrl[] = {MCP_TXB0CTRL, MCP_TXB1CTRL, MCP_TXB2CTRL};
  mcp2515_modifyRegister(MCP_CANCTRL, ABORT_TX, ABORT_TX);
  /* ABAT aborts every later transmission too until it is cleared, so wait
   * for the pending buffers to drop TXREQ and release it. */
  const uint32_t started = hal_micros();
  bool pending = true;
  while (pending && !hal_elapsed_u32(hal_micros(), started, TIMEOUTVALUE)) {
    pending = false;
    for (INT8U i = 0; i < (INT8U)COUNTOF(kTxCtrl); ++i) {
      if ((mcp2515_readRegister(kTxCtrl[i]) & MCP_TXB_TXREQ_M) != 0u) {
        pending = true;
      }
    }
  }
  mcp2515_modifyRegister(MCP_CANCTRL, ABORT_TX, 0u);
  return pending ? CAN_FAIL : CAN_OK;
}

INT8U JHMCP2515::setGPO(INT8U data) {
  JHMCP2515Guard guard(*this);
  mcp2515_modifyRegister(MCP_BFPCTRL, MCP_BxBFS_MASK, (INT8U)(data << 4));
  return 0;
}

INT8U JHMCP2515::getGPI(void) {
  JHMCP2515Guard guard(*this);
  return (INT8U)((mcp2515_readRegister(MCP_TXRTSCTRL) & MCP_BxRTS_MASK) >> 3);
}

#endif /* HAL_ENABLE_MCP2515 && HAL_ENABLE_SPI */
