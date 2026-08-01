#pragma once

/**
 * @file hal_config.h
 * @brief Centralised compile-time and runtime configuration for JaszczurHAL.
 *
 * This file contains:
 *  1. Application-level feature toggles (formerly libConfig.h).
 *  2. Static-pool size defaults - override with project-level -D flags.
 *  3. Runtime configuration via hal_setup() - allows changing effective
 *     pool limits at startup (cannot exceed compile-time max).
 *  4. HAL_ASSERT debug mechanism.
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

/* ── Project-level configuration hook ────────────────────────────────── */
/* If the sketch (project) directory contains a file named
   hal_project_config.h, it is automatically included here.  Use it to
   define HAL_ENABLE_* flags, override pool sizes, or set feature
  toggles - without modifying the library itself.
  Note: this requires the sketch directory to be present on the include
  path for library compilation units (for example via
  compiler.cpp.extra_flags / compiler.c.extra_flags).                    */
#if defined(__has_include)
#if __has_include("hal_project_config.h")
#include "hal_project_config.h"
#endif
#endif

#include "hal_board.h" /* physical board profile and runtime capabilities */

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
/* JaszczurHAL uses an OPT-IN flag model: by default *nothing* beyond the
   bare core is compiled. Each module must be explicitly enabled with a
   HAL_ENABLE_* macro - either via compiler -D flags or by defining it in
   the project-local `hal_project_config.h`. Both the header declarations
   and the implementation file are compiled out unless the corresponding
   flag is defined, so unused modules cost zero code/RAM and any
   third-party libraries they depend on are not linked.

   Supported module flags:

     Application entry:
       HAL_ENABLE_APP_TASK1   - Dispatch optional app_task1() from the
                                  HAL-provided entry path. On the RP family
                                  this explicitly starts the core-1 path.

     FreeRTOS:
       HAL_ENABLE_FREERTOS    - Opt in to native FreeRTOS availability.
                                  RP uses the pinned local kernel and has
                                  FreeRTOS-aware
                                  mutex/delay/idle primitives. STM32G474
                                  uses a local FreeRTOS-Kernel checkout,
                                  target FreeRTOSConfig.h, Cortex-M4F port,
                                  heap_4, FreeRTOS-owned SVC/PendSV/SysTick
                                  vectors, and FreeRTOS-aware
                                  mutex/delay/idle primitives. With
                                  HAL_PROVIDE_APP_ENTRY, STM32 creates
                                  app_task0() and optional app_task1() tasks;
                                  override stack/priority with
                                  HAL_FREERTOS_TASK{0,1}_STACK and
                                  HAL_FREERTOS_TASK{0,1}_PRIORITY. This flag
                                  does not create a public hal_rtos_* API.

     Connectivity:
       HAL_ENABLE_WIFI          - WiFi through the selected JaszczurHAL
                                  network backend and board profile.
       HAL_ENABLE_TIME          - NTP / system time (propagates: WIFI).
       HAL_ENABLE_MQTT          - PubSubClient wrapper (propagates: WIFI).
       HAL_ENABLE_UDP           - UDP transport     (propagates: WIFI).
       HAL_ENABLE_TCP           - TCP client/listener transport
                                  (propagates: WIFI).
       HAL_ENABLE_HTTP_SERVER   - small poll-driven HTTP/1.1 server over
                                  hal_tcp (propagates: TCP, WIFI).
       HAL_ENABLE_HTTP_FILES    - small file serving/upload adapter over
                                  HAL HTTP routes (propagates: HTTP_SERVER,
                                  TCP, WIFI).
       HAL_ENABLE_WEBSOCKET     - small WebSocket server over hal_tcp
                                  (propagates: TCP, WIFI).
       HAL_ENABLE_NET_CONSOLE   - password-protected serial/debug mirror over
                                  hal_tcp (propagates: TCP, WIFI).
       HAL_ENABLE_NET_COMMANDS  - JSON/text command dispatcher for HTTP and
                                  WebSocket control channels (propagates:
                                  HTTP_SERVER, WEBSOCKET, CJSON, TCP, WIFI).
       HAL_ENABLE_BSD_SOCKETS   - minimal POSIX/BSD socket adapter
                                  (propagates: UDP, TCP, WIFI).
       HAL_ENABLE_TLS           - portable TLS client with a private BearSSL
                                  provider (propagates: TCP, WIFI).
       HAL_ENABLE_HTTP_CLIENT   - portable HTTP/HTTPS client (propagates:
                                  TCP, WIFI; select TLS explicitly for HTTPS).
       HAL_ENABLE_OTA           - HAL-socket OTA service
                                   (propagates: WIFI, UDP, TCP, CRYPTO, CRC).
       HAL_ENABLE_WIREGUARD     - WireGuard wrapper (propagates: WIFI, UDP).
       HAL_ENABLE_CELLULAR_MODEM - generic AT-modem engine (hal_modem_at).
                                  Requires at least one backend (e.g.
                                  HAL_ENABLE_A7670), otherwise a
                                  compile-time #error is emitted.
       HAL_ENABLE_A7670         - SimCom A7670 LTE Cat-1 cellular modem
                                  backend (propagates: CELLULAR_MODEM,
                                  UART).

     Storage:
       HAL_ENABLE_EEPROM        - EEPROM (AT24C256 / target flash).
       HAL_ENABLE_KV            - Key-value store (propagates: EEPROM).
       HAL_ENABLE_LITTLEFS      - LittleFS lifecycle + basic FS helpers.
       HAL_ENABLE_FAT           - FatFs core + shared SD-over-SPI disk I/O
                                  and file helpers.
       HAL_ENABLE_SDLOGGER      - SD-card logger/crash logger
                                  (propagates: FAT, EEPROM, SPI).

     Buses:
       HAL_ENABLE_UART          - Hardware UART.
      HAL_ENABLE_SWSERIAL      - software UART (RP2040 PIO/DMA; shared GPIO
                                  fallback on other targets).
       HAL_ENABLE_I2C           - I2C master/controller.
       HAL_ENABLE_I2C_SLAVE     - I2C slave/target with register map.
       HAL_ENABLE_SPI           - SPI master/controller.
       HAL_ENABLE_CAN           - generic CAN API facade.
       HAL_ENABLE_MCP2515       - MCP2515 CAN backend
       HAL_ENABLE_MCP251XFD     - MCP2517FD/MCP2518FD CAN FD backend
       HAL_ENABLE_STM32G474_FDCAN - native STM32G474 FDCAN backend
                                  (propagates: CAN; STM32G474 only).

     Time-of-day:
       HAL_ENABLE_RTC           - generic RTC API (requires at least one
                                  backend: PCF8563 or DS3231).
       HAL_ENABLE_PCF8563       - PCF8563 RTC backend (propagates: RTC, I2C).
       HAL_ENABLE_DS3231        - DS3231 RTC backend  (propagates: RTC, I2C).

     Sensors:
       HAL_ENABLE_THERMOCOUPLE  - generic thermocouple API (requires at
                                  least one backend: MCP9600 or MAX6675).
       HAL_ENABLE_MCP9600       - MCP9600/MCP9601 shared HAL I2C backend
                                  (propagates: THERMOCOUPLE, I2C).
       HAL_ENABLE_MAX6675       - MAX6675 backend       (propagates:
                                  THERMOCOUPLE; shared bit-bang over HAL GPIO).
       HAL_ENABLE_DS18B20       - shared DS18B20 1-Wire temperature sensor
                                  (propagates: ONEWIRE).
       HAL_ENABLE_BH1750        - shared BH1750 ambient-light sensor over
                                  HAL I2C (propagates: I2C).
       HAL_ENABLE_ADP5360       - shared ADP5360 PMIC over HAL I2C:
                                  MFD init/reset/shipment, charger,
                                  fuel-gauge and buck/buck-boost regulator
                                  control (propagates: I2C).
       HAL_ENABLE_TSC2007       - shared TSC2007 resistive touch controller
                                  over HAL I2C (propagates: I2C).
       HAL_ENABLE_STMPE610      - shared STMPE610 resistive touch controller
                                  over HAL I2C/SPI (propagates: I2C, SPI).
       HAL_ENABLE_IRSMALL_DECODER - shared IRsmallDecoder-compatible infrared
                                  receiver decoder over HAL GPIO interrupts.
       HAL_ENABLE_ONEWIRE       - shared generic 1-Wire bus API wrapper
                                  (propagates: CRC).
       HAL_ENABLE_CRC           - generic CRC-8/16/32 checksums (hal_crc);
                                  auto-enabled by ONEWIRE/DS18B20.
       HAL_ENABLE_EXTERNAL_ADC  - ADS1115 external ADC via shared ADS1X15
                                  HAL I2C driver (propagates: I2C).
       HAL_ENABLE_MCP3221       - MCP3221 12-bit ADC over HAL I2C
                                  (propagates: I2C).
       HAL_ENABLE_GPS           - GPS / NMEA receiver (requires a serial
                                  transport: HAL_ENABLE_UART or
                                  HAL_ENABLE_SWSERIAL; does NOT auto-enable
   one).

     Digital potentiometers:
       HAL_ENABLE_DIGIPOT       - generic digital-potentiometer API (requires
                                  at least one backend: MCP401X or MAX5395).
       HAL_ENABLE_MCP401X       - MCP4017/4018/4019 backend (propagates:
                                  DIGIPOT, I2C).
       HAL_ENABLE_MAX5395       - MAX5395 backend       (propagates:
                                  DIGIPOT, I2C).

     Audio volume control:
       HAL_ENABLE_PGA2311       - PGA2311 stereo volume controller over SPI
                                  (propagates: SPI).

     Simple I/O chips:
       HAL_ENABLE_MCP23017      - MCP23017 I2C GPIO expander
                                  (propagates: I2C).
       HAL_ENABLE_PCA9654E      - PCA9654E I2C output expander
                                  (propagates: I2C).
       HAL_ENABLE_PCF8574       - PCF8574 quasi-bidirectional I2C GPIO
                                  expander (propagates: I2C).
       HAL_ENABLE_HC595         - 74HC595 SPI shift-register output expander
                                  (propagates: SPI).
       HAL_ENABLE_MCP4725       - MCP4725 12-bit DAC over HAL I2C
                                  (propagates: I2C).

     RFID:
       HAL_ENABLE_MFRC522       - shared MFRC522 RFID reader driver over HAL
                                  SPI/I2C (propagates: SPI).
       HAL_ENABLE_PN532         - shared PN532 NFC/RFID reader driver over
                                  HAL SPI/I2C/UART (propagates: SPI).

     PWM audio:
       HAL_ENABLE_DACLESS       - DACless PWM-audio engine with block/sample
                                  callbacks and ADC sampling (propagates:
                                  DMA_PWM_AUDIO, PWM_FREQ).
       HAL_ENABLE_DMA_PWM_AUDIO - Narrow DMA helper API for timer-paced
                                  PWM-audio buffers.

     PWM / status:
       HAL_ENABLE_PWM_FREQ      - Frequency-controlled PWM.
       HAL_ENABLE_RGB_LED       - NeoPixel RGB status LED.

     Display (fasada + backend):
       HAL_ENABLE_DISPLAY       - generic display API (requires at least
                                  one backend: TFT or SSD1306).
       HAL_ENABLE_HD44780 - HD44780-compatible parallel character LCD
                                  driver over HAL GPIO/system timing.
       HAL_ENABLE_TFT           - SPI TFT family (requires at least one
                                  driver below; propagates: DISPLAY, SPI).
       HAL_ENABLE_ILI9341       - ILI9341 TFT driver (propagates: TFT, SPI).
       HAL_ENABLE_ST7789        - ST7789 TFT driver  (propagates: TFT, SPI).
       HAL_ENABLE_ST7735        - ST7735 TFT driver  (propagates: TFT, SPI).
       HAL_ENABLE_ST7796S       - ST7796S TFT driver (propagates: TFT, SPI).
       HAL_ENABLE_GC9A01        - GC9A01 round TFT driver (propagates: TFT,
                                  SPI).
       HAL_ENABLE_SSD1331       - SSD1331 RGB OLED facade/backend
                                  (propagates: DISPLAY, SPI).
       HAL_ENABLE_SSD135X       - SSD1351/SSD1357 RGB OLED facade/backend
                                  (propagates: DISPLAY, SPI).
       HAL_ENABLE_SSD1306       - SSD1306 OLED driver (propagates: DISPLAY,
   I2C).
       HAL_ENABLE_ST7567        - ST7567 monochrome LCD facade/backend
                                  (propagates: DISPLAY, I2C; SPI transport
                                  available when SPI is enabled).
       HAL_ENABLE_SSD16XX       - SSD1608/SSD1673/SSD1675A/SSD1680/SSD1681
                                  monochrome EPD backend (propagates: DISPLAY,
                                  SPI).
       HAL_ENABLE_UC81XX        - UC8175/UC8176/UC8151D/UC8179 monochrome EPD
                                  backend (propagates: DISPLAY, SPI).

     Crypto + bundled libs:
       HAL_ENABLE_CRYPTO        - hal_crypto (Base64, MD5, SHA-256,
                                  HMAC-SHA256, ChaCha20 / -Poly1305) and
                                  the dependent hal_sc_auth helper.
                                  Without this flag the headers expand
                                  to nothing and the implementation TUs
                                  produce empty objects. hal_serial_session
                                  keeps working - the SC_AUTH_BEGIN /
                                  SC_AUTH_PROVE handlers are simply
                                  compiled out and the session never
                                  enters the authenticated state.
       HAL_ENABLE_CJSON         - bundled cJSON / cJSON_Utils sources.
       HAL_ENABLE_PNG           - bundled LodePNG memory-based PNG encoder /
                                  decoder. JaszczurHAL disables LodePNG disk
                                  helpers and C++ std wrapper by default; define
                                  HAL_LODEPNG_ENABLE_DISK or
                                  HAL_LODEPNG_ENABLE_CPP to opt them back in.
       HAL_ENABLE_PNG_AS_BASE64 - Base64-encoded PNG helpers in tools
                                  (propagates: CRYPTO, PNG).
       HAL_ENABLE_JPEG          - managed TJpgDec memory-based baseline JPEG
                                  decoder with RGB565 output.
       HAL_ENABLE_JPEG_AS_BASE64 - Base64-encoded JPEG helpers in tools
                                  (propagates: CRYPTO, JPEG).

     Test framework:
       HAL_ENABLE_UNITY         - bundled Unity framework. Typically
                                  enabled only by the host-test CMake.

   Special opt-OUT flag (kept for compatibility with assert.h/NDEBUG
   convention):
     HAL_DISABLE_ASSERTS        - compile HAL_ASSERT() to no-ops.
                                  Asserts are ON by default.

   Dependency propagation - enabling a dependent module automatically
   enables every module it requires.                                     */

/* ── Dependency propagation (enabling a child enables its parents) ──── */

#ifdef HAL_ENABLE_KV
#ifndef HAL_ENABLE_EEPROM
#define HAL_ENABLE_EEPROM
#endif
#endif

#ifdef HAL_ENABLE_SDLOGGER
#ifndef HAL_ENABLE_FAT
#define HAL_ENABLE_FAT
#endif
#ifndef HAL_ENABLE_EEPROM
#define HAL_ENABLE_EEPROM
#endif
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

#if defined(HAL_ENABLE_EEPROM) && (HAL_EEPROM_TYPE == EEPROM_TYPE_AT24C256)
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_PNG_AS_BASE64
#ifndef HAL_ENABLE_CRYPTO
#define HAL_ENABLE_CRYPTO
#endif
#ifndef HAL_ENABLE_PNG
#define HAL_ENABLE_PNG
#endif
#endif

#ifdef HAL_ENABLE_JPEG_AS_BASE64
#ifndef HAL_ENABLE_CRYPTO
#define HAL_ENABLE_CRYPTO
#endif
#ifndef HAL_ENABLE_JPEG
#define HAL_ENABLE_JPEG
#endif
#endif

#ifdef HAL_ENABLE_TIME
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#endif

/* WiFi-dependent network modules. */
#ifdef HAL_ENABLE_MQTT
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#ifndef HAL_ENABLE_TCP
#define HAL_ENABLE_TCP
#endif
#endif

#ifdef HAL_ENABLE_HTTP_CLIENT
#ifndef HAL_ENABLE_TCP
#define HAL_ENABLE_TCP
#endif
#endif

#ifdef HAL_ENABLE_TLS
#ifndef HAL_ENABLE_TCP
#define HAL_ENABLE_TCP
#endif
#endif

#ifdef HAL_ENABLE_BSD_SOCKETS
#ifndef HAL_ENABLE_UDP
#define HAL_ENABLE_UDP
#endif
#ifndef HAL_ENABLE_TCP
#define HAL_ENABLE_TCP
#endif
#endif

#ifdef HAL_ENABLE_UDP
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#endif

#ifdef HAL_ENABLE_TCP
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#endif

#ifdef HAL_ENABLE_HTTP_SERVER
#ifndef HAL_ENABLE_TCP
#define HAL_ENABLE_TCP
#endif
#endif

#ifdef HAL_ENABLE_HTTP_FILES
#ifndef HAL_ENABLE_HTTP_SERVER
#define HAL_ENABLE_HTTP_SERVER
#endif
#ifndef HAL_ENABLE_TCP
#define HAL_ENABLE_TCP
#endif
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#endif

#ifdef HAL_ENABLE_WEBSOCKET
#ifndef HAL_ENABLE_TCP
#define HAL_ENABLE_TCP
#endif
#endif

#ifdef HAL_ENABLE_NET_CONSOLE
#ifndef HAL_ENABLE_TCP
#define HAL_ENABLE_TCP
#endif
#endif

#ifdef HAL_ENABLE_NET_COMMANDS
#ifndef HAL_ENABLE_HTTP_SERVER
#define HAL_ENABLE_HTTP_SERVER
#endif
#ifndef HAL_ENABLE_WEBSOCKET
#define HAL_ENABLE_WEBSOCKET
#endif
#ifndef HAL_ENABLE_CJSON
#define HAL_ENABLE_CJSON
#endif
#ifndef HAL_ENABLE_TCP
#define HAL_ENABLE_TCP
#endif
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#endif

#ifdef HAL_ENABLE_OTA
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#ifndef HAL_ENABLE_UDP
#define HAL_ENABLE_UDP
#endif
#ifndef HAL_ENABLE_TCP
#define HAL_ENABLE_TCP
#endif
#ifndef HAL_ENABLE_CRYPTO
#define HAL_ENABLE_CRYPTO
#endif
#ifndef HAL_ENABLE_CRC
#define HAL_ENABLE_CRC
#endif
#endif

#ifdef HAL_ENABLE_WIREGUARD
#ifndef HAL_ENABLE_UDP
#define HAL_ENABLE_UDP
#endif
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#endif

#ifdef HAL_ENABLE_TIME
#ifndef HAL_ENABLE_UDP
#define HAL_ENABLE_UDP
#endif
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#endif

/* -- Derived network core and exactly-one backend selection ------------ */
/* Feature flags request behavior.  They do not select a radio or socket
 * implementation.  A target may provide a compatibility default, but an
 * explicit HAL_NETWORK_BACKEND_* definition always remains authoritative. */
#if defined(HAL_ENABLE_WIFI) || defined(HAL_ENABLE_TCP) ||                     \
    defined(HAL_ENABLE_UDP) || defined(HAL_ENABLE_MQTT) ||                     \
    defined(HAL_ENABLE_BSD_SOCKETS) || defined(HAL_ENABLE_TLS) ||              \
    defined(HAL_ENABLE_HTTP_SERVER) || defined(HAL_ENABLE_HTTP_FILES) ||       \
    defined(HAL_ENABLE_WEBSOCKET) || defined(HAL_ENABLE_NET_CONSOLE) ||        \
    defined(HAL_ENABLE_NET_COMMANDS) || defined(HAL_ENABLE_OTA) ||             \
    defined(HAL_ENABLE_WIREGUARD) || defined(HAL_ENABLE_TIME)
#define HAL_ENABLE_NETWORK_CORE 1
#endif

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

/* Cellular modem backends. */
#ifdef HAL_ENABLE_A7670
#ifndef HAL_ENABLE_CELLULAR_MODEM
#define HAL_ENABLE_CELLULAR_MODEM
#endif
#ifndef HAL_ENABLE_UART
#define HAL_ENABLE_UART
#endif
#endif

/* I2C-dependent sensors / RTCs. */
#ifdef HAL_ENABLE_EXTERNAL_ADC
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_BH1750
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_ADP5360
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_MCP3221
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_TSC2007
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_STMPE610
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

#ifdef HAL_ENABLE_PCF8563
#ifndef HAL_ENABLE_RTC
#define HAL_ENABLE_RTC
#endif
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_DS3231
#ifndef HAL_ENABLE_RTC
#define HAL_ENABLE_RTC
#endif
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_MCP9600
#ifndef HAL_ENABLE_THERMOCOUPLE
#define HAL_ENABLE_THERMOCOUPLE
#endif
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_MAX6675
#ifndef HAL_ENABLE_THERMOCOUPLE
#define HAL_ENABLE_THERMOCOUPLE
#endif
#endif

/* I2C digital potentiometers. */
#ifdef HAL_ENABLE_MCP401X
#ifndef HAL_ENABLE_DIGIPOT
#define HAL_ENABLE_DIGIPOT
#endif
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_MAX5395
#ifndef HAL_ENABLE_DIGIPOT
#define HAL_ENABLE_DIGIPOT
#endif
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

/* SPI audio volume control. */
#ifdef HAL_ENABLE_PGA2311
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

/* Simple I/O chips. */
#ifdef HAL_ENABLE_MCP23017
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_PCA9654E
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_PCF8574
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#ifdef HAL_ENABLE_HC595
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

#ifdef HAL_ENABLE_MCP4725
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

/* MFRC522 RFID reader. */
#ifdef HAL_ENABLE_MFRC522
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

/* PN532 NFC/RFID reader. */
#ifdef HAL_ENABLE_PN532
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

/* PWM audio engine. */
#ifdef HAL_ENABLE_DACLESS
#ifndef HAL_ENABLE_DMA_PWM_AUDIO
#define HAL_ENABLE_DMA_PWM_AUDIO
#endif
#ifndef HAL_ENABLE_PWM_FREQ
#define HAL_ENABLE_PWM_FREQ
#endif
#endif

/* 1-Wire stack. */
#ifdef HAL_ENABLE_DS18B20
#ifndef HAL_ENABLE_ONEWIRE
#define HAL_ENABLE_ONEWIRE
#endif
#endif

/* 1-Wire relies on CRC-8/CRC-16 for ROM and scratchpad validation. */
#ifdef HAL_ENABLE_ONEWIRE
#ifndef HAL_ENABLE_CRC
#define HAL_ENABLE_CRC
#endif
#endif

/* GPS needs a serial transport but is not tied to a specific one: it can be
   fed from a hardware UART (hal_uart) or SoftwareSerial (hal_swserial). The
   caller enables whichever the wiring uses. If neither is selected, GPS
   defaults to the hardware UART, which exists on every target (RP2040,
   STM32G474, mock). We deliberately default to UART rather than SoftwareSerial
   so GPS never drags SoftwareSerial onto targets that lack it; a caller wiring
   GPS to SoftwareSerial simply enables HAL_ENABLE_SWSERIAL explicitly. */
#ifdef HAL_ENABLE_GPS
#if !defined(HAL_ENABLE_UART) && !defined(HAL_ENABLE_SWSERIAL)
#define HAL_ENABLE_UART
#endif
#endif

/* Display driver family. */
#ifdef HAL_ENABLE_ILI9341
#ifndef HAL_ENABLE_TFT
#define HAL_ENABLE_TFT
#endif
#endif
#ifdef HAL_ENABLE_ST7789
#ifndef HAL_ENABLE_TFT
#define HAL_ENABLE_TFT
#endif
#endif
#ifdef HAL_ENABLE_ST7735
#ifndef HAL_ENABLE_TFT
#define HAL_ENABLE_TFT
#endif
#endif
#ifdef HAL_ENABLE_ST7796S
#ifndef HAL_ENABLE_TFT
#define HAL_ENABLE_TFT
#endif
#endif
#ifdef HAL_ENABLE_GC9A01
#ifndef HAL_ENABLE_TFT
#define HAL_ENABLE_TFT
#endif
#endif

#ifdef HAL_ENABLE_TFT
#ifndef HAL_ENABLE_DISPLAY
#define HAL_ENABLE_DISPLAY
#endif
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

#ifdef HAL_ENABLE_MCP2515
#ifndef HAL_ENABLE_CAN
#define HAL_ENABLE_CAN
#endif
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

#ifdef HAL_ENABLE_MCP251XFD
#ifndef HAL_ENABLE_CAN
#define HAL_ENABLE_CAN
#endif
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

#ifdef HAL_ENABLE_STM32G474_FDCAN
#ifndef HAL_ENABLE_CAN
#define HAL_ENABLE_CAN
#endif
#endif

#ifdef HAL_ENABLE_SSD1306
#ifndef HAL_ENABLE_DISPLAY
#define HAL_ENABLE_DISPLAY
#endif
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#if defined(HAL_ENABLE_SSD1331) || defined(HAL_ENABLE_SSD135X)
#ifndef HAL_ENABLE_DISPLAY
#define HAL_ENABLE_DISPLAY
#endif
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

#ifdef HAL_ENABLE_ST7567
#ifndef HAL_ENABLE_DISPLAY
#define HAL_ENABLE_DISPLAY
#endif
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#endif

#if defined(HAL_ENABLE_SSD16XX) || defined(HAL_ENABLE_UC81XX)
#ifndef HAL_ENABLE_DISPLAY
#define HAL_ENABLE_DISPLAY
#endif
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

/* ── Consistency checks for fasada modules that need a backend ──────── */
/* Standalone modules (WIFI, I2C, I2C_SLAVE, SPI, SWSERIAL, UART, EEPROM,
  KV, SDLOGGER, GPS, DACLESS, DMA_PWM_AUDIO, PWM_FREQ, RGB_LED, DS18B20, DHT,
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

/* ── Optional verbose flag report ───────────────────────────────────── */
/* Define HAL_CONFIG_VERBOSE (e.g. -DHAL_CONFIG_VERBOSE) to make the
   preprocessor print every HAL_ENABLE_* flag that is active after
   propagation. Useful for debugging "why is module X being compiled?". */

#ifdef HAL_CONFIG_VERBOSE
#ifdef HAL_ENABLE_WIFI
#pragma message("HAL_CONFIG: HAL_ENABLE_WIFI")
#endif
#ifdef HAL_ENABLE_TIME
#pragma message("HAL_CONFIG: HAL_ENABLE_TIME")
#endif
#ifdef HAL_ENABLE_MQTT
#pragma message("HAL_CONFIG: HAL_ENABLE_MQTT")
#endif
#ifdef HAL_ENABLE_UDP
#pragma message("HAL_CONFIG: HAL_ENABLE_UDP")
#endif
#ifdef HAL_ENABLE_TCP
#pragma message("HAL_CONFIG: HAL_ENABLE_TCP")
#endif
#ifdef HAL_ENABLE_HTTP_SERVER
#pragma message("HAL_CONFIG: HAL_ENABLE_HTTP_SERVER")
#endif
#ifdef HAL_ENABLE_HTTP_FILES
#pragma message("HAL_CONFIG: HAL_ENABLE_HTTP_FILES")
#endif
#ifdef HAL_ENABLE_WEBSOCKET
#pragma message("HAL_CONFIG: HAL_ENABLE_WEBSOCKET")
#endif
#ifdef HAL_ENABLE_NET_CONSOLE
#pragma message("HAL_CONFIG: HAL_ENABLE_NET_CONSOLE")
#endif
#ifdef HAL_ENABLE_NET_COMMANDS
#pragma message("HAL_CONFIG: HAL_ENABLE_NET_COMMANDS")
#endif
#ifdef HAL_ENABLE_BSD_SOCKETS
#pragma message("HAL_CONFIG: HAL_ENABLE_BSD_SOCKETS")
#endif
#ifdef HAL_ENABLE_TLS
#pragma message("HAL_CONFIG: HAL_ENABLE_TLS")
#endif
#ifdef HAL_ENABLE_OTA
#pragma message("HAL_CONFIG: HAL_ENABLE_OTA")
#endif
#ifdef HAL_ENABLE_WIREGUARD
#pragma message("HAL_CONFIG: HAL_ENABLE_WIREGUARD")
#endif
#ifdef HAL_ENABLE_CELLULAR_MODEM
#pragma message("HAL_CONFIG: HAL_ENABLE_CELLULAR_MODEM")
#endif
#ifdef HAL_ENABLE_A7670
#pragma message("HAL_CONFIG: HAL_ENABLE_A7670")
#endif
#ifdef HAL_ENABLE_EEPROM
#pragma message("HAL_CONFIG: HAL_ENABLE_EEPROM")
#endif
#ifdef HAL_ENABLE_KV
#pragma message("HAL_CONFIG: HAL_ENABLE_KV")
#endif
#ifdef HAL_ENABLE_LITTLEFS
#pragma message("HAL_CONFIG: HAL_ENABLE_LITTLEFS")
#endif
#ifdef HAL_ENABLE_FAT
#pragma message("HAL_CONFIG: HAL_ENABLE_FAT")
#endif
#ifdef HAL_ENABLE_SDLOGGER
#pragma message("HAL_CONFIG: HAL_ENABLE_SDLOGGER")
#endif
#ifdef HAL_ENABLE_UART
#pragma message("HAL_CONFIG: HAL_ENABLE_UART")
#endif
#ifdef HAL_ENABLE_SWSERIAL
#pragma message("HAL_CONFIG: HAL_ENABLE_SWSERIAL")
#endif
#ifdef HAL_ENABLE_I2C
#pragma message("HAL_CONFIG: HAL_ENABLE_I2C")
#endif
#ifdef HAL_ENABLE_I2C_SLAVE
#pragma message("HAL_CONFIG: HAL_ENABLE_I2C_SLAVE")
#endif
#ifdef HAL_ENABLE_SPI
#pragma message("HAL_CONFIG: HAL_ENABLE_SPI")
#endif
#ifdef HAL_ENABLE_CAN
#pragma message("HAL_CONFIG: HAL_ENABLE_CAN")
#endif
#ifdef HAL_ENABLE_MCP2515
#pragma message("HAL_CONFIG: HAL_ENABLE_MCP2515")
#endif
#ifdef HAL_ENABLE_MCP251XFD
#pragma message("HAL_CONFIG: HAL_ENABLE_MCP251XFD")
#endif
#ifdef HAL_ENABLE_STM32G474_FDCAN
#pragma message("HAL_CONFIG: HAL_ENABLE_STM32G474_FDCAN")
#endif
#ifdef HAL_ENABLE_RTC
#pragma message("HAL_CONFIG: HAL_ENABLE_RTC")
#endif
#ifdef HAL_ENABLE_PCF8563
#pragma message("HAL_CONFIG: HAL_ENABLE_PCF8563")
#endif
#ifdef HAL_ENABLE_DS3231
#pragma message("HAL_CONFIG: HAL_ENABLE_DS3231")
#endif
#ifdef HAL_ENABLE_THERMOCOUPLE
#pragma message("HAL_CONFIG: HAL_ENABLE_THERMOCOUPLE")
#endif
#ifdef HAL_ENABLE_MCP9600
#pragma message("HAL_CONFIG: HAL_ENABLE_MCP9600")
#endif
#ifdef HAL_ENABLE_MAX6675
#pragma message("HAL_CONFIG: HAL_ENABLE_MAX6675")
#endif
#ifdef HAL_ENABLE_DS18B20
#pragma message("HAL_CONFIG: HAL_ENABLE_DS18B20")
#endif
#ifdef HAL_ENABLE_DHT
#pragma message("HAL_CONFIG: HAL_ENABLE_DHT")
#endif
#ifdef HAL_ENABLE_BH1750
#pragma message("HAL_CONFIG: HAL_ENABLE_BH1750")
#endif
#ifdef HAL_ENABLE_ADP5360
#pragma message("HAL_CONFIG: HAL_ENABLE_ADP5360")
#endif
#ifdef HAL_ENABLE_MCP3221
#pragma message("HAL_CONFIG: HAL_ENABLE_MCP3221")
#endif
#ifdef HAL_ENABLE_MCP23017
#pragma message("HAL_CONFIG: HAL_ENABLE_MCP23017")
#endif
#ifdef HAL_ENABLE_PCA9654E
#pragma message("HAL_CONFIG: HAL_ENABLE_PCA9654E")
#endif
#ifdef HAL_ENABLE_PCF8574
#pragma message("HAL_CONFIG: HAL_ENABLE_PCF8574")
#endif
#ifdef HAL_ENABLE_HC595
#pragma message("HAL_CONFIG: HAL_ENABLE_HC595")
#endif
#ifdef HAL_ENABLE_MCP4725
#pragma message("HAL_CONFIG: HAL_ENABLE_MCP4725")
#endif
#ifdef HAL_ENABLE_TSC2007
#pragma message("HAL_CONFIG: HAL_ENABLE_TSC2007")
#endif
#ifdef HAL_ENABLE_STMPE610
#pragma message("HAL_CONFIG: HAL_ENABLE_STMPE610")
#endif
#ifdef HAL_ENABLE_IRSMALL_DECODER
#pragma message("HAL_CONFIG: HAL_ENABLE_IRSMALL_DECODER")
#endif
#ifdef HAL_ENABLE_ONEWIRE
#pragma message("HAL_CONFIG: HAL_ENABLE_ONEWIRE")
#endif
#ifdef HAL_ENABLE_CRC
#pragma message("HAL_CONFIG: HAL_ENABLE_CRC")
#endif
#ifdef HAL_ENABLE_EXTERNAL_ADC
#pragma message("HAL_CONFIG: HAL_ENABLE_EXTERNAL_ADC")
#endif
#ifdef HAL_ENABLE_GPS
#pragma message("HAL_CONFIG: HAL_ENABLE_GPS")
#endif
#ifdef HAL_ENABLE_DIGIPOT
#pragma message("HAL_CONFIG: HAL_ENABLE_DIGIPOT")
#endif
#ifdef HAL_ENABLE_MCP401X
#pragma message("HAL_CONFIG: HAL_ENABLE_MCP401X")
#endif
#ifdef HAL_ENABLE_MAX5395
#pragma message("HAL_CONFIG: HAL_ENABLE_MAX5395")
#endif
#ifdef HAL_ENABLE_PGA2311
#pragma message("HAL_CONFIG: HAL_ENABLE_PGA2311")
#endif
#ifdef HAL_ENABLE_MFRC522
#pragma message("HAL_CONFIG: HAL_ENABLE_MFRC522")
#endif
#ifdef HAL_ENABLE_PN532
#pragma message("HAL_CONFIG: HAL_ENABLE_PN532")
#endif
#ifdef HAL_ENABLE_DACLESS
#pragma message("HAL_CONFIG: HAL_ENABLE_DACLESS")
#endif
#ifdef HAL_ENABLE_DMA_PWM_AUDIO
#pragma message("HAL_CONFIG: HAL_ENABLE_DMA_PWM_AUDIO")
#endif
#ifdef HAL_ENABLE_PWM_FREQ
#pragma message("HAL_CONFIG: HAL_ENABLE_PWM_FREQ")
#endif
#ifdef HAL_ENABLE_RGB_LED
#pragma message("HAL_CONFIG: HAL_ENABLE_RGB_LED")
#endif
#ifdef HAL_ENABLE_DISPLAY
#pragma message("HAL_CONFIG: HAL_ENABLE_DISPLAY")
#endif
#ifdef HAL_ENABLE_TFT
#pragma message("HAL_CONFIG: HAL_ENABLE_TFT")
#endif
#ifdef HAL_ENABLE_ILI9341
#pragma message("HAL_CONFIG: HAL_ENABLE_ILI9341")
#endif
#ifdef HAL_ENABLE_ST7789
#pragma message("HAL_CONFIG: HAL_ENABLE_ST7789")
#endif
#ifdef HAL_ENABLE_ST7735
#pragma message("HAL_CONFIG: HAL_ENABLE_ST7735")
#endif
#ifdef HAL_ENABLE_ST7796S
#pragma message("HAL_CONFIG: HAL_ENABLE_ST7796S")
#endif
#ifdef HAL_ENABLE_GC9A01
#pragma message("HAL_CONFIG: HAL_ENABLE_GC9A01")
#endif
#ifdef HAL_ENABLE_SSD1306
#pragma message("HAL_CONFIG: HAL_ENABLE_SSD1306")
#endif
#ifdef HAL_ENABLE_SSD1331
#pragma message("HAL_CONFIG: HAL_ENABLE_SSD1331")
#endif
#ifdef HAL_ENABLE_SSD135X
#pragma message("HAL_CONFIG: HAL_ENABLE_SSD135X")
#endif
#ifdef HAL_ENABLE_ST7567
#pragma message("HAL_CONFIG: HAL_ENABLE_ST7567")
#endif
#ifdef HAL_ENABLE_SSD16XX
#pragma message("HAL_CONFIG: HAL_ENABLE_SSD16XX")
#endif
#ifdef HAL_ENABLE_UC81XX
#pragma message("HAL_CONFIG: HAL_ENABLE_UC81XX")
#endif
#ifdef HAL_ENABLE_CRYPTO
#pragma message("HAL_CONFIG: HAL_ENABLE_CRYPTO")
#endif
#ifdef HAL_ENABLE_CJSON
#pragma message("HAL_CONFIG: HAL_ENABLE_CJSON")
#endif
#ifdef HAL_ENABLE_PNG
#pragma message("HAL_CONFIG: HAL_ENABLE_PNG")
#endif
#ifdef HAL_ENABLE_PNG_AS_BASE64
#pragma message("HAL_CONFIG: HAL_ENABLE_PNG_AS_BASE64")
#endif
#ifdef HAL_ENABLE_JPEG
#pragma message("HAL_CONFIG: HAL_ENABLE_JPEG")
#endif
#ifdef HAL_ENABLE_JPEG_AS_BASE64
#pragma message("HAL_CONFIG: HAL_ENABLE_JPEG_AS_BASE64")
#endif
#ifdef HAL_ENABLE_UNITY
#pragma message("HAL_CONFIG: HAL_ENABLE_UNITY")
#endif
#endif /* HAL_CONFIG_VERBOSE */

/* ── Portable source-compatibility macros ───────────────────────────── */

/**
 * @def PROGMEM
 * @brief No-op on platforms without a separate flash address space.
 *
 * JaszczurHAL targets use a unified address space, so this expands to nothing.
 */
#ifndef PROGMEM
#define PROGMEM /* no-op on platforms without separate flash address space */
#endif

/**
 * @def F(s)
 * @brief No-op identity macro for flash-string literals.
 *
 * JaszczurHAL targets use a unified address space, so this returns the string
 * pointer unchanged.
 */
#ifndef F
#define F(s) (s)
#endif

/**
 * @def hal_min(a, b)
 * @brief Type-generic minimum of two values.
 *
 * Safe drop-in for Arduino's min() macro; does not suffer from
 * double-evaluation when arguments have no side-effects.
 */
#ifndef hal_min
#define hal_min(a, b) (((a) < (b)) ? (a) : (b))
#endif

/**
 * @def hal_max(a, b)
 * @brief Type-generic maximum of two values.
 *
 * Safe drop-in for Arduino's max() macro; does not suffer from
 * double-evaluation when arguments have no side-effects.
 */
#ifndef hal_max
#define hal_max(a, b) (((a) > (b)) ? (a) : (b))
#endif

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

/* ── Runtime configuration ───────────────────────────────────────────── */

/**
 * @brief Runtime HAL configuration.
 *
 * All fields are initialised to the compile-time defaults by
 * hal_config_defaults().  The application may override individual fields
 * and pass the struct to hal_setup() before using any HAL create
 * functions.
 *
 * Values must not exceed the compile-time #define maximums (the static
 * arrays are sized at compile time).  hal_setup() caps any oversized
 * values automatically.
 *
 * @code
 *   hal_config_t cfg = hal_config_defaults();
 *   cfg.pwm_freq_max_channels = 4;   // use only 4 of the 8 slots
 *   cfg.can_max_instances     = 1;
 *   hal_setup(&cfg);
 * @endcode
 */
typedef struct {
  int pwm_freq_max_channels;  /**< Effective PWM-freq channel limit.  */
  int can_max_instances;      /**< Effective CAN instance limit.      */
  int uart_max_instances;     /**< Effective hardware UART limit.     */
  int swserial_max_instances; /**< Effective software UART limit.      */
  int mock_can_max_inst;      /**< Mock CAN instance limit.           */
  int mock_can_buf_size;      /**< Mock CAN ring-buffer depth.        */
  int mock_max_alarms;        /**< Mock timer alarm limit.            */
} hal_config_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return a hal_config_t with all fields set to compile-time defaults.
 */
hal_config_t hal_config_defaults(void);

/**
 * @brief Initialise the HAL with the given configuration.
 *
 * Must be called before any hal_*_create() function.  If never called,
 * compile-time defaults are used.  Values exceeding the compile-time
 * maximum are silently capped.
 *
 * @param cfg Pointer to the configuration struct.
 */
void hal_setup(const hal_config_t *cfg);

/**
 * @brief Get a pointer to the active HAL configuration (read-only).
 * @return Pointer to the internal config struct.
 */
const hal_config_t *hal_get_config(void);

#ifdef __cplusplus
}
#endif

/* ── Debug-assert mechanism ──────────────────────────────────────────── */

/**
 * @def HAL_ASSERT(cond, msg)
 * Lightweight assert for HAL resource exhaustion.
 *
 * When the condition is false the macro calls @c hal_assert_fail(), whose
 * implementation is selected by the exact HAL target. Hardware builds
 * print @p msg through the target debug channel and enter an infinite loop
 * so the watchdog can reset the system; mock/test builds call @c abort().
 *
 * Define @c HAL_DISABLE_ASSERTS before including this header (or via
 * a compiler flag) to compile all HAL_ASSERTs to no-ops, removing
 * both the text overhead and the branch from release builds.
 *
 * @code
 *   HAL_ASSERT(ptr != NULL, "hal_can: pool exhausted");
 * @endcode
 */
#ifdef HAL_DISABLE_ASSERTS

#define HAL_ASSERT(cond, msg) ((void)0)

#else /* asserts enabled (default) */

#ifdef __cplusplus
extern "C" {
#endif

HAL_NORETURN void hal_assert_fail(const char *msg);

#ifdef __cplusplus
}
#endif

#define HAL_ASSERT(cond, msg)                                                  \
  do {                                                                         \
    if (!(cond)) {                                                             \
      hal_assert_fail((msg));                                                  \
    }                                                                          \
  } while (0)

#endif /* HAL_DISABLE_ASSERTS */
