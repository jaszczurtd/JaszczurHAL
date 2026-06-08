#include "../../../hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "../../../hal_config.h"
#if defined(HAL_ENABLE_CAN) && defined(HAL_ENABLE_SPI)

#include "mcp2515_driver.h"

#include "../../../hal_gpio.h"
#include "../../../hal_serial.h"
#include "../../../hal_system.h"

static const hal_spi_settings_t kMcp2515SpiSettings = {
    10000000u,
    HAL_SPI_MSBFIRST,
    HAL_SPI_MODE0,
};

class JHMCP2515Guard {
public:
    explicit JHMCP2515Guard(JHMCP2515 &driver) : d(driver) { d.lock_driver_(); }
    ~JHMCP2515Guard() { d.unlock_driver_(); }
private:
    JHMCP2515 &d;
};

void JHMCP2515::lock_driver_(void) { hal_mutex_lock(m_driver_mutex); }
void JHMCP2515::unlock_driver_(void) { hal_mutex_unlock(m_driver_mutex); }

void JHMCP2515::spi_begin_(void) {
    hal_spi_lock(m_spiBus);
    hal_spi_begin_transaction(m_spiBus, &kMcp2515SpiSettings);
    hal_gpio_write(m_csPin, false);
}

void JHMCP2515::spi_end_(void) {
    hal_gpio_write(m_csPin, true);
    hal_spi_end_transaction(m_spiBus);
    hal_spi_unlock(m_spiBus);
}

void JHMCP2515::mcp2515_reset(void) {
    spi_begin_();
    hal_spi_transfer(m_spiBus, MCP_RESET);
    spi_end_();
    hal_delay_ms(5);
}

INT8U JHMCP2515::mcp2515_readRegister(const INT8U address) {
    spi_begin_();
    hal_spi_transfer(m_spiBus, MCP_READ);
    hal_spi_transfer(m_spiBus, address);
    INT8U ret = hal_spi_transfer(m_spiBus, 0x00u);
    spi_end_();
    return ret;
}

void JHMCP2515::mcp2515_readRegisterS(const INT8U address, INT8U values[], const INT8U n) {
    spi_begin_();
    hal_spi_transfer(m_spiBus, MCP_READ);
    hal_spi_transfer(m_spiBus, address);
    for (INT8U i = 0; i < n; ++i) {
        values[i] = hal_spi_transfer(m_spiBus, 0x00u);
    }
    spi_end_();
}

void JHMCP2515::mcp2515_setRegister(const INT8U address, const INT8U value) {
    spi_begin_();
    hal_spi_transfer(m_spiBus, MCP_WRITE);
    hal_spi_transfer(m_spiBus, address);
    hal_spi_transfer(m_spiBus, value);
    spi_end_();
}

void JHMCP2515::mcp2515_setRegisterS(const INT8U address, const INT8U values[], const INT8U n) {
    spi_begin_();
    hal_spi_transfer(m_spiBus, MCP_WRITE);
    hal_spi_transfer(m_spiBus, address);
    for (INT8U i = 0; i < n; ++i) {
        hal_spi_transfer(m_spiBus, values[i]);
    }
    spi_end_();
}

void JHMCP2515::mcp2515_modifyRegister(const INT8U address, const INT8U mask, const INT8U data) {
    spi_begin_();
    hal_spi_transfer(m_spiBus, MCP_BITMOD);
    hal_spi_transfer(m_spiBus, address);
    hal_spi_transfer(m_spiBus, mask);
    hal_spi_transfer(m_spiBus, data);
    spi_end_();
}

INT8U JHMCP2515::mcp2515_readStatus(void) {
    spi_begin_();
    hal_spi_transfer(m_spiBus, MCP_READ_STATUS);
    INT8U status = hal_spi_transfer(m_spiBus, 0x00u);
    spi_end_();
    return status;
}

void JHMCP2515::setSleepWakeup(const INT8U enable) {
    JHMCP2515Guard guard(*this);
    mcp2515_modifyRegister(MCP_CANINTE, MCP_WAKIF, enable ? MCP_WAKIF : 0u);
}

INT8U JHMCP2515::setMode(const INT8U opMode) {
    JHMCP2515Guard guard(*this);
    mcpMode = opMode;
    return mcp2515_setCANCTRL_Mode(mcpMode);
}

INT8U JHMCP2515::mcp2515_setCANCTRL_Mode(const INT8U newmode) {
    if ((mcp2515_readRegister(MCP_CANSTAT) & MODE_MASK) == MCP_SLEEP && newmode != MCP_SLEEP) {
        INT8U wakeIntEnabled = (INT8U)(mcp2515_readRegister(MCP_CANINTE) & MCP_WAKIF);
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

INT8U JHMCP2515::mcp2515_configRate(const INT8U canSpeed, const INT8U canClock) {
    INT8U cfg1 = 0, cfg2 = 0, cfg3 = 0;
    switch (canClock & MCP_CLOCK_SELECT) {
        case MCP_8MHZ:
            switch (canSpeed) {
                case CAN_5KBPS: cfg1 = MCP_8MHz_5kBPS_CFG1; cfg2 = MCP_8MHz_5kBPS_CFG2; cfg3 = MCP_8MHz_5kBPS_CFG3; break;
                case CAN_10KBPS: cfg1 = MCP_8MHz_10kBPS_CFG1; cfg2 = MCP_8MHz_10kBPS_CFG2; cfg3 = MCP_8MHz_10kBPS_CFG3; break;
                case CAN_20KBPS: cfg1 = MCP_8MHz_20kBPS_CFG1; cfg2 = MCP_8MHz_20kBPS_CFG2; cfg3 = MCP_8MHz_20kBPS_CFG3; break;
                case CAN_31K25BPS: cfg1 = MCP_8MHz_31k25BPS_CFG1; cfg2 = MCP_8MHz_31k25BPS_CFG2; cfg3 = MCP_8MHz_31k25BPS_CFG3; break;
                case CAN_33K3BPS: cfg1 = MCP_8MHz_33k3BPS_CFG1; cfg2 = MCP_8MHz_33k3BPS_CFG2; cfg3 = MCP_8MHz_33k3BPS_CFG3; break;
                case CAN_40KBPS: cfg1 = MCP_8MHz_40kBPS_CFG1; cfg2 = MCP_8MHz_40kBPS_CFG2; cfg3 = MCP_8MHz_40kBPS_CFG3; break;
                case CAN_50KBPS: cfg1 = MCP_8MHz_50kBPS_CFG1; cfg2 = MCP_8MHz_50kBPS_CFG2; cfg3 = MCP_8MHz_50kBPS_CFG3; break;
                case CAN_80KBPS: cfg1 = MCP_8MHz_80kBPS_CFG1; cfg2 = MCP_8MHz_80kBPS_CFG2; cfg3 = MCP_8MHz_80kBPS_CFG3; break;
                case CAN_100KBPS: cfg1 = MCP_8MHz_100kBPS_CFG1; cfg2 = MCP_8MHz_100kBPS_CFG2; cfg3 = MCP_8MHz_100kBPS_CFG3; break;
                case CAN_125KBPS: cfg1 = MCP_8MHz_125kBPS_CFG1; cfg2 = MCP_8MHz_125kBPS_CFG2; cfg3 = MCP_8MHz_125kBPS_CFG3; break;
                case CAN_200KBPS: cfg1 = MCP_8MHz_200kBPS_CFG1; cfg2 = MCP_8MHz_200kBPS_CFG2; cfg3 = MCP_8MHz_200kBPS_CFG3; break;
                case CAN_250KBPS: cfg1 = MCP_8MHz_250kBPS_CFG1; cfg2 = MCP_8MHz_250kBPS_CFG2; cfg3 = MCP_8MHz_250kBPS_CFG3; break;
                case CAN_500KBPS: cfg1 = MCP_8MHz_500kBPS_CFG1; cfg2 = MCP_8MHz_500kBPS_CFG2; cfg3 = MCP_8MHz_500kBPS_CFG3; break;
                case CAN_1000KBPS: cfg1 = MCP_8MHz_1000kBPS_CFG1; cfg2 = MCP_8MHz_1000kBPS_CFG2; cfg3 = MCP_8MHz_1000kBPS_CFG3; break;
                default: return MCP2515_FAIL;
            }
            break;
        case MCP_16MHZ:
            switch (canSpeed) {
                case CAN_5KBPS: cfg1 = MCP_16MHz_5kBPS_CFG1; cfg2 = MCP_16MHz_5kBPS_CFG2; cfg3 = MCP_16MHz_5kBPS_CFG3; break;
                case CAN_10KBPS: cfg1 = MCP_16MHz_10kBPS_CFG1; cfg2 = MCP_16MHz_10kBPS_CFG2; cfg3 = MCP_16MHz_10kBPS_CFG3; break;
                case CAN_20KBPS: cfg1 = MCP_16MHz_20kBPS_CFG1; cfg2 = MCP_16MHz_20kBPS_CFG2; cfg3 = MCP_16MHz_20kBPS_CFG3; break;
                case CAN_33K3BPS: cfg1 = MCP_16MHz_33k3BPS_CFG1; cfg2 = MCP_16MHz_33k3BPS_CFG2; cfg3 = MCP_16MHz_33k3BPS_CFG3; break;
                case CAN_40KBPS: cfg1 = MCP_16MHz_40kBPS_CFG1; cfg2 = MCP_16MHz_40kBPS_CFG2; cfg3 = MCP_16MHz_40kBPS_CFG3; break;
                case CAN_50KBPS: cfg2 = MCP_16MHz_50kBPS_CFG2; cfg3 = MCP_16MHz_50kBPS_CFG3; break;
                case CAN_80KBPS: cfg1 = MCP_16MHz_80kBPS_CFG1; cfg2 = MCP_16MHz_80kBPS_CFG2; cfg3 = MCP_16MHz_80kBPS_CFG3; break;
                case CAN_100KBPS: cfg1 = MCP_16MHz_100kBPS_CFG1; cfg2 = MCP_16MHz_100kBPS_CFG2; cfg3 = MCP_16MHz_100kBPS_CFG3; break;
                case CAN_125KBPS: cfg1 = MCP_16MHz_125kBPS_CFG1; cfg2 = MCP_16MHz_125kBPS_CFG2; cfg3 = MCP_16MHz_125kBPS_CFG3; break;
                case CAN_200KBPS: cfg1 = MCP_16MHz_200kBPS_CFG1; cfg2 = MCP_16MHz_200kBPS_CFG2; cfg3 = MCP_16MHz_200kBPS_CFG3; break;
                case CAN_250KBPS: cfg1 = MCP_16MHz_250kBPS_CFG1; cfg2 = MCP_16MHz_250kBPS_CFG2; cfg3 = MCP_16MHz_250kBPS_CFG3; break;
                case CAN_500KBPS: cfg1 = MCP_16MHz_500kBPS_CFG1; cfg2 = MCP_16MHz_500kBPS_CFG2; cfg3 = MCP_16MHz_500kBPS_CFG3; break;
                case CAN_1000KBPS: cfg1 = MCP_16MHz_1000kBPS_CFG1; cfg2 = MCP_16MHz_1000kBPS_CFG2; cfg3 = MCP_16MHz_1000kBPS_CFG3; break;
                default: return MCP2515_FAIL;
            }
            break;
        case MCP_20MHZ:
            switch (canSpeed) {
                case CAN_40KBPS: cfg1 = MCP_20MHz_40kBPS_CFG1; cfg2 = MCP_20MHz_40kBPS_CFG2; cfg3 = MCP_20MHz_40kBPS_CFG3; break;
                case CAN_50KBPS: cfg1 = MCP_20MHz_50kBPS_CFG1; cfg2 = MCP_20MHz_50kBPS_CFG2; cfg3 = MCP_20MHz_50kBPS_CFG3; break;
                case CAN_80KBPS: cfg1 = MCP_20MHz_80kBPS_CFG1; cfg2 = MCP_20MHz_80kBPS_CFG2; cfg3 = MCP_20MHz_80kBPS_CFG3; break;
                case CAN_100KBPS: cfg1 = MCP_20MHz_100kBPS_CFG1; cfg2 = MCP_20MHz_100kBPS_CFG2; cfg3 = MCP_20MHz_100kBPS_CFG3; break;
                case CAN_125KBPS: cfg1 = MCP_20MHz_125kBPS_CFG1; cfg2 = MCP_20MHz_125kBPS_CFG2; cfg3 = MCP_20MHz_125kBPS_CFG3; break;
                case CAN_200KBPS: cfg1 = MCP_20MHz_200kBPS_CFG1; cfg2 = MCP_20MHz_200kBPS_CFG2; cfg3 = MCP_20MHz_200kBPS_CFG3; break;
                case CAN_250KBPS: cfg1 = MCP_20MHz_250kBPS_CFG1; cfg2 = MCP_20MHz_250kBPS_CFG2; cfg3 = MCP_20MHz_250kBPS_CFG3; break;
                case CAN_500KBPS: cfg1 = MCP_20MHz_500kBPS_CFG1; cfg2 = MCP_20MHz_500kBPS_CFG2; cfg3 = MCP_20MHz_500kBPS_CFG3; break;
                case CAN_1000KBPS: cfg1 = MCP_20MHz_1000kBPS_CFG1; cfg2 = MCP_20MHz_1000kBPS_CFG2; cfg3 = MCP_20MHz_1000kBPS_CFG3; break;
                default: return MCP2515_FAIL;
            }
            break;
        default:
            return MCP2515_FAIL;
    }
    if (canClock & MCP_CLKOUT_ENABLE) {
        cfg3 &= (INT8U)(~0x80u);
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

INT8U JHMCP2515::mcp2515_init(const INT8U canIDMode, const INT8U canSpeed, const INT8U canClock) {
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
                mcp2515_modifyRegister(MCP_RXB0CTRL, MCP_RXB_RX_MASK | MCP_RXB_BUKT_MASK, MCP_RXB_RX_ANY | MCP_RXB_BUKT_MASK);
                mcp2515_modifyRegister(MCP_RXB1CTRL, MCP_RXB_RX_MASK, MCP_RXB_RX_ANY);
                break;
            case MCP_STDEXT:
                mcp2515_modifyRegister(MCP_RXB0CTRL, MCP_RXB_RX_MASK | MCP_RXB_BUKT_MASK, MCP_RXB_RX_STDEXT | MCP_RXB_BUKT_MASK);
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

void JHMCP2515::mcp2515_write_id(const INT8U mcp_addr, const INT8U ext, const INT32U id) {
    uint16_t canid = (uint16_t)(id & 0x0FFFFu);
    INT8U tbufdata[4];
    if (ext == 1u) {
        tbufdata[MCP_EID0] = (INT8U)(canid & 0xFFu);
        tbufdata[MCP_EID8] = (INT8U)(canid >> 8);
        canid = (uint16_t)(id >> 16);
        tbufdata[MCP_SIDL] = (INT8U)(canid & 0x03u);
        tbufdata[MCP_SIDL] += (INT8U)((canid & 0x1Cu) << 3);
        tbufdata[MCP_SIDL] |= MCP_TXB_EXIDE_M;
        tbufdata[MCP_SIDH] = (INT8U)(canid >> 5);
    } else {
        tbufdata[MCP_SIDH] = (INT8U)(canid >> 3);
        tbufdata[MCP_SIDL] = (INT8U)((canid & 0x07u) << 5);
        tbufdata[MCP_EID0] = 0;
        tbufdata[MCP_EID8] = 0;
    }
    mcp2515_setRegisterS(mcp_addr, tbufdata, 4);
}

void JHMCP2515::mcp2515_write_mf(const INT8U mcp_addr, const INT8U ext, const INT32U id) {
    uint16_t canid = (uint16_t)(id & 0x0FFFFu);
    INT8U tbufdata[4];
    if (ext == 1u) {
        tbufdata[MCP_EID0] = (INT8U)(canid & 0xFFu);
        tbufdata[MCP_EID8] = (INT8U)(canid >> 8);
        canid = (uint16_t)(id >> 16);
        tbufdata[MCP_SIDL] = (INT8U)(canid & 0x03u);
        tbufdata[MCP_SIDL] += (INT8U)((canid & 0x1Cu) << 3);
        tbufdata[MCP_SIDL] |= MCP_TXB_EXIDE_M;
        tbufdata[MCP_SIDH] = (INT8U)(canid >> 5);
    } else {
        tbufdata[MCP_EID0] = (INT8U)(canid & 0xFFu);
        tbufdata[MCP_EID8] = (INT8U)(canid >> 8);
        canid = (uint16_t)(id >> 16);
        tbufdata[MCP_SIDL] = (INT8U)((canid & 0x07u) << 5);
        tbufdata[MCP_SIDH] = (INT8U)(canid >> 3);
    }
    mcp2515_setRegisterS(mcp_addr, tbufdata, 4);
}

void JHMCP2515::mcp2515_read_id(const INT8U mcp_addr, INT8U *ext, INT32U *id) {
    INT8U tbufdata[4];
    *ext = 0;
    *id = 0;
    mcp2515_readRegisterS(mcp_addr, tbufdata, 4);
    *id = ((INT32U)tbufdata[MCP_SIDH] << 3) + (tbufdata[MCP_SIDL] >> 5);
    if ((tbufdata[MCP_SIDL] & MCP_TXB_EXIDE_M) == MCP_TXB_EXIDE_M) {
        *id = (*id << 2) + (tbufdata[MCP_SIDL] & 0x03u);
        *id = (*id << 8) + tbufdata[MCP_EID8];
        *id = (*id << 8) + tbufdata[MCP_EID0];
        *ext = 1;
    }
}

void JHMCP2515::mcp2515_write_canMsg(const INT8U buffer_sidh_addr) {
    INT8U mcp_addr = buffer_sidh_addr;
    mcp2515_setRegisterS((INT8U)(mcp_addr + 5), m_nDta, m_nDlc);
    if (m_nRtr == 1) {
        m_nDlc |= MCP_RTR_MASK;
    }
    mcp2515_setRegister((INT8U)(mcp_addr + 4), m_nDlc);
    mcp2515_write_id(mcp_addr, m_nExtFlg, m_nID);
}

void JHMCP2515::mcp2515_read_canMsg(const INT8U buffer_sidh_addr) {
    INT8U ctrl, sidl;
    mcp2515_read_id(buffer_sidh_addr, &m_nExtFlg, &m_nID);
    ctrl = mcp2515_readRegister((INT8U)(buffer_sidh_addr - 1));
    sidl = mcp2515_readRegister((INT8U)(buffer_sidh_addr + MCP_SIDL));
    m_nDlc = mcp2515_readRegister((INT8U)(buffer_sidh_addr + 4));
    m_nRtr = ((ctrl & 0x08u) || ((m_nExtFlg == 0u) && (sidl & 0x10u))) ? 1u : 0u;
    m_nDlc &= MCP_DLC_MASK;
    if (m_nDlc > MAX_CHAR_IN_MESSAGE) {
        m_nDlc = MAX_CHAR_IN_MESSAGE;
    }
    mcp2515_readRegisterS((INT8U)(buffer_sidh_addr + 5), &m_nDta[0], m_nDlc);
}

INT8U JHMCP2515::mcp2515_getNextFreeTXBuf(INT8U *txbuf_n) {
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
      m_csPin(cs_pin), mcpMode(MCP_NORMAL), m_spiBus((spi_bus == 1u) ? 1u : 0u),
      m_driver_mutex(hal_mutex_create()) {
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
    INT8U res = mcp2515_init(idmodeset, speedset, clockset);
    return (res == MCP2515_OK) ? CAN_OK : CAN_FAILINIT;
}

INT8U JHMCP2515::init_Mask(INT8U num, INT8U ext, INT32U ulData) {
    JHMCP2515Guard guard(*this);
    INT8U res = mcp2515_setCANCTRL_Mode(MODE_CONFIG);
    bool valid = true;
    if (res > 0) return res;
    if (num == 0) mcp2515_write_mf(MCP_RXM0SIDH, ext, ulData);
    else if (num == 1) mcp2515_write_mf(MCP_RXM1SIDH, ext, ulData);
    else valid = false;
    res = mcp2515_setCANCTRL_Mode(mcpMode);
    if (res > 0) return res;
    return valid ? MCP2515_OK : MCP2515_FAIL;
}

INT8U JHMCP2515::init_Mask(INT8U num, INT32U ulData) {
    return init_Mask(num, (ulData & CAN_IS_EXTENDED) ? 1u : 0u, ulData);
}

INT8U JHMCP2515::init_Filt(INT8U num, INT8U ext, INT32U ulData) {
    JHMCP2515Guard guard(*this);
    INT8U res = mcp2515_setCANCTRL_Mode(MODE_CONFIG);
    if (res > 0) return res;
    switch (num) {
        case 0: mcp2515_write_mf(MCP_RXF0SIDH, ext, ulData); break;
        case 1: mcp2515_write_mf(MCP_RXF1SIDH, ext, ulData); break;
        case 2: mcp2515_write_mf(MCP_RXF2SIDH, ext, ulData); break;
        case 3: mcp2515_write_mf(MCP_RXF3SIDH, ext, ulData); break;
        case 4: mcp2515_write_mf(MCP_RXF4SIDH, ext, ulData); break;
        case 5: mcp2515_write_mf(MCP_RXF5SIDH, ext, ulData); break;
        default: res = MCP2515_FAIL; break;
    }
    if (res == MCP2515_OK) {
        res = mcp2515_setCANCTRL_Mode(mcpMode);
    }
    return res;
}

INT8U JHMCP2515::init_Filt(INT8U num, INT32U ulData) {
    return init_Filt(num, (ulData & CAN_IS_EXTENDED) ? 1u : 0u, ulData);
}

INT8U JHMCP2515::setMsg(INT32U id, INT8U rtr, INT8U ext, INT8U len, INT8U *pData) {
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

INT8U JHMCP2515::sendMsg() {
    INT8U res, res1, txbuf_n;
    uint32_t uiTimeOut, temp;
    temp = hal_micros();
    do {
        res = mcp2515_getNextFreeTXBuf(&txbuf_n);
        uiTimeOut = hal_micros() - temp;
    } while (res == MCP_ALLTXBUSY && (uiTimeOut < TIMEOUTVALUE));
    if (uiTimeOut >= TIMEOUTVALUE) return CAN_GETTXBFTIMEOUT;
    mcp2515_write_canMsg(txbuf_n);
    mcp2515_modifyRegister((INT8U)(txbuf_n - 1), MCP_TXB_TXREQ_M, MCP_TXB_TXREQ_M);
    temp = hal_micros();
    do {
        res1 = (INT8U)(mcp2515_readRegister((INT8U)(txbuf_n - 1)) & 0x08u);
        uiTimeOut = hal_micros() - temp;
    } while (res1 && (uiTimeOut < TIMEOUTVALUE));
    if (uiTimeOut >= TIMEOUTVALUE) return CAN_SENDMSGTIMEOUT;
    return CAN_OK;
}

INT8U JHMCP2515::sendMsgBuf(INT32U id, INT8U ext, INT8U len, INT8U *buf) {
    JHMCP2515Guard guard(*this);
    setMsg(id, 0, ext, len, buf);
    return sendMsg();
}

INT8U JHMCP2515::sendMsgBuf(INT32U id, INT8U len, INT8U *buf) {
    JHMCP2515Guard guard(*this);
    INT8U ext = 0, rtr = 0;
    if ((id & CAN_IS_EXTENDED) == CAN_IS_EXTENDED) ext = 1;
    if ((id & CAN_IS_REMOTE_REQUEST) == CAN_IS_REMOTE_REQUEST) rtr = 1;
    setMsg(id, rtr, ext, len, buf);
    return sendMsg();
}

INT8U JHMCP2515::readMsg() {
    INT8U stat = mcp2515_readStatus();
    if (stat & MCP_STAT_RX0IF) {
        mcp2515_read_canMsg(MCP_RXBUF_0);
        mcp2515_modifyRegister(MCP_CANINTF, MCP_RX0IF, 0);
        return CAN_OK;
    }
    if (stat & MCP_STAT_RX1IF) {
        mcp2515_read_canMsg(MCP_RXBUF_1);
        mcp2515_modifyRegister(MCP_CANINTF, MCP_RX1IF, 0);
        return CAN_OK;
    }
    return CAN_NOMSG;
}

INT8U JHMCP2515::readMsgBuf(INT32U *id, INT8U *ext, INT8U *len, INT8U buf[]) {
    JHMCP2515Guard guard(*this);
    if (readMsg() == CAN_NOMSG) return CAN_NOMSG;
    *id = m_nID;
    *len = m_nDlc;
    *ext = m_nExtFlg;
    for (int i = 0; i < m_nDlc; ++i) buf[i] = m_nDta[i];
    return CAN_OK;
}

INT8U JHMCP2515::readMsgBuf(INT32U *id, INT8U *len, INT8U buf[]) {
    JHMCP2515Guard guard(*this);
    if (readMsg() == CAN_NOMSG) return CAN_NOMSG;
    if (m_nExtFlg) m_nID |= CAN_IS_EXTENDED;
    if (m_nRtr) m_nID |= CAN_IS_REMOTE_REQUEST;
    *id = m_nID;
    *len = m_nDlc;
    for (int i = 0; i < m_nDlc; ++i) buf[i] = m_nDta[i];
    return CAN_OK;
}

INT8U JHMCP2515::checkReceive(void) {
    JHMCP2515Guard guard(*this);
    return (mcp2515_readStatus() & MCP_STAT_RXIF_MASK) ? CAN_MSGAVAIL : CAN_NOMSG;
}

INT8U JHMCP2515::checkError(void) {
    JHMCP2515Guard guard(*this);
    return (mcp2515_readRegister(MCP_EFLG) & MCP_EFLG_ERRORMASK) ? CAN_CTRLERROR : CAN_OK;
}

INT8U JHMCP2515::getError(void) {
    JHMCP2515Guard guard(*this);
    return mcp2515_readRegister(MCP_EFLG);
}

INT8U JHMCP2515::errorCountRX(void) { JHMCP2515Guard guard(*this); return mcp2515_readRegister(MCP_REC); }
INT8U JHMCP2515::errorCountTX(void) { JHMCP2515Guard guard(*this); return mcp2515_readRegister(MCP_TEC); }

INT8U JHMCP2515::enOneShotTX(void) {
    JHMCP2515Guard guard(*this);
    mcp2515_modifyRegister(MCP_CANCTRL, MODE_ONESHOT, MODE_ONESHOT);
    return ((mcp2515_readRegister(MCP_CANCTRL) & MODE_ONESHOT) == MODE_ONESHOT) ? CAN_OK : CAN_FAIL;
}

INT8U JHMCP2515::disOneShotTX(void) {
    JHMCP2515Guard guard(*this);
    mcp2515_modifyRegister(MCP_CANCTRL, MODE_ONESHOT, 0);
    return ((mcp2515_readRegister(MCP_CANCTRL) & MODE_ONESHOT) == 0) ? CAN_OK : CAN_FAIL;
}

INT8U JHMCP2515::abortTX(void) {
    JHMCP2515Guard guard(*this);
    mcp2515_modifyRegister(MCP_CANCTRL, ABORT_TX, ABORT_TX);
    return ((mcp2515_readRegister(MCP_CANCTRL) & ABORT_TX) == ABORT_TX) ? CAN_OK : CAN_FAIL;
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

#endif /* HAL_ENABLE_CAN && HAL_ENABLE_SPI */
#endif /* supported target */