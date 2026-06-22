#pragma once

#include "../../../hal_can.h"
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
