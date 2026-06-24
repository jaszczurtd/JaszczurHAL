#pragma once

#include "hal/hal_can.h"
#include "mcp2515_driver.h"

bool hal_can_mcp2515_init(JHMCP2515 *mcp, const hal_can_mcp2515_config_t *cfg);
void hal_can_mcp2515_deinit(JHMCP2515 *mcp);
bool hal_can_mcp2515_send(JHMCP2515 *mcp, uint32_t id, uint8_t len,
                          const uint8_t *data);
bool hal_can_mcp2515_receive(JHMCP2515 *mcp, uint32_t *id, uint8_t *len,
                             uint8_t *data);
bool hal_can_mcp2515_available(JHMCP2515 *mcp);
bool hal_can_mcp2515_set_std_filters(JHMCP2515 *mcp, uint32_t id0,
                                     uint32_t id1);
bool hal_can_mcp2515_set_filter(JHMCP2515 *mcp, uint8_t index,
                                const hal_can_filter_t *filter);
bool hal_can_mcp2515_start(JHMCP2515 *mcp, hal_can_mode_t mode);
bool hal_can_mcp2515_stop(JHMCP2515 *mcp);
bool hal_can_mcp2515_set_mode(JHMCP2515 *mcp, hal_can_mode_t mode);
bool hal_can_mcp2515_get_state(JHMCP2515 *mcp, bool started,
                               hal_can_state_t *state);
bool hal_can_mcp2515_get_error_counters(JHMCP2515 *mcp,
                                        hal_can_error_counters_t *counters);
bool hal_can_mcp2515_send_frame(JHMCP2515 *mcp, const hal_can_frame_t *frame);
bool hal_can_mcp2515_receive_frame(JHMCP2515 *mcp, hal_can_frame_t *frame);
