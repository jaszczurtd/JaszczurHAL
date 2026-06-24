#include "hal/hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474)

#include "hal/hal_config.h"
#if defined(HAL_ENABLE_CAN) && defined(HAL_ENABLE_MCP251XFD)

#include "hal/hal_serial.h"
#include "hal_can_mcp251xfd.h"

#include <string.h>

bool hal_can_mcp251xfd_init(JHMCP251XFD *mcp,
                            const hal_can_mcp251xfd_config_t *cfg) {
  if (!mcp || !cfg) {
    return false;
  }
  if (!mcp->begin(cfg)) {
    hal_derr_limited("can", "MCP251XFD init failed");
    return false;
  }
  return true;
}

void hal_can_mcp251xfd_deinit(JHMCP251XFD *mcp) {
  if (!mcp) {
    return;
  }
  mcp->~JHMCP251XFD();
}

bool hal_can_mcp251xfd_send_frame(JHMCP251XFD *mcp,
                                  const hal_can_frame_t *frame) {
  return mcp && mcp->send_frame(frame);
}

bool hal_can_mcp251xfd_send(JHMCP251XFD *mcp, uint32_t id, uint8_t len,
                            const uint8_t *data) {
  if (!mcp || (len > 0u && data == NULL)) {
    return false;
  }
  if (len > HAL_CAN_MAX_DATA_LEN) {
    len = HAL_CAN_MAX_DATA_LEN;
  }
  hal_can_frame_t frame = {};
  frame.id = id & HAL_CAN_STD_ID_MASK;
  frame.dlc = len;
  frame.len = len;
  if (len > 0u) {
    memcpy(frame.data, data, len);
  }
  return mcp->send_frame(&frame);
}

bool hal_can_mcp251xfd_receive_frame(JHMCP251XFD *mcp, hal_can_frame_t *frame) {
  return mcp && mcp->receive_frame(frame);
}

bool hal_can_mcp251xfd_receive(JHMCP251XFD *mcp, uint32_t *id, uint8_t *len,
                               uint8_t *data) {
  if (!mcp || !id || !len || !data) {
    return false;
  }
  hal_can_frame_t frame = {};
  if (!mcp->receive_frame(&frame)) {
    return false;
  }
  if ((frame.flags & HAL_CAN_FRAME_FD) != 0u ||
      frame.len > HAL_CAN_MAX_DATA_LEN) {
    return false;
  }
  *id = frame.id;
  *len = frame.len;
  if (frame.len > 0u) {
    memcpy(data, frame.data, frame.len);
  }
  return true;
}

bool hal_can_mcp251xfd_available(JHMCP251XFD *mcp) {
  return mcp && mcp->available();
}

bool hal_can_mcp251xfd_set_std_filters(JHMCP251XFD *mcp, uint32_t id0,
                                       uint32_t id1) {
  if (!mcp) {
    return false;
  }
  hal_can_filter_t f0 = {id0 & HAL_CAN_STD_ID_MASK, HAL_CAN_STD_ID_MASK, 0u};
  hal_can_filter_t f1 = {id1 & HAL_CAN_STD_ID_MASK, HAL_CAN_STD_ID_MASK, 0u};
  return mcp->set_filter(0u, &f0) && mcp->set_filter(1u, &f1);
}

bool hal_can_mcp251xfd_set_filter(JHMCP251XFD *mcp, uint8_t index,
                                  const hal_can_filter_t *filter) {
  return mcp && mcp->set_filter(index, filter);
}

bool hal_can_mcp251xfd_start(JHMCP251XFD *mcp, hal_can_mode_t mode) {
  return mcp && mcp->start(mode);
}

bool hal_can_mcp251xfd_stop(JHMCP251XFD *mcp) { return mcp && mcp->stop(); }

bool hal_can_mcp251xfd_set_mode(JHMCP251XFD *mcp, hal_can_mode_t mode) {
  return mcp && mcp->set_mode(mode);
}

bool hal_can_mcp251xfd_get_state(JHMCP251XFD *mcp, bool started,
                                 hal_can_state_t *state) {
  return mcp && mcp->get_state(started, state);
}

bool hal_can_mcp251xfd_get_error_counters(JHMCP251XFD *mcp,
                                          hal_can_error_counters_t *counters) {
  return mcp && mcp->get_error_counters(counters);
}

#endif /* HAL_ENABLE_CAN && HAL_ENABLE_MCP251XFD */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
