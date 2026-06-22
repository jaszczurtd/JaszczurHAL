#include "../../../hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474)

#include "../../../hal_config.h"
#if defined(HAL_ENABLE_CAN) && defined(HAL_ENABLE_MCP2515)

#include "../../../hal_serial.h"
#include "hal_can_mcp2515.h"

#include <string.h>

static bool can_mcp2515_map_bitrate(uint32_t bitrate_hz, uint8_t *out_speed) {
  if (!out_speed) {
    return false;
  }
  switch (bitrate_hz) {
  case 4096u:
    *out_speed = CAN_4K096BPS;
    break;
  case 5000u:
    *out_speed = CAN_5KBPS;
    break;
  case 10000u:
    *out_speed = CAN_10KBPS;
    break;
  case 20000u:
    *out_speed = CAN_20KBPS;
    break;
  case 31250u:
    *out_speed = CAN_31K25BPS;
    break;
  case 33300u:
    *out_speed = CAN_33K3BPS;
    break;
  case 40000u:
    *out_speed = CAN_40KBPS;
    break;
  case 50000u:
    *out_speed = CAN_50KBPS;
    break;
  case 80000u:
    *out_speed = CAN_80KBPS;
    break;
  case 100000u:
    *out_speed = CAN_100KBPS;
    break;
  case 125000u:
    *out_speed = CAN_125KBPS;
    break;
  case 200000u:
    *out_speed = CAN_200KBPS;
    break;
  case 250000u:
    *out_speed = CAN_250KBPS;
    break;
  case 500000u:
    *out_speed = CAN_500KBPS;
    break;
  case 1000000u:
    *out_speed = CAN_1000KBPS;
    break;
  default:
    return false;
  }
  return true;
}

static bool can_mcp2515_map_clock(uint32_t oscillator_hz, uint8_t *out_clock) {
  if (!out_clock) {
    return false;
  }
  switch (oscillator_hz) {
  case 8000000u:
    *out_clock = MCP_8MHZ;
    break;
  case 16000000u:
    *out_clock = MCP_16MHZ;
    break;
  case 20000000u:
    *out_clock = MCP_20MHZ;
    break;
  default:
    return false;
  }
  return true;
}

bool hal_can_mcp2515_init(JHMCP2515 *mcp, const hal_can_mcp2515_config_t *cfg) {
  if (!mcp || !cfg) {
    return false;
  }

  uint8_t speed = 0u;
  uint8_t clock = 0u;
  if (!can_mcp2515_map_bitrate(cfg->bitrate_hz, &speed) ||
      !can_mcp2515_map_clock(cfg->oscillator_hz, &clock)) {
    hal_derr_limited("can", "unsupported MCP2515 bitrate/clock: %lu/%lu",
                     (unsigned long)cfg->bitrate_hz,
                     (unsigned long)cfg->oscillator_hz);
    return false;
  }

  if (mcp->begin(MCP_ANY, speed, clock) != CAN_OK) {
    return false;
  }
  mcp->setMode(MCP_NORMAL);
  if (cfg->one_shot_tx) {
    mcp->enOneShotTX();
  } else {
    mcp->disOneShotTX();
  }
  mcp->setSleepWakeup(cfg->sleep_wakeup ? 1u : 0u);
  return true;
}

void hal_can_mcp2515_deinit(JHMCP2515 *mcp) {
  if (!mcp) {
    return;
  }
  mcp->~JHMCP2515();
}

bool hal_can_mcp2515_send(JHMCP2515 *mcp, uint32_t id, uint8_t len,
                          const uint8_t *data) {
  if (!mcp) {
    return false;
  }
  if (len > 0 && data == NULL) {
    hal_derr_limited("can", "send called with NULL data pointer and len=%u",
                     (unsigned)len);
    return false;
  }

  uint8_t buf[HAL_CAN_MAX_DATA_LEN];
  uint8_t safe_len = len <= HAL_CAN_MAX_DATA_LEN ? len : HAL_CAN_MAX_DATA_LEN;
  if (safe_len > 0) {
    memcpy(buf, data, safe_len);
  }

  bool ok = mcp->sendMsgBuf(id, safe_len, buf) == CAN_OK;
  if (!ok) {
    hal_derr_limited("can", "send failed for id=%u", (unsigned)id);
  }
  return ok;
}

bool hal_can_mcp2515_receive(JHMCP2515 *mcp, uint32_t *id, uint8_t *len,
                             uint8_t *data) {
  if (!mcp || !id || !len || !data) {
    return false;
  }
  if (mcp->checkReceive() != CAN_MSGAVAIL) {
    return false;
  }

  uint8_t buf[HAL_CAN_MAX_DATA_LEN] = {};
  uint8_t msg_len = 0;
  uint32_t msg_id = 0;
  bool ok = mcp->readMsgBuf(&msg_id, &msg_len, buf) == CAN_OK;
  if (!ok)
    return false;

  *id = msg_id;
  *len = msg_len;
  memcpy(data, buf,
         msg_len < HAL_CAN_MAX_DATA_LEN ? msg_len : HAL_CAN_MAX_DATA_LEN);
  return true;
}

bool hal_can_mcp2515_available(JHMCP2515 *mcp) {
  return mcp && mcp->checkReceive() == CAN_MSGAVAIL;
}

bool hal_can_mcp2515_set_std_filters(JHMCP2515 *mcp, uint32_t id0,
                                     uint32_t id1) {
  if (!mcp) {
    return false;
  }
  uint32_t mask = 0x07FF0000UL;
  uint32_t fid0 = (id0 & 0x7FFU) << 16;
  uint32_t fid1 = (id1 & 0x7FFU) << 16;
  bool ok = true;
  ok = ok && (mcp->init_Mask(0, 0, mask) == CAN_OK);
  ok = ok && (mcp->init_Mask(1, 0, mask) == CAN_OK);
  ok = ok && (mcp->init_Filt(0, 0, fid0) == CAN_OK);
  ok = ok && (mcp->init_Filt(1, 0, fid1) == CAN_OK);
  ok = ok && (mcp->init_Filt(2, 0, fid0) == CAN_OK);
  ok = ok && (mcp->init_Filt(3, 0, fid1) == CAN_OK);
  ok = ok && (mcp->init_Filt(4, 0, fid0) == CAN_OK);
  ok = ok && (mcp->init_Filt(5, 0, fid1) == CAN_OK);
  return ok;
}

#endif /* HAL_ENABLE_CAN && HAL_ENABLE_MCP2515 */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
