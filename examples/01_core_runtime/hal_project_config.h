#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(CAPTURE, "Pulse capture example", HAL_ENABLE_PULSE_CAPTURE = 1)

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* GPIO, timers, system diagnostics, and PID are available without feature
 * flags. */
