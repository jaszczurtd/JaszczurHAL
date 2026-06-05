#pragma once

/*
 * HAL-only MCP2515 driver derived from the working register/SPI logic of the
 * Seeed/Loovee MCP_CAN library, with contributions by Cory J. Fowler.
 * The behavior and register programming flow are intentionally kept aligned
 * with the upstream implementation while removing Arduino runtime dependencies.
 */

#include "mcp2515_driver_defs.h"
#include "../../../hal_spi.h"
#include "../../../hal_sync.h"

class JHMCP2515Guard;

class JHMCP2515 {
private:
    INT8U m_nExtFlg;
    INT32U m_nID;
    INT8U m_nDlc;
    INT8U m_nDta[MAX_CHAR_IN_MESSAGE];
    INT8U m_nRtr;
    INT8U m_nfilhit;
    INT8U m_csPin;
    INT8U mcpMode;
    INT8U m_spiBus;
    hal_mutex_t m_driver_mutex;

    void mcp2515_reset(void);
    INT8U mcp2515_readRegister(INT8U address);
    void mcp2515_readRegisterS(INT8U address, INT8U values[], INT8U n);
    void mcp2515_setRegister(INT8U address, INT8U value);
    void mcp2515_setRegisterS(INT8U address, const INT8U values[], INT8U n);
    void mcp2515_initCANBuffers(void);
    void mcp2515_modifyRegister(INT8U address, INT8U mask, INT8U data);
    INT8U mcp2515_readStatus(void);
    INT8U mcp2515_setCANCTRL_Mode(INT8U newmode);
    INT8U mcp2515_requestNewMode(INT8U newmode);
    INT8U mcp2515_configRate(INT8U canSpeed, INT8U canClock);
    INT8U mcp2515_init(INT8U canIDMode, INT8U canSpeed, INT8U canClock);
    void mcp2515_write_mf(INT8U mcp_addr, INT8U ext, INT32U id);
    void mcp2515_write_id(INT8U mcp_addr, INT8U ext, INT32U id);
    void mcp2515_read_id(INT8U mcp_addr, INT8U *ext, INT32U *id);
    void mcp2515_write_canMsg(INT8U buffer_sidh_addr);
    void mcp2515_read_canMsg(INT8U buffer_sidh_addr);
    INT8U mcp2515_getNextFreeTXBuf(INT8U *txbuf_n);
    void spi_begin_(void);
    void spi_end_(void);
    void lock_driver_(void);
    void unlock_driver_(void);

    INT8U setMsg(INT32U id, INT8U rtr, INT8U ext, INT8U len, INT8U *pData);
    INT8U clearMsg();
    INT8U readMsg();
    INT8U sendMsg();

public:
    JHMCP2515(INT8U cs_pin, INT8U spi_bus = 0);
    ~JHMCP2515();

    INT8U begin(INT8U idmodeset, INT8U speedset, INT8U clockset);
    INT8U init_Mask(INT8U num, INT8U ext, INT32U ulData);
    INT8U init_Mask(INT8U num, INT32U ulData);
    INT8U init_Filt(INT8U num, INT8U ext, INT32U ulData);
    INT8U init_Filt(INT8U num, INT32U ulData);
    void setSleepWakeup(INT8U enable);
    INT8U setMode(INT8U opMode);
    INT8U sendMsgBuf(INT32U id, INT8U ext, INT8U len, INT8U *buf);
    INT8U sendMsgBuf(INT32U id, INT8U len, INT8U *buf);
    INT8U readMsgBuf(INT32U *id, INT8U *ext, INT8U *len, INT8U *buf);
    INT8U readMsgBuf(INT32U *id, INT8U *len, INT8U *buf);
    INT8U checkReceive(void);
    INT8U checkError(void);
    INT8U getError(void);
    INT8U errorCountRX(void);
    INT8U errorCountTX(void);
    INT8U enOneShotTX(void);
    INT8U disOneShotTX(void);
    INT8U abortTX(void);
    INT8U setGPO(INT8U data);
    INT8U getGPI(void);

    friend class JHMCP2515Guard;
};
