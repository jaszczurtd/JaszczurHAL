#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#define HAL_ENABLE_WIFI
/* The value matches the definition the BTstack build adds, so it is not
 * redefined. */
#define JH_BLUETOOTH_CLASSIC_HID_PROBE 1
