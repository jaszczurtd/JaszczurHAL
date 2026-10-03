#pragma once

#include "hal/can/hal_can.h"
#include "hal/can/jh_can_provider.h"
#include "mcp251xfd_driver.h"

/** @brief MCP251XFD provider of the CAN facade; its context is a JHMCP251XFD.
 */
extern const jh_can_provider_t jh_can_mcp251xfd_provider;
