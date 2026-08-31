/**
 * @file hal_esp32_build_config.cpp
 * @brief Compile-time ESP-IDF and generated board contract validation.
 */

#if defined(JH_ESP_IDF_COMPONENT_BUILD)

#include "hal/core/hal_target.h"
#include "jh_board_config.h"
#include "sdkconfig.h"

#if !HAL_TARGET_IS_ESP32_FAMILY
#error "JaszczurHAL: the ESP-IDF recipe selected a non-ESP32 target."
#endif

#if HAL_TARGET_IS_ESP32 && !defined(CONFIG_IDF_TARGET_ESP32)
#error "JaszczurHAL: IDF_TARGET must match the generated ESP32 target."
#elif HAL_TARGET_IS_ESP32_S3 && !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "JaszczurHAL: IDF_TARGET must match the generated ESP32-S3 target."
#endif

#if !defined(HAL_BOARD_EXPECTED_FLASH_MIB)
#error "JaszczurHAL: ESP-IDF requires whole-MiB board flash capacity."
#endif

#define JH_ESP_IDF_FLASH_SIZE_CONFIG_(size_mib)                                \
  CONFIG_ESPTOOLPY_FLASHSIZE_##size_mib##MB
#define JH_ESP_IDF_FLASH_SIZE_CONFIG(size_mib)                                 \
  JH_ESP_IDF_FLASH_SIZE_CONFIG_(size_mib)

#if !JH_ESP_IDF_FLASH_SIZE_CONFIG(HAL_BOARD_EXPECTED_FLASH_MIB)
#error "JaszczurHAL: sdkconfig flash size differs from the board descriptor."
#endif

#if HAL_BOARD_HAS_PSRAM
#if !defined(CONFIG_SPIRAM)
#error "JaszczurHAL: sdkconfig disables PSRAM declared by the board descriptor."
#elif defined(HAL_BOARD_PSRAM_INTERFACE_QUAD) &&                               \
    !defined(CONFIG_SPIRAM_MODE_QUAD)
#error "JaszczurHAL: sdkconfig PSRAM mode differs from the board descriptor."
#elif defined(HAL_BOARD_PSRAM_INTERFACE_OCTAL) &&                              \
    !defined(CONFIG_SPIRAM_MODE_OCT)
#error "JaszczurHAL: sdkconfig PSRAM mode differs from the board descriptor."
#elif !defined(HAL_BOARD_PSRAM_INTERFACE_QUAD) &&                              \
    !defined(HAL_BOARD_PSRAM_INTERFACE_OCTAL)
#error "JaszczurHAL: the board descriptor uses an unsupported PSRAM interface."
#endif
#elif defined(CONFIG_SPIRAM)
#error "JaszczurHAL: sdkconfig enables PSRAM absent from the board descriptor."
#endif

static_assert(!HAL_BOARD_HAS_PSRAM || HAL_BOARD_PSRAM_BYTES > UINT32_C(0),
              "A board with PSRAM must declare its physical capacity.");
static_assert(
    HAL_TARGET_CPU_CORES == CONFIG_FREERTOS_NUMBER_OF_CORES,
    "ESP-IDF FreeRTOS core count differs from the target descriptor.");

#undef JH_ESP_IDF_FLASH_SIZE_CONFIG
#undef JH_ESP_IDF_FLASH_SIZE_CONFIG_

#endif /* JH_ESP_IDF_COMPONENT_BUILD */
