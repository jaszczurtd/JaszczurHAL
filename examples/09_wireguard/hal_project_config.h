#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* The build selects the HAL entry point for RP and STM32 applications. */

#define HAL_ENABLE_WIREGUARD
