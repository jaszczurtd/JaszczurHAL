#include "hal/core/hal_target.h"

#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_CAN) && defined(HAL_ENABLE_MCP2515)

#include "hal/serial/hal_serial.h"
#include "hal_can_mcp2515.h"

#include <new>
#include <string.h>

static_assert(CAN_IS_EXTENDED == HAL_CAN_ID_EXTENDED_FLAG &&
                  CAN_IS_REMOTE_REQUEST == HAL_CAN_ID_RTR_FLAG,
              "the MCP2515 driver flags are the classic CAN API flags");

/* Status of a send, receive or init result code of the driver. */
static hal_status_t mcp2515_status(INT8U code) {
  switch (code) {
  case CAN_OK:
  case CAN_MSGAVAIL:
    return HAL_OK;
  case CAN_NOMSG:
    return HAL_EAGAIN;
  case CAN_GETTXBFTIMEOUT:
    return HAL_EBUSY; /* every transmit buffer stayed taken */
  case CAN_SENDMSGTIMEOUT:
    return HAL_ETIMEOUT; /* the frame did not leave and was aborted */
  default:
    return HAL_EIO; /* CAN_FAILINIT, CAN_FAILTX, CAN_CTRLERROR, CAN_FAIL */
  }
}

/* A mode request (setMode, init_Mask, init_Filt) fails only when CANSTAT did
 * not show the requested mode within the driver's 200 ms. */
static hal_status_t mcp2515_mode_status(INT8U code) {
  return code == MCP2515_OK ? HAL_OK : HAL_ETIMEOUT;
}

static hal_status_t can_mcp2515_map_bitrate(uint32_t bitrate_hz,
                                            uint8_t *out_speed) {
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
    return HAL_EUNSUPPORTED;
  }
  return HAL_OK;
}

static hal_status_t can_mcp2515_map_clock(uint32_t oscillator_hz,
                                          uint8_t *out_clock) {
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
    return HAL_EUNSUPPORTED;
  }
  return HAL_OK;
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

/* One-shot transmission on or off; the driver reads CANCTRL back. */
static hal_status_t mcp2515_set_one_shot(JHMCP2515 *mcp, bool one_shot) {
  return mcp2515_status(one_shot ? mcp->enOneShotTX() : mcp->disOneShotTX());
}

/* Reset, bitrate, normal mode and one-shot TX of a constructed driver. */
static hal_status_t mcp2515_configure(JHMCP2515 *mcp,
                                      const hal_can_mcp2515_config_t *cfg) {
  uint8_t speed = 0u;
  uint8_t clock = 0u;
  hal_status_t st = can_mcp2515_map_bitrate(cfg->bitrate_hz, &speed);
  if (st == HAL_OK) {
    st = can_mcp2515_map_clock(cfg->oscillator_hz, &clock);
  }
  if (st != HAL_OK) {
    hal_derr_limited("can", "unsupported MCP2515 bitrate/clock: %lu/%lu",
                     (unsigned long)cfg->bitrate_hz,
                     (unsigned long)cfg->oscillator_hz);
    return st;
  }

  st = mcp2515_status(mcp->begin(MCP_ANY, speed, clock));
  if (st == HAL_OK) {
    st = mcp2515_mode_status(mcp->setMode(MCP_NORMAL));
  }
  if (st == HAL_OK) {
    st = mcp2515_set_one_shot(mcp, cfg->one_shot_tx);
  }
  if (st != HAL_OK) {
    hal_derr_limited("can", "MCP2515 init failed: %s",
                     hal_status_to_string(st));
    return st;
  }
  mcp->setSleepWakeup(cfg->sleep_wakeup ? 1u : 0u);
  return HAL_OK;
}

/* A transmission and its status; the HAL logs a failure with the id. */
static hal_status_t mcp2515_transmit(JHMCP2515 *mcp, uint32_t id, uint8_t len,
                                     uint8_t *buf, uint32_t log_id) {
  const hal_status_t st = mcp2515_status(mcp->sendMsgBuf(id, len, buf));
  if (st != HAL_OK) {
    hal_derr_limited("can", "send failed for id=%u: %s", (unsigned)log_id,
                     hal_status_to_string(st));
  }
  return st;
}

// ── Provider of the CAN facade
// ─────────────────────────────────────────────
// The facade owns the context storage, checks handles, frames, filters and
// output pointers, and passes a NULL payload only with len 0; the operations
// below rely on that.

static JHMCP2515 *mcp_ctx(void *ctx) { return static_cast<JHMCP2515 *>(ctx); }

static hal_status_t mcp2515_init(void *ctx, const hal_can_config_t *cfg,
                                 jh_can_caps_t *caps, hal_can_mode_t *mode) {
  JHMCP2515 *mcp =
      new (ctx) JHMCP2515(cfg->mcp2515.cs_pin, cfg->mcp2515.spi_bus);
  const hal_status_t st = mcp2515_configure(mcp, &cfg->mcp2515);
  if (st != HAL_OK) {
    mcp->~JHMCP2515();
    return st;
  }
  caps->modes = HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_LISTEN_ONLY |
                HAL_CAN_MODE_ONE_SHOT | HAL_CAN_MODE_SLEEP;
  caps->legacy_filters = HAL_CAN_MAX_FILTERS;
  /* Six filters on two masks, each for standard or extended IDs. */
  caps->public_caps.std_filters = 6u;
  caps->public_caps.ext_filters = 6u;
  caps->public_caps.tx_slots = 3u;
  caps->public_caps.core_clock_hz = cfg->mcp2515.oscillator_hz;
  caps->public_caps.max_nominal_bitrate_hz = 1000000u;
  caps->public_caps.max_data_bitrate_hz = 1000000u;
  *mode =
      cfg->mcp2515.one_shot_tx ? HAL_CAN_MODE_ONE_SHOT : HAL_CAN_MODE_NORMAL;
  return HAL_OK;
}

static void mcp2515_deinit(void *ctx) { mcp_ctx(ctx)->~JHMCP2515(); }

static hal_status_t mcp2515_apply_mode(void *ctx, hal_can_mode_t mode) {
  JHMCP2515 *mcp = mcp_ctx(ctx);
  hal_status_t st =
      mcp2515_set_one_shot(mcp, (mode & HAL_CAN_MODE_ONE_SHOT) != 0u);
  if (st == HAL_OK) {
    st = mcp2515_mode_status(mcp->setMode(can_mcp2515_op_mode(mode)));
  }
  return st;
}

static hal_status_t mcp2515_stop(void *ctx) {
  JHMCP2515 *mcp = mcp_ctx(ctx);
  /* The channel is stopped once the controller is in configuration mode;
   * a buffer still pending after the abort wait is only reported. */
  if (mcp->abortTX() != CAN_OK) {
    hal_derr_limited("can", "MCP2515 transmit abort timed out on stop");
  }
  return mcp2515_mode_status(mcp->setMode(MODE_CONFIG));
}

/* The facade validates the frame and refuses CAN FD on this classic-only
 * controller before it gets here. */
static hal_status_t mcp2515_send_frame(void *ctx,
                                       const hal_can_frame_t *frame) {
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
  return mcp2515_transmit(mcp_ctx(ctx), id, frame->len, buf, frame->id);
}

static hal_status_t mcp2515_receive_frame(void *ctx, hal_can_frame_t *frame) {
  uint8_t buf[HAL_CAN_MAX_DATA_LEN] = {};
  uint8_t ext = 0u;
  uint8_t msg_len = 0u;
  uint32_t msg_id = 0u;
  const hal_status_t st =
      mcp2515_status(mcp_ctx(ctx)->readMsgBuf(&msg_id, &ext, &msg_len, buf));
  if (st != HAL_OK) {
    return st;
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
  return HAL_OK;
}

static hal_status_t mcp2515_available(void *ctx) {
  return mcp2515_status(mcp_ctx(ctx)->checkReceive());
}

static hal_status_t mcp2515_set_filter(void *ctx, uint8_t index,
                                       const hal_can_filter_t *filter) {
  JHMCP2515 *mcp = mcp_ctx(ctx);
  const bool ext = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u;
  const uint8_t ext_flag = ext ? 1u : 0u;
  const uint8_t mask_num = index < 2u ? 0u : 1u;
  const uint32_t id = ext ? (filter->id & HAL_CAN_EXT_ID_MASK)
                          : ((filter->id & HAL_CAN_STD_ID_MASK) << 16);
  const uint32_t mask = ext ? (filter->mask & HAL_CAN_EXT_ID_MASK)
                            : ((filter->mask & HAL_CAN_STD_ID_MASK) << 16);

  hal_status_t st =
      mcp2515_mode_status(mcp->init_Mask(mask_num, ext_flag, mask));
  if (st == HAL_OK) {
    st = mcp2515_mode_status(mcp->init_Filt(index, ext_flag, id));
  }
  return st;
}

static hal_status_t mcp2515_get_state(void *ctx, hal_can_state_t *state) {
  const uint8_t eflg = mcp_ctx(ctx)->getError();
  if ((eflg & MCP_EFLG_TXBO) != 0u) {
    *state = HAL_CAN_STATE_BUS_OFF;
  } else if ((eflg & (MCP_EFLG_RXEP | MCP_EFLG_TXEP)) != 0u) {
    *state = HAL_CAN_STATE_ERROR_PASSIVE;
  } else if ((eflg & MCP_EFLG_EWARN) != 0u) {
    *state = HAL_CAN_STATE_ERROR_WARNING;
  } else {
    *state = HAL_CAN_STATE_ERROR_ACTIVE;
  }
  return HAL_OK;
}

static hal_status_t mcp2515_get_error_counters(void *ctx,
                                               hal_can_error_counters_t *c) {
  JHMCP2515 *mcp = mcp_ctx(ctx);
  c->tx = mcp->errorCountTX();
  c->rx = mcp->errorCountRX();
  return HAL_OK;
}

/* The classic send keeps the HAL_CAN_ID_*_FLAG bits, which are the driver's
 * own flags. */
static hal_status_t mcp2515_legacy_send(void *ctx, uint32_t id, uint8_t len,
                                        const uint8_t *data) {
  uint8_t buf[HAL_CAN_MAX_DATA_LEN];
  const uint8_t safe_len =
      len <= HAL_CAN_MAX_DATA_LEN ? len : HAL_CAN_MAX_DATA_LEN;
  if (safe_len > 0u) {
    memcpy(buf, data, safe_len);
  }
  return mcp2515_transmit(mcp_ctx(ctx), id, safe_len, buf, id);
}

static hal_status_t mcp2515_legacy_receive(void *ctx, uint32_t *id,
                                           uint8_t *len, uint8_t *data) {
  uint8_t buf[HAL_CAN_MAX_DATA_LEN] = {};
  uint8_t msg_len = 0;
  uint32_t msg_id = 0;
  const hal_status_t st =
      mcp2515_status(mcp_ctx(ctx)->readMsgBuf(&msg_id, &msg_len, buf));
  if (st != HAL_OK) {
    return st;
  }

  *id = msg_id;
  *len = msg_len;
  memcpy(data, buf,
         msg_len < HAL_CAN_MAX_DATA_LEN ? msg_len : HAL_CAN_MAX_DATA_LEN);
  return HAL_OK;
}

/* Masks 0 and 1 compare all 11 bits; filters 0, 2, 4 take id0 and 1, 3, 5
 * take id1. The first mode request that fails ends the call. */
static hal_status_t mcp2515_set_std_filters(void *ctx, uint32_t id0,
                                            uint32_t id1) {
  JHMCP2515 *mcp = mcp_ctx(ctx);
  const uint32_t mask = 0x07FF0000UL;
  const uint32_t fid[2] = {(id0 & 0x7FFU) << 16, (id1 & 0x7FFU) << 16};
  hal_status_t st = mcp2515_mode_status(mcp->init_Mask(0, 0, mask));
  if (st == HAL_OK) {
    st = mcp2515_mode_status(mcp->init_Mask(1, 0, mask));
  }
  for (uint8_t num = 0u; st == HAL_OK && num < HAL_CAN_MAX_FILTERS; num++) {
    st = mcp2515_mode_status(mcp->init_Filt(num, 0, fid[num & 1u]));
  }
  return st;
}

const jh_can_provider_t jh_can_mcp2515_provider = {HAL_CAN_BACKEND_MCP2515,
                                                   mcp2515_init,
                                                   mcp2515_deinit,
                                                   mcp2515_apply_mode,
                                                   mcp2515_stop,
                                                   mcp2515_send_frame,
                                                   mcp2515_receive_frame,
                                                   mcp2515_available,
                                                   mcp2515_set_filter,
                                                   mcp2515_get_state,
                                                   mcp2515_get_error_counters,
                                                   mcp2515_legacy_send,
                                                   mcp2515_legacy_receive,
                                                   mcp2515_set_std_filters,
                                                   NULL,
                                                   NULL,
                                                   NULL,
                                                   NULL,
                                                   NULL,
                                                   NULL,
                                                   NULL};

#endif /* HAL_ENABLE_CAN && HAL_ENABLE_MCP2515 */
