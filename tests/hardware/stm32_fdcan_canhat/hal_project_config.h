#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

#define HAL_ENABLE_STM32G474_FDCAN

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif
