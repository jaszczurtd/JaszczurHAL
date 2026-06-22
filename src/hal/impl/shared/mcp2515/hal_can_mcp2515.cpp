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

static bool can_mcp2515_mode_valid(hal_can_mode_t mode) {
  const hal_can_mode_t supported = HAL_CAN_MODE_LOOPBACK |
                                   HAL_CAN_MODE_LISTEN_ONLY |
                                   HAL_CAN_MODE_ONE_SHOT | HAL_CAN_MODE_SLEEP;
  if ((mode & ~supported) != 0u) {
    return false;
  }
  uint8_t ops = 0u;
  ops += (mode & HAL_CAN_MODE_LOOPBACK) != 0u ? 1u : 0u;
  ops += (mode & HAL_CAN_MODE_LISTEN_ONLY) != 0u ? 1u : 0u;
  ops += (mode & HAL_CAN_MODE_SLEEP) != 0u ? 1u : 0u;
  return ops <= 1u;
}

static uint8_t can_mcp2515_op_mode(hal_can_mode_t mode) {
  if ((mode & HAL_CAN_MODE_SLEEP) != 0u) {
    return MCP_SLEEP;
  }
  if ((mode & HAL_CAN_MODE_LISTEN_ONLY) != 0u) {
    return MCP_LISTENONLY;
  }
  if ((mode & HAL_CAN_MODE_LOOPBACK) != 0u) {
    return MCP_LOOPBACK;
  }
  return MCP_NORMAL;
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

static bool can_mcp2515_validate_classic_frame(const hal_can_frame_t *frame) {
  if (!hal_can_validate_frame(frame)) {
    return false;
  }
  if (frame->flags &
      (HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS | HAL_CAN_FRAME_ESI)) {
    hal_derr_limited("can", "MCP2515 does not support CAN FD frames");
    return false;
  }
  return true;
}

bool hal_can_mcp2515_send_frame(JHMCP2515 *mcp, const hal_can_frame_t *frame) {
  if (!mcp || !can_mcp2515_validate_classic_frame(frame)) {
    return false;
  }

  uint32_t id = frame->id;
  if (frame->flags & HAL_CAN_FRAME_EXTENDED) {
    id |= CAN_IS_EXTENDED;
  }
  if (frame->flags & HAL_CAN_FRAME_RTR) {
    id |= CAN_IS_REMOTE_REQUEST;
  }

  uint8_t buf[HAL_CAN_MAX_DATA_LEN] = {};
  if ((frame->flags & HAL_CAN_FRAME_RTR) == 0u && frame->len > 0u) {
    memcpy(buf, frame->data, frame->len);
  }

  bool ok = mcp->sendMsgBuf(id, frame->len, buf) == CAN_OK;
  if (!ok) {
    hal_derr_limited("can", "send frame failed for id=%u", (unsigned)frame->id);
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

bool hal_can_mcp2515_receive_frame(JHMCP2515 *mcp, hal_can_frame_t *frame) {
  if (!mcp || !frame) {
    return false;
  }
  if (mcp->checkReceive() != CAN_MSGAVAIL) {
    return false;
  }

  uint8_t buf[HAL_CAN_MAX_DATA_LEN] = {};
  uint8_t ext = 0u;
  uint8_t msg_len = 0u;
  uint32_t msg_id = 0u;
  bool ok = mcp->readMsgBuf(&msg_id, &ext, &msg_len, buf) == CAN_OK;
  if (!ok) {
    return false;
  }

  memset(frame, 0, sizeof(*frame));
  frame->flags = 0u;
  if ((msg_id & CAN_IS_EXTENDED) != 0u || ext != 0u) {
    frame->flags |= HAL_CAN_FRAME_EXTENDED;
    msg_id &= ~CAN_IS_EXTENDED;
  }
  if ((msg_id & CAN_IS_REMOTE_REQUEST) != 0u) {
    frame->flags |= HAL_CAN_FRAME_RTR;
    msg_id &= ~CAN_IS_REMOTE_REQUEST;
  }
  frame->id = msg_id;
  frame->len = msg_len < HAL_CAN_MAX_DATA_LEN ? msg_len : HAL_CAN_MAX_DATA_LEN;
  frame->dlc = frame->len;
  if (frame->len > 0u) {
    memcpy(frame->data, buf, frame->len);
  }
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

bool hal_can_mcp2515_set_filter(JHMCP2515 *mcp, uint8_t index,
                                const hal_can_filter_t *filter) {
  if (!mcp || index >= HAL_CAN_MAX_FILTERS ||
      !hal_can_validate_filter(filter)) {
    return false;
  }

  const bool ext = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u;
  const uint8_t ext_flag = ext ? 1u : 0u;
  const uint8_t mask_num = index < 2u ? 0u : 1u;
  const uint32_t id = ext ? (filter->id & HAL_CAN_EXT_ID_MASK)
                          : ((filter->id & HAL_CAN_STD_ID_MASK) << 16);
  const uint32_t mask = ext ? (filter->mask & HAL_CAN_EXT_ID_MASK)
                            : ((filter->mask & HAL_CAN_STD_ID_MASK) << 16);

  bool ok = mcp->init_Mask(mask_num, ext_flag, mask) == CAN_OK;
  ok = ok && (mcp->init_Filt(index, ext_flag, id) == CAN_OK);
  return ok;
}

bool hal_can_mcp2515_set_mode(JHMCP2515 *mcp, hal_can_mode_t mode) {
  if (!mcp || !can_mcp2515_mode_valid(mode)) {
    return false;
  }
  bool ok = true;
  if ((mode & HAL_CAN_MODE_ONE_SHOT) != 0u) {
    ok = ok && (mcp->enOneShotTX() == CAN_OK);
  } else {
    ok = ok && (mcp->disOneShotTX() == CAN_OK);
  }
  ok = ok && (mcp->setMode(can_mcp2515_op_mode(mode)) == CAN_OK);
  return ok;
}

bool hal_can_mcp2515_start(JHMCP2515 *mcp, hal_can_mode_t mode) {
  return hal_can_mcp2515_set_mode(mcp, mode);
}

bool hal_can_mcp2515_stop(JHMCP2515 *mcp) {
  if (!mcp) {
    return false;
  }
  (void)mcp->abortTX();
  return mcp->setMode(MODE_CONFIG) == CAN_OK;
}

bool hal_can_mcp2515_get_state(JHMCP2515 *mcp, bool started,
                               hal_can_state_t *state) {
  if (!mcp || !state) {
    return false;
  }
  if (!started) {
    *state = HAL_CAN_STATE_STOPPED;
    return true;
  }
  const uint8_t eflg = mcp->getError();
  if ((eflg & MCP_EFLG_TXBO) != 0u) {
    *state = HAL_CAN_STATE_BUS_OFF;
  } else if ((eflg & (MCP_EFLG_RXEP | MCP_EFLG_TXEP)) != 0u) {
    *state = HAL_CAN_STATE_ERROR_PASSIVE;
  } else if ((eflg & MCP_EFLG_EWARN) != 0u) {
    *state = HAL_CAN_STATE_ERROR_WARNING;
  } else {
    *state = HAL_CAN_STATE_ERROR_ACTIVE;
  }
  return true;
}

bool hal_can_mcp2515_get_error_counters(JHMCP2515 *mcp,
                                        hal_can_error_counters_t *counters) {
  if (!mcp || !counters) {
    return false;
  }
  counters->tx = mcp->errorCountTX();
  counters->rx = mcp->errorCountRX();
  return true;
}

#endif /* HAL_ENABLE_CAN && HAL_ENABLE_MCP2515 */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
