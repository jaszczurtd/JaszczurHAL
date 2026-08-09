#pragma once

/**
 * @file hal_board.h
 * @brief Board identity and runtime hardware capabilities.
 *
 * HAL_TARGET_* selects the MCU/ISA. JH_BOARD selects the physical board.
 */

#include "generated/jh_board_registry.h"
#include "hal_status.h"
#include "hal_target.h"
#include <stdint.h>

#if defined(__has_include)
#if __has_include("jh_board_config.h")
#define JH_HAS_GENERATED_BOARD_CONFIG 1
#include "jh_board_config.h"
#endif
#endif

#ifndef JH_HAS_GENERATED_BOARD_CONFIG
#define JH_HAS_GENERATED_BOARD_CONFIG 0
#include "generated/jh_board_fallback_config.h"
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
