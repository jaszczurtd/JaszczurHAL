#pragma once

/**
 * @file hal_board.h
 * @brief Board identity and runtime hardware capabilities.
 *
 * HAL_TARGET_* selects the MCU/ISA. JH_BOARD selects the physical board.
 */

#include "hal/core/hal_layout_check.h"

#include "hal/core/hal_status.h"
#include "hal/core/hal_target.h"
#include "hal/generated/jh_board_registry.h"
#include <stdint.h>

/* Composite board profiles (a base board plus a module) were replaced by
 * device_tree.json assemblies. Without these errors a removed selector would
 * be ignored and the build would quietly get the base board without the
 * module. */
#if defined(HAL_BOARD_PROFILE_RP_PICO_PIM730) ||                               \
    defined(HAL_CYW43_PROFILE_PIM730)
#error                                                                         \
    "JaszczurHAL: the pico-rm2 profile was removed. Describe board pico with a pimoroni,pim730 node in device_tree.json and build that assembly (example: examples/26_ble_stream, assembly pico-rm2)."
#endif
#if defined(HAL_BOARD_PROFILE_STM32G474_NUCLEO_PIM730)
#error                                                                         \
    "JaszczurHAL: the nucleo-g474re-pim730 profile was removed. Describe board nucleo-g474re with a pimoroni,pim730 node in device_tree.json and build that assembly (example: examples/08_mqtt, assembly nucleo-pim730)."
#endif
#if defined(HAL_BOARD_PROFILE_STM32G474_NUCLEO_CANHAT)
#error                                                                         \
    "JaszczurHAL: the nucleo-g474re-canhat profile was removed. Describe board nucleo-g474re with an embeddedgarage,can-fd-hat-g474 node in device_tree.json and build that assembly (example: examples/21_stm32g474_fdcan_native, assembly nucleo-canhat)."
#endif
#if defined(HAL_BOARD_PROFILE_RP_PICO_CORE1262_HF)
#error                                                                         \
    "JaszczurHAL: the pico-core1262-hf profile was removed. Describe board pico with a waveshare,core1262-hf node in device_tree.json and build that assembly (example: examples/27_lora_point_to_point, assembly pico-core1262-hf)."
#endif
#if defined(HAL_BOARD_PROFILE_STM32G474_NUCLEO_CORE1262_HF)
#error                                                                         \
    "JaszczurHAL: the nucleo-g474re-core1262-hf profile was removed. Describe board nucleo-g474re with a waveshare,core1262-hf node in device_tree.json and build that assembly (example: examples/27_lora_point_to_point, assembly nucleo-core1262-hf)."
#endif

#if defined(__has_include)
#if __has_include("jh_board_config.h")
#define JH_HAS_GENERATED_BOARD_CONFIG 1
#include "jh_board_config.h"
#endif
#endif

#ifndef JH_HAS_GENERATED_BOARD_CONFIG
#if defined(JH_HARDWARE_HEADER)
#error                                                                         \
    "a device tree build needs the jh_board_config.h generated with its jh_hardware.h"
#endif
#define JH_HAS_GENERATED_BOARD_CONFIG 0
#include "hal/generated/jh_board_fallback_config.h"
#endif

/* The generated board header of a device tree build checks that it matches
 * jh_hardware.h; a board-only header from another build never stands in. */
#if defined(JH_HARDWARE_HEADER) && !defined(HAL_BOARD_HARDWARE_ID)
#error                                                                         \
    "jh_board_config.h on the include path was not generated from the device tree"
#endif

typedef uint32_t hal_board_capabilities_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Keep the source compatibility alias for generated and fallback profiles. */
#if !defined(LED_BUILTIN) && defined(HAL_LED_BUILTIN)
#define LED_BUILTIN HAL_LED_BUILTIN
#endif

/** @brief Runtime state of one board capability. */
typedef enum {
  HAL_BOARD_CAP_NOT_PRESENT = 0,
  HAL_BOARD_CAP_INACTIVE = 1,
  HAL_BOARD_CAP_AVAILABLE = 2,
  HAL_BOARD_CAP_FAILED = 3
} hal_board_capability_state_t;

/** @brief Snapshot of the selected board and its runtime capabilities. */
typedef struct {
  hal_board_profile_t profile;
  const char *name;
  hal_board_capabilities_t declared;
  hal_board_capabilities_t available;
  hal_board_capabilities_t failed;
} hal_board_info_t;

/** @brief Read a consistent board/runtime-capability snapshot. */
hal_status_t hal_board_get_info(hal_board_info_t *out_info);

/** @brief Query one HAL_BOARD_CAP_* bit. */
hal_status_t
hal_board_get_capability_state(hal_board_capabilities_t capability,
                               hal_board_capability_state_t *out_state);

/**
 * @brief Require a set of runtime capabilities.
 *
 * @return HAL_OK when all are available, HAL_EUNSUPPORTED when the board does
 * not declare one, HAL_EHW when one failed, or HAL_EUNINIT while inactive.
 */
hal_status_t
hal_board_require_capabilities(hal_board_capabilities_t capabilities);

#ifdef __cplusplus
}
#endif
