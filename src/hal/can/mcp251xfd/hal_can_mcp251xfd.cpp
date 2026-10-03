#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_CAN) && defined(HAL_ENABLE_MCP251XFD)

#include "hal/serial/hal_serial.h"
#include "hal_can_mcp251xfd.h"

#include <new>

static JHMCP251XFD *mcp_ctx(void *ctx) {
  return static_cast<JHMCP251XFD *>(ctx);
}

static hal_status_t mcp251xfd_init(void *ctx, const hal_can_config_t *cfg,
                                   jh_can_caps_t *caps, hal_can_mode_t *mode) {
  const hal_can_mcp251xfd_config_t *c = &cfg->mcp251xfd;
  JHMCP251XFD *mcp = new (ctx) JHMCP251XFD(c->cs_pin, c->spi_bus);
  const hal_status_t st = mcp->begin(c);
  if (st != HAL_OK) {
    hal_derr_limited("can", "MCP251XFD init failed: %s",
                     hal_status_to_string(st));
    mcp->~JHMCP251XFD();
    return st;
  }
  caps->modes = HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_EXTERNAL_LOOPBACK |
                HAL_CAN_MODE_LISTEN_ONLY | HAL_CAN_MODE_ONE_SHOT |
                HAL_CAN_MODE_SLEEP |
                (c->enable_fd ? HAL_CAN_MODE_FD : HAL_CAN_MODE_NORMAL);
  caps->legacy_filters = HAL_CAN_MAX_FILTERS;
  /* 32 filters shared by both ID kinds (the last one holds the unmatched
   * policy); a send waits for its frame, so one TXQ object. */
  caps->public_caps.std_filters = 32u;
  caps->public_caps.ext_filters = 32u;
  caps->public_caps.tx_slots = 1u;
  if (c->enable_fd) {
    caps->public_caps.features = HAL_CAN_CAP_FD;
  }
  caps->public_caps.core_clock_hz = mcp->sysclk_hz();
  caps->public_caps.max_nominal_bitrate_hz = 1000000u;
  caps->public_caps.max_data_bitrate_hz = c->enable_fd ? 8000000u : 1000000u;
  *mode = (c->one_shot_tx ? HAL_CAN_MODE_ONE_SHOT : HAL_CAN_MODE_NORMAL) |
          (c->enable_fd ? HAL_CAN_MODE_FD : HAL_CAN_MODE_NORMAL);
  return HAL_OK;
}

static void mcp251xfd_deinit(void *ctx) { mcp_ctx(ctx)->~JHMCP251XFD(); }

static hal_status_t mcp251xfd_apply_mode(void *ctx, hal_can_mode_t mode) {
  return mcp_ctx(ctx)->set_mode(mode);
}

static hal_status_t mcp251xfd_stop(void *ctx) { return mcp_ctx(ctx)->stop(); }

static hal_status_t mcp251xfd_send_frame(void *ctx,
                                         const hal_can_frame_t *frame) {
  return mcp_ctx(ctx)->send_frame(frame);
}

static hal_status_t mcp251xfd_receive_frame(void *ctx, hal_can_frame_t *frame) {
  return mcp_ctx(ctx)->receive_frame(frame);
}

static hal_status_t mcp251xfd_available(void *ctx) {
  return mcp_ctx(ctx)->available();
}

static hal_status_t mcp251xfd_set_filter(void *ctx, uint8_t index,
                                         const hal_can_filter_t *filter) {
  return mcp_ctx(ctx)->set_filter(index, filter);
}

static hal_status_t mcp251xfd_get_state(void *ctx, hal_can_state_t *state) {
  return mcp_ctx(ctx)->get_state(state);
}

static hal_status_t mcp251xfd_get_error_counters(void *ctx,
                                                 hal_can_error_counters_t *c) {
  return mcp_ctx(ctx)->get_error_counters(c);
}

static hal_status_t mcp251xfd_add_filter(void *ctx,
                                         const hal_can_filter_ex_t *filter,
                                         uint8_t *index) {
  return mcp_ctx(ctx)->add_filter(filter, index);
}

static hal_status_t mcp251xfd_remove_filter(void *ctx, uint8_t index) {
  return mcp_ctx(ctx)->remove_filter(index);
}

static hal_status_t mcp251xfd_set_unmatched_policy(void *ctx, bool accept_std,
                                                   bool accept_ext,
                                                   bool accept_rtr) {
  return mcp_ctx(ctx)->set_unmatched_policy(accept_std, accept_ext, accept_rtr);
}

const jh_can_provider_t jh_can_mcp251xfd_provider = {
    HAL_CAN_BACKEND_MCP251XFD,
    mcp251xfd_init,
    mcp251xfd_deinit,
    mcp251xfd_apply_mode,
    mcp251xfd_stop,
    mcp251xfd_send_frame,
    mcp251xfd_receive_frame,
    mcp251xfd_available,
    mcp251xfd_set_filter,
    mcp251xfd_get_state,
    mcp251xfd_get_error_counters,
    NULL, /* legacy_send */
    NULL, /* legacy_receive */
    NULL, /* set_std_filters */
    NULL, /* attach: SPI cannot be read from an interrupt */
    NULL, /* kick_tx */
    NULL, /* get_status */
    NULL, /* recover */
    mcp251xfd_add_filter,
    mcp251xfd_remove_filter,
    mcp251xfd_set_unmatched_policy};

#endif /* HAL_ENABLE_CAN && HAL_ENABLE_MCP251XFD */
