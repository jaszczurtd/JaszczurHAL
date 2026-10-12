#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(WIFI_ONLY, "Wi-Fi baseline without the Bluetooth probe",                   \
    JH_BLUETOOTH_STAGE1_WIFI_ONLY = 1)

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#define HAL_ENABLE_WIFI

/* The stage-1 Bluetooth probe, unless the Wi-Fi baseline is built. The value
 * matches the definition the BTstack build adds, so it is not redefined. */
#if !defined(JH_BLUETOOTH_STAGE1_WIFI_ONLY)
#define JH_BLUETOOTH_STAGE1_PROBE 1
#endif
