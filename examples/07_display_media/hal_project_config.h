#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#define HAL_ENABLE_ILI9341
#define HAL_DISPLAY_ILI9341
#define HAL_ENABLE_PNG_AS_BASE64
#define HAL_ENABLE_JPEG_AS_BASE64
