#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(FREERTOS, "FreeRTOS SMP runtime", HAL_ENABLE_FREERTOS)

#ifndef HAL_ENABLE_SDLOGGER
#define HAL_ENABLE_SDLOGGER
#endif

/* SDLogger opens the card on the bus of the sd node in device_tree.json. */
#define HAL_SDLOGGER_SPI_BUS JH_HW_NODE_SD_BUS_INDEX

#ifndef HAL_SDLOGGER_WRITE_INTERVAL_MS
#define HAL_SDLOGGER_WRITE_INTERVAL_MS 60000u
#endif

#if defined(HAL_ENABLE_FREERTOS) && !defined(HAL_FREERTOS_TASK0_STACK)
#define HAL_FREERTOS_TASK0_STACK 1536u
#endif
