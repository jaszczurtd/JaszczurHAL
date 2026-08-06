#pragma once

/**
 * @file hal_config.h
 * @brief Compile-time configuration facade for JaszczurHAL.
 *
 * This file contains:
 *  1. Application-level feature toggles (formerly libConfig.h).
 *  2. Static-pool size defaults - override with project-level -D flags.
 *  3. Compatibility includes for runtime configuration, assertions and
 *     portable source helpers.
 *
 * Include this from any source that needs library configuration.
 */

#include "hal_compiler.h" /* compiler attributes and builtins */
#include "hal_target.h"   /* exact backend/target selection */
#include "hal_uart_config.h"
#include <stdint.h>

/* ── Application feature toggles ─────────────────────────────────────── */
/* These were previously in libConfig.h.  Override with -D flags or edit
   directly as needed for your project.                                  */

#ifndef SUPPORT_TRANSACTIONS
#define SUPPORT_TRANSACTIONS
#endif

/* Supported EEPROM types */
#define EEPROM_TYPE_AT24C256 1
#define EEPROM_TYPE_RASPBERRY 2
#define EEPROM_TYPE_STM32_FLASH 3
#define EEPROM_TYPE_FLASH 4

#ifndef HAL_EEPROM_TYPE
#if HAL_TARGET_IS_STM32G474
#define HAL_EEPROM_TYPE EEPROM_TYPE_STM32_FLASH
#else
#define HAL_EEPROM_TYPE EEPROM_TYPE_AT24C256
#endif
#endif

#if HAL_EEPROM_TYPE == EEPROM_TYPE_AT24C256
#define AT24C256
#endif

#ifndef EEPROM_I2C_ADDRESS
#define EEPROM_I2C_ADDRESS 0x50
#endif

#ifndef HAL_AT24C256_PAGE_SIZE
#define HAL_AT24C256_PAGE_SIZE 64u
#endif

#ifndef HAL_AT24C256_WRITE_TIMEOUT_US
#define HAL_AT24C256_WRITE_TIMEOUT_US 20000u
#endif

#ifndef HAL_AT24C256_ACK_POLL_US
#define HAL_AT24C256_ACK_POLL_US 100u
#endif

#ifndef HAL_STM32_FLASH_PAGE_SIZE
#define HAL_STM32_FLASH_PAGE_SIZE 2048u
#endif

#ifndef HAL_STM32_FLASH_EEPROM_SIZE
#define HAL_STM32_FLASH_EEPROM_SIZE 4096u
#endif

#ifndef HAL_STM32_FLASH_LITTLEFS_SIZE
#define HAL_STM32_FLASH_LITTLEFS_SIZE 0u
#endif

#ifndef HAL_RP_FLASH_EEPROM_SIZE
#define HAL_RP_FLASH_EEPROM_SIZE 4096u
#endif

#ifndef HAL_RP_FLASH_LITTLEFS_SIZE
#define HAL_RP_FLASH_LITTLEFS_SIZE 0u
#endif

#ifndef HAL_RP_FLASH_TRANSACTION_TIMEOUT_MS
#define HAL_RP_FLASH_TRANSACTION_TIMEOUT_MS 10000u
#endif

#ifndef HAL_RP_OTA_BOOT_SIZE
#define HAL_RP_OTA_BOOT_SIZE 0u
#endif

#ifndef HAL_RP_OTA_PROGRAM_OFFSET
#define HAL_RP_OTA_PROGRAM_OFFSET 0u
#endif

#ifndef HAL_RP_OTA_SLOT_SIZE
#define HAL_RP_OTA_SLOT_SIZE 0u
#endif

#ifndef HAL_RP_OTA_STAGING_OFFSET
#define HAL_RP_OTA_STAGING_OFFSET 0u
#endif

#ifndef HAL_RP_OTA_PHASE_OFFSET
#define HAL_RP_OTA_PHASE_OFFSET 0u
#endif

#ifndef HAL_RP_OTA_SCRATCH_OFFSET
#define HAL_RP_OTA_SCRATCH_OFFSET 0u
#endif

#ifndef HAL_RP_OTA_STATE_A_OFFSET
#define HAL_RP_OTA_STATE_A_OFFSET 0u
#endif

#ifndef HAL_RP_OTA_STATE_B_OFFSET
#define HAL_RP_OTA_STATE_B_OFFSET 0u
#endif

#ifndef HAL_RP_OTA_MAX_BOOT_ATTEMPTS
#define HAL_RP_OTA_MAX_BOOT_ATTEMPTS 3u
#endif

/* Uncomment (or define via -D) to enable optional features:
 *   #define RESET_EEPROM
 *   #define PICO_W     // board/core define; HAL WiFi uses HAL_ENABLE_WIFI
 *   #define HAL_ENABLE_FREERTOS
 */

#include "hal_board.h" /* physical board profile and runtime capabilities */
#define JH_HAL_FEATURE_VERBOSE_REPORT_DEFERRED 1
#include "generated/jh_hal_features.h" /* generated feature closure */
#undef JH_HAL_FEATURE_VERBOSE_REPORT_DEFERRED

#if defined(HAL_ENABLE_BLE)
#if !HAL_BOARD_HAS_BLUETOOTH_CONTROLLER
#error "HAL_ENABLE_BLE requires a board profile with a Bluetooth controller"
#endif
#if !(HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)
#error "HAL_ENABLE_BLE is currently supported on RP2040, STM32G474, and mock"
#endif
#endif

/* -- Network backend transport configuration --------------------------- */
#if defined(HAL_CYW43_BUS_PICO_PIO) && defined(HAL_CYW43_BUS_STM32_GSPI)
#error "JaszczurHAL CYW43 requires exactly one transport"
#endif
#if defined(HAL_CYW43_BUS_STM32_GSPI) && !HAL_TARGET_IS_STM32G474
#error "HAL_CYW43_BUS_STM32_GSPI requires HAL_TARGET_STM32G474"
#endif
#if defined(HAL_CYW43_STACK_LWIP) && !defined(HAL_CYW43_BUS_PICO_PIO) &&       \
    !defined(HAL_CYW43_BUS_STM32_GSPI)
#error "HAL_CYW43_STACK_LWIP requires a JaszczurHAL CYW43 bus"
#endif
#if defined(HAL_NETWORK_BACKEND_CYW43)
#if HAL_TARGET_IS_STM32G474 && !HAL_BOARD_HAS_CYW43
#error                                                                         \
    "HAL_NETWORK_BACKEND_CYW43 on STM32G474 requires a board profile with CYW43"
#endif
#if (defined(HAL_CYW43_BUS_PICO_PIO) + defined(HAL_CYW43_BUS_STM32_GSPI)) != 1
#error                                                                         \
    "HAL_NETWORK_BACKEND_CYW43 requires exactly one HAL_CYW43_BUS_* transport"
#endif
#if HAL_TARGET_IS_RP
#if !defined(HAL_CYW43_BUS_PICO_PIO)
#error "HAL_NETWORK_BACKEND_CYW43 on RP targets requires HAL_CYW43_BUS_PICO_PIO"
#endif
#if !defined(HAL_CYW43_STACK_LWIP)
#error "HAL_NETWORK_BACKEND_CYW43 on RP targets requires HAL_CYW43_STACK_LWIP"
#endif
#elif HAL_TARGET_IS_STM32G474
#if !defined(HAL_CYW43_BUS_STM32_GSPI)
#error                                                                         \
    "HAL_NETWORK_BACKEND_CYW43 on STM32G474 requires HAL_CYW43_BUS_STM32_GSPI"
#endif
#if !defined(HAL_CYW43_STACK_LWIP)
#error "HAL_NETWORK_BACKEND_CYW43 on STM32G474 requires HAL_CYW43_STACK_LWIP"
#endif
#else
#error "HAL_NETWORK_BACKEND_CYW43 is unsupported on the selected target"
#endif
#endif

#if defined(HAL_CYW43_PROFILE_PIM730)
#ifndef HAL_CYW43_PIN_WL_ON
#define HAL_CYW43_PIN_WL_ON 2u
#endif
#ifndef HAL_CYW43_PIN_CHIP_SELECT
#define HAL_CYW43_PIN_CHIP_SELECT 3u
#endif
#ifndef HAL_CYW43_PIN_DATA
#define HAL_CYW43_PIN_DATA 4u
#endif
#ifndef HAL_CYW43_PIN_CLOCK
#define HAL_CYW43_PIN_CLOCK 5u
#endif
/* The PIO divider is derived from the live clk_sys by the RP transport. */
#ifndef HAL_CYW43_GSPI_TARGET_HZ
#define HAL_CYW43_GSPI_TARGET_HZ 31250000u
#endif
#endif

#if defined(HAL_CYW43_PROFILE_PICOW)
#ifndef HAL_CYW43_PIN_WL_ON
#define HAL_CYW43_PIN_WL_ON 23u
#endif
#ifndef HAL_CYW43_PIN_CHIP_SELECT
#define HAL_CYW43_PIN_CHIP_SELECT 25u
#endif
#ifndef HAL_CYW43_PIN_DATA
#define HAL_CYW43_PIN_DATA 24u
#endif
#ifndef HAL_CYW43_PIN_CLOCK
#define HAL_CYW43_PIN_CLOCK 29u
#endif
/* Keep the validated gSPI rate independent of RP2040/RP2350 clk_sys. */
#ifndef HAL_CYW43_GSPI_TARGET_HZ
#define HAL_CYW43_GSPI_TARGET_HZ 31250000u
#endif
#endif

#if defined(HAL_CYW43_PIO_CLOCK_DIV_INT) !=                                    \
    defined(HAL_CYW43_PIO_CLOCK_DIV_FRAC8)
#error                                                                         \
    "HAL_CYW43_PIO_CLOCK_DIV_INT and HAL_CYW43_PIO_CLOCK_DIV_FRAC8 must be set together"
#endif

#if defined(HAL_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256) &&                          \
    defined(HAL_CYW43_PIO_CLOCK_DIV_INT)
#error                                                                         \
    "Select either HAL_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256 or the legacy INT/FRAC8 pair"
#endif

#if defined(HAL_CYW43_PIO_CLOCK_DIV_INT)
#define HAL_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256                                  \
  ((HAL_CYW43_PIO_CLOCK_DIV_INT) * 256u + (HAL_CYW43_PIO_CLOCK_DIV_FRAC8))
#elif !defined(HAL_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256)
#define HAL_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256 0u
#endif

#if defined(HAL_NETWORK_BACKEND_CYW43) && HAL_TARGET_IS_RP &&                  \
    HAL_BOARD_HAS_CYW43 &&                                                     \
    (!defined(HAL_CYW43_PIN_WL_ON) || !defined(HAL_CYW43_PIN_CHIP_SELECT) ||   \
     !defined(HAL_CYW43_PIN_DATA) || !defined(HAL_CYW43_PIN_CLOCK) ||          \
     !defined(HAL_CYW43_GSPI_TARGET_HZ) ||                                     \
     !defined(HAL_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256) ||                        \
     !defined(HAL_CYW43_MAX_TRANSACTION_BYTES))
#error                                                                         \
    "HAL_NETWORK_BACKEND_CYW43 requires a complete CYW43 board pin/clock profile"
#endif

#if defined(HAL_NETWORK_BACKEND_CYW43) && HAL_TARGET_IS_STM32G474 &&           \
    (!defined(HAL_CYW43_PIN_WL_ON) || !defined(HAL_CYW43_PIN_CHIP_SELECT) ||   \
     !defined(HAL_CYW43_PIN_DATA) || !defined(HAL_CYW43_PIN_CLOCK) ||          \
     !defined(HAL_CYW43_MAX_TRANSACTION_BYTES))
#error                                                                         \
    "HAL_NETWORK_BACKEND_CYW43 on STM32G474 requires a complete CYW43 gSPI profile"
#endif

#ifndef HAL_CYW43_COUNTRY_CODE
#define HAL_CYW43_COUNTRY_CODE CYW43_COUNTRY_WORLDWIDE
#endif

/* -- Application entry opt-ins ----------------------------------------- */
/* HAL_ENABLE_APP_TASK1 controls the optional app_task1 dispatch path when
   HAL_PROVIDE_APP_ENTRY is enabled. On the RP family this starts the core-1
   path through Pico SDK multicore startup, so it is intentionally explicit. */

/* -- FreeRTOS opt-in ---------------------------------------------------- */
/* HAL_ENABLE_FREERTOS is a configuration flag for native FreeRTOS support.
   It does not add a public hal_rtos_* wrapper. Applications use the native
   FreeRTOS headers/API directly, while HAL internals select RTOS-safe paths
   where each target has implemented them.

   Current target status:
     - RP targets link the pinned local FreeRTOS-Kernel and the matching
       RP2040/RP2350 SMP port. HAL owns scheduler startup,
       app_task0 runs on core 0, and optional app_task1 runs on core 1.
     - STM32G474 must have the local FreeRTOS-Kernel include path configured.
       STM32 CMake builds compile the Cortex-M4F port, heap_4, and kernel
       source list when FreeRTOS mode is enabled. hal_mutex_*,
       hal_delay_ms(), and hal_idle() are FreeRTOS-aware; hard
       hal_critical_section_* still masks interrupts for timing-sensitive
       code. With HAL_PROVIDE_APP_ENTRY, app_task0() and optional app_task1()
       are created as FreeRTOS tasks before vTaskStartScheduler().
   HAL-provided native FreeRTOS entry defaults:
     - HAL_FREERTOS_TASK0_STACK: 512 FreeRTOS stack words
     - HAL_FREERTOS_HEAP_SIZE: target-specific FreeRTOS heap size in bytes
     - HAL_FREERTOS_TASK1_STACK: 512 FreeRTOS stack words
     - HAL_FREERTOS_TASK0_PRIORITY: tskIDLE_PRIORITY + 1
     - HAL_FREERTOS_TASK1_PRIORITY: tskIDLE_PRIORITY + 1

   Platform stack-size override helpers:
     - HAL_STM32_MAIN_STACK_SIZE: bytes; overrides STM32 linker
       _Min_Stack_Size reserve (default 0x800)
     - HAL_RP2040_STACK_SIZE: bytes; mapped to PICO_STACK_SIZE
     - HAL_RP2040_CORE1_STACK_SIZE: bytes; mapped to
       PICO_CORE1_STACK_SIZE

   No default runtime behavior changes when HAL_ENABLE_FREERTOS is undefined. */

#ifdef HAL_ENABLE_FREERTOS
#if HAL_TARGET_IS_RP
#ifndef __FREERTOS
#error                                                                         \
    "JaszczurHAL: HAL_ENABLE_FREERTOS on RP requires the local FreeRTOS port and __FREERTOS build definition."
#endif
#if defined(__has_include)
#if !__has_include(<FreeRTOS.h>)
#error                                                                         \
    "JaszczurHAL: native RP FreeRTOS requires the local third_party/FreeRTOS-Kernel include path."
#endif
#if !__has_include("FreeRTOSConfig.h")
#error "JaszczurHAL: native RP FreeRTOS requires its target FreeRTOSConfig.h."
#endif
#endif
#elif HAL_TARGET_IS_STM32G474
#if defined(__has_include)
#if !__has_include(<FreeRTOS.h>)
#error                                                                         \
    "JaszczurHAL: HAL_ENABLE_FREERTOS on STM32G474 requires the local third_party/FreeRTOS-Kernel include path to provide <FreeRTOS.h>."
#endif
#if !__has_include("FreeRTOSConfig.h")
#error                                                                         \
    "JaszczurHAL: HAL_ENABLE_FREERTOS on STM32G474 requires a target FreeRTOSConfig.h on the include path."
#endif
#else
#error                                                                         \
    "JaszczurHAL: HAL_ENABLE_FREERTOS on STM32G474 requires compiler support for __has_include so the FreeRTOS-Kernel and FreeRTOSConfig.h paths can be validated."
#endif
#else
#error                                                                         \
    "JaszczurHAL: HAL_ENABLE_FREERTOS is supported only for RP targets or HAL_TARGET_STM32G474."
#endif
#endif

/* ── Module enable flags (opt-in) ────────────────────────────────────── */
/* JaszczurHAL uses an opt-in model: only explicitly enabled modules and their
   propagated dependencies are compiled. The maintained public flag catalog is
   in doc/api/02_module_flags.md; doc/HAL_FLAGS.txt provides a concise summary.
   The generated feature registry resolves unconditional dependencies. */

/* Conditional residual outside feature registry v1. */
#if defined(HAL_ENABLE_EEPROM) && (HAL_EEPROM_TYPE == EEPROM_TYPE_AT24C256)
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

/* -- Derived network core and exactly-one backend selection ------------ */
/* Feature flags request behavior.  They do not select a radio or socket
 * implementation.  A target may provide a compatibility default, but an
 * explicit HAL_NETWORK_BACKEND_* definition always remains authoritative. */
#if defined(HAL_ENABLE_NETWORK_CORE) && !defined(HAL_NETWORK_BACKEND_CYW43) && \
    !defined(HAL_NETWORK_BACKEND_MOCK) && !defined(HAL_NETWORK_BACKEND_ESP_AT)
#if HAL_TARGET_IS_MOCK
#define HAL_NETWORK_BACKEND_MOCK 1
#endif
#endif

#if defined(HAL_NETWORK_BACKEND_CYW43)
#define HAL_NETWORK_BACKEND_CYW43_SELECTED 1
#else
#define HAL_NETWORK_BACKEND_CYW43_SELECTED 0
#endif
#if defined(HAL_NETWORK_BACKEND_MOCK)
#define HAL_NETWORK_BACKEND_MOCK_SELECTED 1
#else
#define HAL_NETWORK_BACKEND_MOCK_SELECTED 0
#endif
#if defined(HAL_NETWORK_BACKEND_ESP_AT)
#define HAL_NETWORK_BACKEND_ESP_AT_SELECTED 1
#else
#define HAL_NETWORK_BACKEND_ESP_AT_SELECTED 0
#endif

#define HAL_NETWORK_BACKEND_SELECTION_COUNT                                    \
  (HAL_NETWORK_BACKEND_CYW43_SELECTED + HAL_NETWORK_BACKEND_MOCK_SELECTED +    \
   HAL_NETWORK_BACKEND_ESP_AT_SELECTED)

#if defined(HAL_ENABLE_NETWORK_CORE) && HAL_NETWORK_BACKEND_SELECTION_COUNT == 0
#error "JaszczurHAL network core requires exactly one HAL_NETWORK_BACKEND_*"
#endif
#if HAL_NETWORK_BACKEND_SELECTION_COUNT > 1
#error "Select exactly one HAL_NETWORK_BACKEND_*"
#endif

#if defined(HAL_NETWORK_BACKEND_MOCK) && !HAL_TARGET_IS_MOCK
#error "HAL_NETWORK_BACKEND_MOCK requires HAL_TARGET_MOCK"
#endif
#if defined(HAL_NETWORK_BACKEND_ESP_AT) && !defined(HAL_ESP_AT_PROFILE_ESP8266)
#error                                                                         \
    "HAL_NETWORK_BACKEND_ESP_AT requires an HAL_ESP_AT_PROFILE_* command profile"
#endif

/* Backend topology/capability flags consumed by common network code. */
#if defined(HAL_NETWORK_BACKEND_CYW43)
#define HAL_NETWORK_CORE_HAS_WIFI_CONTROL 1
#define HAL_NETWORK_CORE_HAS_RESOLVER 1
#define HAL_NETWORK_CORE_HAS_TCP_CLIENT 1
#define HAL_NETWORK_CORE_HAS_TCP_LISTENER 1
#define HAL_NETWORK_CORE_HAS_UDP 1
#define HAL_NETWORK_CORE_HAS_HOST_STACK_L3 1
#define HAL_NETWORK_CORE_HAS_VIRTUAL_NETIF_ROUTE 1
#define HAL_NETWORK_CORE_HAS_STACK_CONTEXT 1
#define HAL_NETWORK_CORE_HAS_SECURE_ENTROPY 1
#elif defined(HAL_NETWORK_BACKEND_MOCK)
#define HAL_NETWORK_CORE_HAS_WIFI_CONTROL 1
#define HAL_NETWORK_CORE_HAS_RESOLVER 1
#define HAL_NETWORK_CORE_HAS_TCP_CLIENT 1
#define HAL_NETWORK_CORE_HAS_TCP_LISTENER 1
#define HAL_NETWORK_CORE_HAS_UDP 1
#define HAL_NETWORK_CORE_HAS_HOST_STACK_L3 1
#define HAL_NETWORK_CORE_HAS_VIRTUAL_NETIF_ROUTE 1
#define HAL_NETWORK_CORE_HAS_STACK_CONTEXT 1
#define HAL_NETWORK_CORE_HAS_SECURE_ENTROPY 1
#elif defined(HAL_NETWORK_BACKEND_ESP_AT)
#define HAL_NETWORK_CORE_HAS_WIFI_CONTROL 1
#define HAL_NETWORK_CORE_HAS_RESOLVER 1
#define HAL_NETWORK_CORE_HAS_TCP_CLIENT 1
#define HAL_NETWORK_CORE_HAS_TCP_LISTENER 1
#define HAL_NETWORK_CORE_HAS_UDP 1
#define HAL_NETWORK_CORE_HAS_HOST_STACK_L3 0
#define HAL_NETWORK_CORE_HAS_VIRTUAL_NETIF_ROUTE 0
#define HAL_NETWORK_CORE_HAS_STACK_CONTEXT 0
#define HAL_NETWORK_CORE_HAS_SECURE_ENTROPY 0
#endif

#if defined(HAL_ENABLE_WIFI) && !HAL_NETWORK_CORE_HAS_WIFI_CONTROL
#error "HAL_ENABLE_WIFI requires backend WiFi-control capability"
#endif
#if defined(HAL_ENABLE_TCP) &&                                                 \
    (!HAL_NETWORK_CORE_HAS_TCP_CLIENT || !HAL_NETWORK_CORE_HAS_TCP_LISTENER)
#error "HAL_ENABLE_TCP requires backend TCP client and listener capabilities"
#endif
#if defined(HAL_ENABLE_UDP) && !HAL_NETWORK_CORE_HAS_UDP
#error "HAL_ENABLE_UDP requires backend UDP capability"
#endif
#if defined(HAL_ENABLE_NETWORK_CORE) && !HAL_NETWORK_CORE_HAS_RESOLVER
#error "The selected network backend must provide resolver capability"
#endif
#if defined(HAL_ENABLE_WIREGUARD) &&                                           \
    (!HAL_NETWORK_CORE_HAS_HOST_STACK_L3 ||                                    \
     !HAL_NETWORK_CORE_HAS_VIRTUAL_NETIF_ROUTE ||                              \
     !HAL_NETWORK_CORE_HAS_STACK_CONTEXT ||                                    \
     !HAL_NETWORK_CORE_HAS_SECURE_ENTROPY)
#error                                                                         \
    "HAL_ENABLE_WIREGUARD requires a host-stack L3 backend with stack-context and virtual-netif/route capabilities"
#endif

/* GPS needs a serial transport but is not tied to a specific one: it can be
   fed from a hardware UART (hal_uart) or SoftwareSerial (hal_swserial). The
   caller enables whichever the wiring uses. If neither is selected, GPS
   defaults to the hardware UART, which exists on every target (RP2040,
   STM32G474, mock). We deliberately default to UART rather than SoftwareSerial
   so GPS never drags SoftwareSerial onto targets that lack it; a caller wiring
   GPS to SoftwareSerial simply enables HAL_ENABLE_SWSERIAL explicitly. */
/* Provider-default residual outside feature registry v1. */
#ifdef HAL_ENABLE_GPS
#if !defined(HAL_ENABLE_UART) && !defined(HAL_ENABLE_SWSERIAL)
#define HAL_ENABLE_UART
#endif
#endif

/* ── Contextual consistency checks outside feature registry v1 ──── */
/* The registry's simple conjunctive `requires` relation cannot express these
  provider/transport choices or the target-dependent FDCAN constraint. */
/* Standalone modules (WIFI, I2C, I2C_SLAVE, SPI, SWSERIAL, UART, EEPROM,
  KV, SDLOGGER, DACLESS, DMA_PWM_AUDIO, PWM_FREQ, RGB_LED, DS18B20, DHT,
  BH1750, TSC2007, STMPE610, ONEWIRE, EXTERNAL_ADC, MCP3221, MCP23017,
  PCA9654E, PCF8574, HC595, MCP4725, PGA2311, SSD1331, SSD135X, ST7567, TIME,
  UNITY, MQTT, UDP, TCP,
  HTTP_SERVER, HTTP_FILES, WEBSOCKET, NET_CONSOLE, NET_COMMANDS, OTA, WIREGUARD,
  LITTLEFS, CRYPTO, CJSON, PNG, PNG_AS_BASE64, JPEG, JPEG_AS_BASE64) do NOT need
  such checks - they can be enabled on their own. The checks below only catch
  generic-API modules enabled without any backend, which would otherwise leave
  the user with a non-functional binary. */

#if defined(HAL_ENABLE_RTC) && !defined(HAL_ENABLE_PCF8563) &&                 \
    !defined(HAL_ENABLE_DS3231)
#error                                                                         \
    "HAL_ENABLE_RTC requires at least one backend: HAL_ENABLE_PCF8563 or HAL_ENABLE_DS3231"
#endif

#if defined(HAL_ENABLE_CELLULAR_MODEM) && !defined(HAL_ENABLE_A7670)
#error                                                                         \
    "HAL_ENABLE_CELLULAR_MODEM requires at least one backend: HAL_ENABLE_A7670"
#endif

#if defined(HAL_ENABLE_THERMOCOUPLE) && !defined(HAL_ENABLE_MCP9600) &&        \
    !defined(HAL_ENABLE_MAX6675)
#error                                                                         \
    "HAL_ENABLE_THERMOCOUPLE requires at least one backend: HAL_ENABLE_MCP9600 or HAL_ENABLE_MAX6675"
#endif

#if defined(HAL_ENABLE_STM32G474_FDCAN) && !HAL_TARGET_IS_STM32G474
#error "HAL_ENABLE_STM32G474_FDCAN is only valid with HAL_TARGET_STM32G474"
#endif

#if defined(HAL_ENABLE_CAN) && !defined(HAL_ENABLE_MCP2515) &&                 \
    !defined(HAL_ENABLE_MCP251XFD) && !defined(HAL_ENABLE_STM32G474_FDCAN)
#error                                                                         \
    "HAL_ENABLE_CAN requires at least one backend: HAL_ENABLE_MCP2515, HAL_ENABLE_MCP251XFD, or HAL_ENABLE_STM32G474_FDCAN"
#endif

#if defined(HAL_ENABLE_DIGIPOT) && !defined(HAL_ENABLE_MCP401X) &&             \
    !defined(HAL_ENABLE_MAX5395)
#error                                                                         \
    "HAL_ENABLE_DIGIPOT requires at least one backend: HAL_ENABLE_MCP401X or HAL_ENABLE_MAX5395"
#endif

#if defined(HAL_ENABLE_GPS) && !defined(HAL_ENABLE_SWSERIAL) &&                \
    !defined(HAL_ENABLE_UART)
#error                                                                         \
    "HAL_ENABLE_GPS requires a serial transport: HAL_ENABLE_SWSERIAL or HAL_ENABLE_UART"
#endif

#if defined(HAL_ENABLE_DISPLAY) && !defined(HAL_ENABLE_TFT) &&                 \
    !defined(HAL_ENABLE_SSD1306) && !defined(HAL_ENABLE_SSD1331) &&            \
    !defined(HAL_ENABLE_SSD135X) && !defined(HAL_ENABLE_ST7567) &&             \
    !defined(HAL_ENABLE_SSD16XX) && !defined(HAL_ENABLE_UC81XX)
#error "HAL_ENABLE_DISPLAY requires a TFT, OLED, LCD, or EPD backend"
#endif

#if defined(HAL_ENABLE_TFT) && !defined(HAL_ENABLE_ILI9341) &&                 \
    !defined(HAL_ENABLE_ST7789) && !defined(HAL_ENABLE_ST7735) &&              \
    !defined(HAL_ENABLE_ST7796S) && !defined(HAL_ENABLE_GC9A01)
#error                                                                         \
    "HAL_ENABLE_TFT requires at least one driver: HAL_ENABLE_ILI9341 / HAL_ENABLE_ST7789 / HAL_ENABLE_ST7735 / HAL_ENABLE_ST7796S / HAL_ENABLE_GC9A01"
#endif

#include "generated/jh_hal_features.h" /* final feature report */

/* Keep source-compatibility helpers available through hal_config.h. */
#include "hal_compat.h"

/* ── GPS UART frame config ────────────────────────────────────────────── */

/**
 * @def HAL_GPS_DEFAULT_UART_CONFIG
 * @brief Default UART frame format passed to hal_gps_init().
 *
 * The NMEA 0183 standard specifies 8N1.  hal_gps_init() will automatically
 * try the alternate framing (8N1↔7N1) if all NMEA checksums fail after
 * the first ~500 received characters, so the default is safe for both
 * genuine and clone modules.
 *
 * Override this define to force a specific framing if needed:
 * @code
 *   #define HAL_GPS_DEFAULT_UART_CONFIG HAL_UART_CFG_7N1
 * @endcode
 */
#ifndef HAL_GPS_DEFAULT_UART_CONFIG
#define HAL_GPS_DEFAULT_UART_CONFIG HAL_UART_CFG_8N1
#endif

/* ── Pool-size compile-time defaults ─────────────────────────────────── */
/* These define the MAXIMUM static array size.  Runtime configuration
   (hal_setup) can lower the effective limit but never exceed it.         */

/**
 * @def HAL_PWM_FREQ_MAX_CHANNELS
 * Maximum number of frequency-controlled PWM channels that can exist
 * simultaneously.  Each slot occupies ~24 B.  RP2040 has 8 PWM slices
 * (16 channels total), so 8 is usually enough.
 */
#ifndef HAL_PWM_FREQ_MAX_CHANNELS
#define HAL_PWM_FREQ_MAX_CHANNELS 8
#endif

/**
 * @def HAL_DMA_PWM_AUDIO_MAX_CHANNELS
 * Maximum number of timer-paced PWM-audio DMA handles.
 */
#ifndef HAL_DMA_PWM_AUDIO_MAX_CHANNELS
#define HAL_DMA_PWM_AUDIO_MAX_CHANNELS 4
#endif

/**
 * @def HAL_CAN_MAX_INSTANCES
 * Maximum number of CAN-bus controller interfaces. One instance per physical
 * controller, whether it is native or attached over SPI.
 */
#ifndef HAL_CAN_MAX_INSTANCES
#define HAL_CAN_MAX_INSTANCES 2
#endif

/**
 * @def HAL_UDP_SOCKET_MAX_INSTANCES
 * Maximum number of handle-based UDP sockets.
 * The legacy single-socket hal_udp_* API uses one default socket from this
 * pool.
 */
#ifndef HAL_UDP_SOCKET_MAX_INSTANCES
#define HAL_UDP_SOCKET_MAX_INSTANCES 4u
#endif

/**
 * @def HAL_TCP_SOCKET_MAX_INSTANCES
 * Maximum number of TCP client socket handles.
 */
#ifndef HAL_TCP_SOCKET_MAX_INSTANCES
#define HAL_TCP_SOCKET_MAX_INSTANCES 4u
#endif

/**
 * @def HAL_TCP_LISTENER_MAX_INSTANCES
 * Maximum number of TCP listener/server handles.
 */
#ifndef HAL_TCP_LISTENER_MAX_INSTANCES
#define HAL_TCP_LISTENER_MAX_INSTANCES 2u
#endif

/**
 * @def HAL_TCP_LISTENER_BACKLOG_MAX
 * Maximum pending-connection backlog stored by the portable TCP listener
 * layer. Backends may also impose their own platform limit.
 */
#ifndef HAL_TCP_LISTENER_BACKLOG_MAX
#define HAL_TCP_LISTENER_BACKLOG_MAX 5u
#endif
#if HAL_TCP_LISTENER_BACKLOG_MAX < 1u
#error "HAL_TCP_LISTENER_BACKLOG_MAX must be at least 1"
#endif

/**
 * @def HAL_BSD_SOCKET_MAX_FDS
 * Maximum number of file descriptors reserved for the BSD socket adapter.
 */
#ifndef HAL_BSD_SOCKET_MAX_FDS
#define HAL_BSD_SOCKET_MAX_FDS 8u
#endif

/** Maximum number of concurrent portable TLS client handles. */
#ifndef HAL_TLS_MAX_CLIENTS
#define HAL_TLS_MAX_CLIENTS 2u
#endif

/** Maximum number of CA trust anchors retained by one TLS client. */
#ifndef HAL_TLS_MAX_TRUST_ANCHORS
#define HAL_TLS_MAX_TRUST_ANCHORS 4u
#endif

/** Reject clocks older than 2020-01-01 UTC. */
#ifndef HAL_TLS_MIN_VALID_UNIX_TIME
#define HAL_TLS_MIN_VALID_UNIX_TIME 1577836800ULL
#endif

/** Maximum DNS hostname length retained for SNI and identity verification. */
#ifndef HAL_TLS_HOSTNAME_MAX_LENGTH
#define HAL_TLS_HOSTNAME_MAX_LENGTH 253u
#endif

/** Finite timeout used by the bounded-worker BearSSL BSD callbacks. */
#ifndef HAL_TLS_DEFAULT_TRANSPORT_TIMEOUT_MS
#define HAL_TLS_DEFAULT_TRANSPORT_TIMEOUT_MS 5000u
#endif

/** Default upper bound for a complete public TLS operation. */
#ifndef HAL_TLS_DEFAULT_OPERATION_TIMEOUT_MS
#define HAL_TLS_DEFAULT_OPERATION_TIMEOUT_MS 15000u
#endif

/** Maximum record transport actions performed by one poll call. */
#ifndef HAL_TLS_DEFAULT_POLL_STEP_BUDGET
#define HAL_TLS_DEFAULT_POLL_STEP_BUDGET 4u
#endif
#if HAL_TLS_MAX_CLIENTS < 1u
#error "HAL_TLS_MAX_CLIENTS must be at least 1"
#endif
#if HAL_TLS_MAX_TRUST_ANCHORS < 1u
#error "HAL_TLS_MAX_TRUST_ANCHORS must be at least 1"
#endif
#if HAL_TLS_HOSTNAME_MAX_LENGTH < 1u
#error "HAL_TLS_HOSTNAME_MAX_LENGTH must be at least 1"
#endif
#if HAL_TLS_DEFAULT_TRANSPORT_TIMEOUT_MS < 1u ||                               \
    HAL_TLS_DEFAULT_OPERATION_TIMEOUT_MS < 1u ||                               \
    HAL_TLS_DEFAULT_POLL_STEP_BUDGET < 1u
#error "Default TLS time and poll budgets must be finite and non-zero"
#endif

/**
 * @def HAL_BSD_SOCKET_FD_BASE
 * First descriptor number used by the BSD socket adapter. Keeping socket
 * descriptors away from 0, 1, and 2 avoids collisions with standard streams.
 */
#ifndef HAL_BSD_SOCKET_FD_BASE
#define HAL_BSD_SOCKET_FD_BASE 64
#endif

/**
 * @def HAL_SWSERIAL_MAX_INSTANCES
 * Maximum number of software UART ports in the HAL-owned pool.
 * On RP2040 this is an upper bound: every active handle also needs two free
 * PIO state machines and one free DMA channel.
 */
#ifndef HAL_SWSERIAL_MAX_INSTANCES
#define HAL_SWSERIAL_MAX_INSTANCES 4
#endif

/**
 * @def HAL_UART_MAX_INSTANCES
 * Maximum number of hardware UART handles.
 * Each slot stores lightweight metadata only; the target backend owns the
 * hardware peripheral.
 */
#ifndef HAL_UART_MAX_INSTANCES
#define HAL_UART_MAX_INSTANCES 2
#endif

/**
 * @def HAL_RTC_MAX_INSTANCES
 * Maximum number of simultaneous RTC handles.
 *
 * The current backend set contains PCF8563 and DS3231. Each slot stores
 * per-instance runtime state and synchronization metadata.
 */
#ifndef HAL_RTC_MAX_INSTANCES
#define HAL_RTC_MAX_INSTANCES 4
#endif

/**
 * @def HAL_THERMOCOUPLE_MAX_INSTANCES
 * Maximum number of simultaneous thermocouple instances (MCP9600 / MAX6675).
 * Each slot holds one driver object placed in a static storage buffer.
 * Override with -DHAL_THERMOCOUPLE_MAX_INSTANCES=N if more are needed.
 */
#ifndef HAL_THERMOCOUPLE_MAX_INSTANCES
#define HAL_THERMOCOUPLE_MAX_INSTANCES 4
#endif

/**
 * @def HAL_DS18B20_MAX_INSTANCES
 * Maximum number of simultaneous DS18B20 handles.
 * Each slot stores one state machine plus a small cache.
 */
#ifndef HAL_DS18B20_MAX_INSTANCES
#define HAL_DS18B20_MAX_INSTANCES 4
#endif

/**
 * @def HAL_ONEWIRE_MAX_INSTANCES
 * Maximum number of simultaneous generic OneWire bus handles.
 */
#ifndef HAL_ONEWIRE_MAX_INSTANCES
#define HAL_ONEWIRE_MAX_INSTANCES 4
#endif

/**
 * @def MOCK_EEPROM_BUF_SIZE
 * Mock-only: size (bytes) of the in-memory array that backs the mock EEPROM.
 * Large enough to cover AT24C256 (32 KB) by default.
 */
#ifndef MOCK_EEPROM_BUF_SIZE
#define MOCK_EEPROM_BUF_SIZE 32768
#endif

/**
 * @def MOCK_CAN_MAX_INST
 * Mock-only: maximum CAN instances for unit-test builds.
 */
#ifndef MOCK_CAN_MAX_INST
#define MOCK_CAN_MAX_INST 4
#endif

/**
 * @def MOCK_CAN_BUF_SIZE
 * Mock-only: ring-buffer depth (frames) for each mock CAN instance.
 */
#ifndef MOCK_CAN_BUF_SIZE
#define MOCK_CAN_BUF_SIZE 32
#endif

/**
 * @def MOCK_MAX_ALARMS
 * Mock-only: maximum simultaneous timer alarms for unit-test builds.
 */
#ifndef MOCK_MAX_ALARMS
#define MOCK_MAX_ALARMS 16
#endif

/**
 * @def HAL_DEBUG_DEFAULT_BAUD
 * Default baud rate used by hal_deb() lazy initialisation when
 * hal_debug_init() has not been called explicitly.
 */
#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 9600
#endif

/* Keep the runtime API available through hal_config.h. */
#include "hal_runtime_config.h"

/* Keep the assertion API available through hal_config.h. */
#include "hal_assert.h"
