#pragma once

#include "hal/can/hal_can.h"
#include "mcp251xfd_driver.h"

bool hal_can_mcp251xfd_init(JHMCP251XFD *mcp,
                            const hal_can_mcp251xfd_config_t *cfg);
void hal_can_mcp251xfd_deinit(JHMCP251XFD *mcp);
bool hal_can_mcp251xfd_send(JHMCP251XFD *mcp, uint32_t id, uint8_t len,
                            const uint8_t *data);
bool hal_can_mcp251xfd_receive(JHMCP251XFD *mcp, uint32_t *id, uint8_t *len,
                               uint8_t *data);
bool hal_can_mcp251xfd_available(JHMCP251XFD *mcp);
bool hal_can_mcp251xfd_set_std_filters(JHMCP251XFD *mcp, uint32_t id0,
                                       uint32_t id1);
bool hal_can_mcp251xfd_set_filter(JHMCP251XFD *mcp, uint8_t index,
                                  const hal_can_filter_t *filter);
bool hal_can_mcp251xfd_start(JHMCP251XFD *mcp, hal_can_mode_t mode);
bool hal_can_mcp251xfd_stop(JHMCP251XFD *mcp);
bool hal_can_mcp251xfd_set_mode(JHMCP251XFD *mcp, hal_can_mode_t mode);
bool hal_can_mcp251xfd_get_state(JHMCP251XFD *mcp, bool started,
                                 hal_can_state_t *state);
bool hal_can_mcp251xfd_get_error_counters(JHMCP251XFD *mcp,
                                          hal_can_error_counters_t *counters);
bool hal_can_mcp251xfd_send_frame(JHMCP251XFD *mcp,
                                  const hal_can_frame_t *frame);
bool hal_can_mcp251xfd_receive_frame(JHMCP251XFD *mcp, hal_can_frame_t *frame);
