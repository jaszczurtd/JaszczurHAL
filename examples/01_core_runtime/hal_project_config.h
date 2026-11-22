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

/* The application code runs from RAM: SRAM on RP, CCM SRAM on STM32G474 and
 * IRAM on ESP32. Constants stay in flash. Each variant lists the file that
 * holds its code. */
#if defined(HAL_ENABLE_PULSE_CAPTURE)
#define HAL_RAM_CODE_SOURCES "pulse_capture_app.c"
#else
#define HAL_RAM_CODE_SOURCES "app.c"
#endif
