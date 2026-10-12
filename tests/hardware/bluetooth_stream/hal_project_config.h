#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(FREERTOS, "FreeRTOS runtime", HAL_ENABLE_FREERTOS)                         \
  X(DISPLAY, "ILI9341 load on SPI1 next to the PIM730 transport",              \
    JHBL5_ENABLE_DISPLAY = 1, HAL_ENABLE_ILI9341, HAL_DISPLAY_ILI9341)         \
  X(DISPLAY_FREERTOS,                                                          \
    "ILI9341 load on SPI1 next to the PIM730 transport, FreeRTOS",             \
    JHBL5_ENABLE_DISPLAY = 1, HAL_ENABLE_ILI9341, HAL_DISPLAY_ILI9341,         \
    HAL_ENABLE_FREERTOS)

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#if defined(HAL_ENABLE_FREERTOS) && !defined(HAL_FREERTOS_TASK0_STACK)
#define HAL_FREERTOS_TASK0_STACK 1024u
#endif

#define HAL_ENABLE_BLE_STREAM

#if defined(JHBL5_ENABLE_DISPLAY) && !JH_HW_NODE_DISPLAY_PRESENT
#error                                                                         \
    "bluetooth_stream DISPLAY: needs the ILI9341 of the nucleo-pim730 assembly"
#endif
