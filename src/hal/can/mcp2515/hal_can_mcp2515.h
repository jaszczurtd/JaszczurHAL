#pragma once

#include "hal/can/hal_can.h"
#include "hal/can/jh_can_provider.h"
#include "mcp2515_driver.h"

/**
 * @brief MCP2515 provider of the CAN facade; its context is a JHMCP2515.
 *
 * The operations return the facade's statuses: HAL_EBUSY when no transmit
 * buffer became free, HAL_ETIMEOUT when a frame did not leave in time or the
 * controller did not reach a requested mode within 200 ms, HAL_EIO for a
 * failed one-shot attempt or a controller that did not answer, HAL_EAGAIN
 * when no frame waits, and HAL_EUNSUPPORTED from init for a bitrate or
 * crystal without a timing table.
 */
extern const jh_can_provider_t jh_can_mcp2515_provider;
