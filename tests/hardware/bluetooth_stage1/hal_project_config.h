#pragma once

/* Targets this fixture builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_RP2350_ARM)                                                     \
  X(HAL_TARGET_STM32G474)

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(WIFI_ONLY, "Wi-Fi baseline without the Bluetooth probe",                   \
    JH_BLUETOOTH_STAGE1_WIFI_ONLY = 1)

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#define HAL_ENABLE_WIFI

/* The stage-1 Bluetooth probe, unless the Wi-Fi baseline is built. */
#if !defined(JH_BLUETOOTH_STAGE1_WIFI_ONLY)
#define JH_BLUETOOTH_STAGE1_PROBE
#endif
