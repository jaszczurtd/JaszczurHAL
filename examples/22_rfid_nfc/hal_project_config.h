#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#define HAL_ENABLE_MFRC522
#define HAL_ENABLE_PN532
