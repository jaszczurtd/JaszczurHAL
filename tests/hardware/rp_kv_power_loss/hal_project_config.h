#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

#define HAL_ENABLE_KV

/* Enables only the hardware fixture's controlled flash interruption hook. */
#define JH_RP_FLASH_FAULT_INJECTION
