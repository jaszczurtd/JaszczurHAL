#pragma once

/**
 * @file hal_board.h
 * @brief Board-profile selection and runtime hardware capabilities.
 *
 * HAL_TARGET_* selects the MCU/ISA. HAL_BOARD_PROFILE_* selects the physical
 * board.
 */

#include "hal_status.h"
#include "hal_target.h"
#include <stdint.h>

#if defined(__has_include)
#if __has_include("jh_board_config.h") && __has_include("jh_board_registry.h")
#define JH_HAS_GENERATED_BOARD_CONFIG 1
#include "jh_board_config.h"
#include "jh_board_registry.h"
#endif
#endif

#ifndef JH_HAS_GENERATED_BOARD_CONFIG
#define JH_HAS_GENERATED_BOARD_CONFIG 0
#endif

typedef uint32_t hal_board_capabilities_t;

#ifdef __cplusplus
extern "C" {
#endif

#if !JH_HAS_GENERATED_BOARD_CONFIG

/* 1. Reject ambiguous legacy CYW43 profiles. */
#if defined(HAL_CYW43_PROFILE_PICO_W)
#error "HAL_CYW43_PROFILE_PICO_W is invalid; use HAL_CYW43_PROFILE_PICOW."
#endif
#if defined(HAL_CYW43_PROFILE_PIM730) && defined(HAL_CYW43_PROFILE_PICOW)
#error "JaszczurHAL: select only one legacy HAL_CYW43_PROFILE_*."
#endif

/* 2. Auto-detect a board when no explicit profile was selected. */
#if !defined(HAL_BOARD_PROFILE_RP_PICO) &&                                     \
    !defined(HAL_BOARD_PROFILE_RP_PICO_W) &&                                   \
    !defined(HAL_BOARD_PROFILE_RP_PICO_2) &&                                   \
    !defined(HAL_BOARD_PROFILE_RP_PICO_2_W) &&                                 \
    !defined(HAL_BOARD_PROFILE_RP_PICO_PIM730) &&                              \
    !defined(HAL_BOARD_PROFILE_STM32G474_GENERIC) &&                           \
    !defined(HAL_BOARD_PROFILE_HOST_MOCK)

#if HAL_TARGET_IS_RP
#if defined(HAL_CYW43_PROFILE_PIM730)
#define HAL_BOARD_PROFILE_RP_PICO_PIM730 1
#elif defined(HAL_CYW43_PROFILE_PICOW)
#if HAL_TARGET_IS_RP2040
#define HAL_BOARD_PROFILE_RP_PICO_W 1
#else
#define HAL_BOARD_PROFILE_RP_PICO_2_W 1
#endif
#elif defined(RASPBERRYPI_PICO2_W)
#define HAL_BOARD_PROFILE_RP_PICO_2_W 1
#elif defined(RASPBERRYPI_PICO2)
#define HAL_BOARD_PROFILE_RP_PICO_2 1
#elif defined(RASPBERRYPI_PICO_W)
#define HAL_BOARD_PROFILE_RP_PICO_W 1
#elif defined(RASPBERRYPI_PICO)
#define HAL_BOARD_PROFILE_RP_PICO 1
#else
#error                                                                         \
    "JaszczurHAL: unknown RP board. Configure through the board generator (JH_BOARD) or define an explicit HAL_BOARD_PROFILE_*."
#endif
#elif HAL_TARGET_IS_STM32G474
#define HAL_BOARD_PROFILE_STM32G474_GENERIC 1
#else
#define HAL_BOARD_PROFILE_HOST_MOCK 1
#endif

#endif

/* 3. Normalize board selectors to 0/1. */
#if defined(HAL_BOARD_PROFILE_RP_PICO)
#define HAL_BOARD_IS_RP_PICO 1
#else
#define HAL_BOARD_IS_RP_PICO 0
#endif

#if defined(HAL_BOARD_PROFILE_RP_PICO_W)
#define HAL_BOARD_IS_RP_PICO_W 1
#else
#define HAL_BOARD_IS_RP_PICO_W 0
#endif

#if defined(HAL_BOARD_PROFILE_RP_PICO_2)
#define HAL_BOARD_IS_RP_PICO_2 1
#else
#define HAL_BOARD_IS_RP_PICO_2 0
#endif

#if defined(HAL_BOARD_PROFILE_RP_PICO_2_W)
#define HAL_BOARD_IS_RP_PICO_2_W 1
#else
#define HAL_BOARD_IS_RP_PICO_2_W 0
#endif

#if defined(HAL_BOARD_PROFILE_RP_PICO_PIM730)
#define HAL_BOARD_IS_RP_PICO_PIM730 1
#else
#define HAL_BOARD_IS_RP_PICO_PIM730 0
#endif

#if defined(HAL_BOARD_PROFILE_STM32G474_GENERIC)
#define HAL_BOARD_IS_STM32G474_GENERIC 1
#else
#define HAL_BOARD_IS_STM32G474_GENERIC 0
#endif

#if defined(HAL_BOARD_PROFILE_HOST_MOCK)
#define HAL_BOARD_IS_HOST_MOCK 1
#else
#define HAL_BOARD_IS_HOST_MOCK 0
#endif

#if (HAL_BOARD_IS_RP_PICO + HAL_BOARD_IS_RP_PICO_W + HAL_BOARD_IS_RP_PICO_2 +  \
     HAL_BOARD_IS_RP_PICO_2_W + HAL_BOARD_IS_RP_PICO_PIM730 +                  \
     HAL_BOARD_IS_STM32G474_GENERIC + HAL_BOARD_IS_HOST_MOCK) != 1
#error "JaszczurHAL: exactly one HAL_BOARD_PROFILE_* must be selected."
#endif

/* 4. Validate target/profile compatibility. */
#if (HAL_BOARD_IS_RP_PICO || HAL_BOARD_IS_RP_PICO_W ||                         \
     HAL_BOARD_IS_RP_PICO_PIM730) &&                                           \
    !HAL_TARGET_IS_RP2040
#error                                                                         \
    "JaszczurHAL: Pico, Pico W and Pico PIM730 profiles require HAL_TARGET_RP2040."
#endif

#if (HAL_BOARD_IS_RP_PICO_2 || HAL_BOARD_IS_RP_PICO_2_W) &&                    \
    !(HAL_TARGET_IS_RP2350_ARM || HAL_TARGET_IS_RP2350_RISCV)
#error "JaszczurHAL: Pico 2 and Pico 2 W profiles require an RP2350 target."
#endif

#if HAL_BOARD_IS_STM32G474_GENERIC && !HAL_TARGET_IS_STM32G474
#error                                                                         \
    "JaszczurHAL: STM32G474 generic board profile requires HAL_TARGET_STM32G474."
#endif

#if HAL_BOARD_IS_HOST_MOCK && !HAL_TARGET_IS_MOCK
#error "JaszczurHAL: host mock board profile requires HAL_TARGET_MOCK."
#endif

#if defined(HAL_CYW43_PROFILE_PIM730) && !HAL_BOARD_IS_RP_PICO_PIM730
#error                                                                         \
    "JaszczurHAL: HAL_CYW43_PROFILE_PIM730 conflicts with the selected board profile."
#endif

#if defined(HAL_CYW43_PROFILE_PICOW) &&                                        \
    !(HAL_BOARD_IS_RP_PICO_W || HAL_BOARD_IS_RP_PICO_2_W)
#error                                                                         \
    "JaszczurHAL: HAL_CYW43_PROFILE_PICOW conflicts with the selected board profile."
#endif

/* 5. Preserve legacy CYW43 pin-profile configuration. */
#if HAL_BOARD_IS_RP_PICO_PIM730 && !defined(HAL_CYW43_PROFILE_PIM730)
#define HAL_CYW43_PROFILE_PIM730 1
#endif

#if (HAL_BOARD_IS_RP_PICO_W || HAL_BOARD_IS_RP_PICO_2_W) &&                    \
    !defined(HAL_CYW43_PROFILE_PICOW)
#define HAL_CYW43_PROFILE_PICOW 1
#endif

/* 6. Compile-time physical board facts. */
#if HAL_BOARD_IS_RP_PICO_W || HAL_BOARD_IS_RP_PICO_2_W ||                      \
    HAL_BOARD_IS_RP_PICO_PIM730
#define HAL_BOARD_HAS_CYW43 1
#else
#define HAL_BOARD_HAS_CYW43 0
#endif

#if HAL_BOARD_IS_RP_PICO || HAL_BOARD_IS_RP_PICO_W ||                          \
    HAL_BOARD_IS_RP_PICO_2 || HAL_BOARD_IS_RP_PICO_2_W ||                      \
    HAL_BOARD_IS_RP_PICO_PIM730
#define HAL_BOARD_HAS_USB_DEVICE 1
#else
#define HAL_BOARD_HAS_USB_DEVICE 0
#endif

#if HAL_BOARD_IS_RP_PICO_PIM730
#define HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND 1
#else
#define HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND 0
#endif

/* 7. Board-owned built-in LED mapping. */
#ifndef HAL_LED_BUILTIN
#if HAL_BOARD_IS_RP_PICO_W || HAL_BOARD_IS_RP_PICO_2_W
#define HAL_LED_BUILTIN 64u
#elif defined(PIN_LED)
#define HAL_LED_BUILTIN PIN_LED
#elif HAL_BOARD_IS_RP_PICO || HAL_BOARD_IS_RP_PICO_2 ||                        \
    HAL_BOARD_IS_RP_PICO_PIM730
#define HAL_LED_BUILTIN 25u
#elif HAL_BOARD_IS_STM32G474_GENERIC
#define HAL_LED_BUILTIN 5u
#endif
#endif

#if !defined(LED_BUILTIN) && defined(HAL_LED_BUILTIN)
#define LED_BUILTIN HAL_LED_BUILTIN
#endif

/** @brief Stable identity of the selected physical board profile. */
typedef enum {
  HAL_BOARD_RP_PICO = 1,
  HAL_BOARD_RP_PICO_W = 2,
  HAL_BOARD_RP_PICO_2 = 3,
  HAL_BOARD_RP_PICO_2_W = 4,
  HAL_BOARD_RP_PICO_PIM730 = 5,
  HAL_BOARD_STM32G474_GENERIC = 6,
  HAL_BOARD_HOST_MOCK = 7
} hal_board_profile_t;

#if HAL_BOARD_IS_RP_PICO
#define HAL_BOARD_PROFILE_ID HAL_BOARD_RP_PICO
#define HAL_BOARD_PROFILE_NAME "pico"
#elif HAL_BOARD_IS_RP_PICO_W
#define HAL_BOARD_PROFILE_ID HAL_BOARD_RP_PICO_W
#define HAL_BOARD_PROFILE_NAME "pico-w"
#elif HAL_BOARD_IS_RP_PICO_2
#define HAL_BOARD_PROFILE_ID HAL_BOARD_RP_PICO_2
#define HAL_BOARD_PROFILE_NAME "pico-2"
#elif HAL_BOARD_IS_RP_PICO_2_W
#define HAL_BOARD_PROFILE_ID HAL_BOARD_RP_PICO_2_W
#define HAL_BOARD_PROFILE_NAME "pico-2-w"
#elif HAL_BOARD_IS_RP_PICO_PIM730
#define HAL_BOARD_PROFILE_ID HAL_BOARD_RP_PICO_PIM730
#define HAL_BOARD_PROFILE_NAME "pico-pim730"
#elif HAL_BOARD_IS_STM32G474_GENERIC
#define HAL_BOARD_PROFILE_ID HAL_BOARD_STM32G474_GENERIC
#define HAL_BOARD_PROFILE_NAME "stm32g474-generic"
#else
#define HAL_BOARD_PROFILE_ID HAL_BOARD_HOST_MOCK
#define HAL_BOARD_PROFILE_NAME "host-mock"
#endif

#define HAL_BOARD_CAP_USB_DEVICE (UINT32_C(1) << 0)
#define HAL_BOARD_CAP_CYW43 (UINT32_C(1) << 1)
#define HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND (UINT32_C(1) << 2)
#define HAL_BOARD_CAP_ALL                                                      \
  (HAL_BOARD_CAP_USB_DEVICE | HAL_BOARD_CAP_CYW43 |                            \
   HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND)

#define HAL_BOARD_DECLARED_CAPABILITIES                                        \
  ((HAL_BOARD_HAS_USB_DEVICE ? HAL_BOARD_CAP_USB_DEVICE : UINT32_C(0)) |       \
   (HAL_BOARD_HAS_CYW43 ? HAL_BOARD_CAP_CYW43 : UINT32_C(0)) |                 \
   (HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND                                      \
        ? HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND                                \
        : UINT32_C(0)))

#endif

/* Generated profiles own HAL_LED_BUILTIN in jh_board_config.h. Keep the
 * source-level compatibility alias available for both generated and fallback
 * board selection paths. */
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
