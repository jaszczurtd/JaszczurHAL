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

#include "hal_target.h" /* canonical backend/target selection */
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

#ifndef HAL_EEPROM_TYPE
#define HAL_EEPROM_TYPE EEPROM_TYPE_AT24C256
#endif

#if HAL_EEPROM_TYPE == EEPROM_TYPE_AT24C256
#define AT24C256
#endif

#ifndef EEPROM_I2C_ADDRESS
#define EEPROM_I2C_ADDRESS 0x50
#endif

/* Uncomment (or define via -D) to enable optional features:
 *   #define I2C_SCANNER
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

/* -- Application entry opt-ins ----------------------------------------- */
/* HAL_ENABLE_APP_TASK1 controls the optional app_task1 dispatch path when
   HAL_PROVIDE_APP_ENTRY is enabled. On RP2040 this emits Arduino loop1(),
   which starts the core-1 path, so it is intentionally explicit.         */

/* -- FreeRTOS opt-in ---------------------------------------------------- */
/* HAL_ENABLE_FREERTOS is a configuration flag for native FreeRTOS support.
   It does not add a public hal_rtos_* wrapper. Applications use the native
   FreeRTOS headers/API directly, while HAL internals select RTOS-safe paths
   where each target has implemented them.

   Current target status:
     - RP2040 must use arduino-pico's FreeRTOS mode (__FREERTOS).
       Under that configuration, hal_mutex_*, hal_delay_ms(), and hal_idle()
       are FreeRTOS-aware. hal_critical_section_* remains a hard, per-core
       interrupt mask for timing-sensitive code.
     - STM32G474 must have the local FreeRTOS-Kernel include path configured.
       STM32 CMake builds compile the Cortex-M4F port, heap_4, and kernel
       source list when FreeRTOS mode is enabled; HAL runtime primitives are
       upgraded in later stages.
   No default runtime behavior changes when HAL_ENABLE_FREERTOS is undefined. */

#ifdef HAL_ENABLE_FREERTOS
#if HAL_TARGET_IS_RP2040
#ifndef __FREERTOS
#error                                                                         \
    "JaszczurHAL: HAL_ENABLE_FREERTOS on RP2040 requires arduino-pico FreeRTOS mode. Select 'Operating System -> FreeRTOS SMP' or use an FQBN option such as ':os=freertos' so __FREERTOS is defined."
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
    "JaszczurHAL: HAL_ENABLE_FREERTOS is currently supported only for HAL_TARGET_RP2040 with arduino-pico FreeRTOS mode or HAL_TARGET_STM32G474 with a local FreeRTOS-Kernel configuration."
#endif
#endif

/* ── Module enable flags (opt-in) ────────────────────────────────────── */
/* JaszczurHAL uses an OPT-IN flag model: by default *nothing* beyond the
   bare core is compiled. Each module must be explicitly enabled with a
   HAL_ENABLE_* macro - either via compiler -D flags or by defining it in
   the project-local `hal_project_config.h`. Both the header declarations
   and the implementation file are compiled out unless the corresponding
   flag is defined, so unused modules cost zero code/RAM and any
   third-party libraries they depend on are not pulled in by the
   arduino-cli library resolver.

   Supported module flags:

     Application entry:
       HAL_ENABLE_APP_TASK1   - Dispatch optional app_task1() from the
                                  HAL-provided entry path. On RP2040 this
                                  emits loop1() and starts the core-1 path.

     FreeRTOS:
       HAL_ENABLE_FREERTOS    - Opt in to native FreeRTOS availability.
                                  RP2040 uses arduino-pico FreeRTOS mode
                                  (__FREERTOS) and has FreeRTOS-aware
                                  mutex/delay/idle primitives. STM32G474
                                  uses a local FreeRTOS-Kernel checkout,
                                  target FreeRTOSConfig.h, Cortex-M4F port,
                                  heap_4, and FreeRTOS-owned SVC/PendSV/
                                  SysTick vectors. This flag does not create
                                  a public hal_rtos_* API.

     Connectivity:
       HAL_ENABLE_WIFI          - WiFi (arduino-pico; use a WiFi-capable
                                  board/FQBN such as rpipicow at runtime).
       HAL_ENABLE_TIME          - NTP / system time (propagates: WIFI).
       HAL_ENABLE_MQTT          - PubSubClient wrapper (propagates: WIFI).
       HAL_ENABLE_UDP           - WiFiUDP wrapper   (propagates: WIFI).
       HAL_ENABLE_OTA           - ArduinoOTA wrapper (propagates: WIFI).
       HAL_ENABLE_WIREGUARD     - WireGuard wrapper (propagates: WIFI).
       HAL_ENABLE_CELLULAR_MODEM - generic AT-modem engine (hal_modem_at).
                                  Requires at least one backend (e.g.
                                  HAL_ENABLE_A7670), otherwise a
                                  compile-time #error is emitted.
       HAL_ENABLE_A7670         - SimCom A7670 LTE Cat-1 cellular modem
                                  backend (propagates: CELLULAR_MODEM,
                                  UART).

     Storage:
       HAL_ENABLE_EEPROM        - EEPROM (AT24C256 / RP2040 flash).
       HAL_ENABLE_KV            - Key-value store (propagates: EEPROM).
       HAL_ENABLE_LITTLEFS      - LittleFS lifecycle + basic FS helpers.
       HAL_ENABLE_SDLOGGER      - SD-card logger/crash logger
                                  (propagates: EEPROM, I2C, SPI).

     Buses:
       HAL_ENABLE_UART          - Hardware UART (SerialUART).
       HAL_ENABLE_SWSERIAL      - SoftwareSerial.
       HAL_ENABLE_I2C           - I2C master (Wire).
       HAL_ENABLE_I2C_SLAVE     - I2C slave/target with register map.
       HAL_ENABLE_SPI           - SPI master (Arduino-compatible SPIClass).
       HAL_ENABLE_CAN           - MCP2515 CAN bus (propagates: SPI).

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
       HAL_ENABLE_ONEWIRE       - shared generic 1-Wire bus API wrapper.
       HAL_ENABLE_EXTERNAL_ADC  - ADS1115 external ADC via shared ADS1X15
                                  HAL I2C driver (propagates: I2C).
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

     PWM / status:
       HAL_ENABLE_PWM_FREQ      - Frequency-controlled PWM.
       HAL_ENABLE_RGB_LED       - NeoPixel RGB status LED.

     Display (fasada + backend):
       HAL_ENABLE_DISPLAY       - generic display API (requires at least
                                  one backend: TFT or SSD1306).
       HAL_ENABLE_TFT           - SPI TFT family (requires at least one
                                  driver below; propagates: DISPLAY, SPI).
       HAL_ENABLE_ILI9341       - ILI9341 TFT driver (propagates: TFT, SPI).
       HAL_ENABLE_ST7789        - ST7789 TFT driver  (propagates: TFT, SPI).
       HAL_ENABLE_ST7735        - ST7735 TFT driver  (propagates: TFT, SPI).
       HAL_ENABLE_ST7796S       - ST7796S TFT driver (propagates: TFT, SPI).
       HAL_ENABLE_SSD1306       - SSD1306 OLED driver (propagates: DISPLAY,
   I2C).

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
#ifndef HAL_ENABLE_EEPROM
#define HAL_ENABLE_EEPROM
#endif
#ifndef HAL_ENABLE_I2C
#define HAL_ENABLE_I2C
#endif
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
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
#endif

#ifdef HAL_ENABLE_UDP
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#endif

#ifdef HAL_ENABLE_OTA
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
#endif

#ifdef HAL_ENABLE_WIREGUARD
#ifndef HAL_ENABLE_WIFI
#define HAL_ENABLE_WIFI
#endif
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

/* 1-Wire stack. */
#ifdef HAL_ENABLE_DS18B20
#ifndef HAL_ENABLE_ONEWIRE
#define HAL_ENABLE_ONEWIRE
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

#ifdef HAL_ENABLE_TFT
#ifndef HAL_ENABLE_DISPLAY
#define HAL_ENABLE_DISPLAY
#endif
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
#endif
#endif

#ifdef HAL_ENABLE_CAN
#ifndef HAL_ENABLE_SPI
#define HAL_ENABLE_SPI
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

/* ── Consistency checks for fasada modules that need a backend ──────── */
/* Standalone modules (WIFI, I2C, I2C_SLAVE, SPI, SWSERIAL, UART, EEPROM,
  KV, SDLOGGER, GPS, CAN, PWM_FREQ, RGB_LED, DS18B20, ONEWIRE, EXTERNAL_ADC,
  PGA2311,
   TIME, UNITY, MQTT, UDP, OTA, WIREGUARD, LITTLEFS, CRYPTO, CJSON) do
   NOT need such checks - they can be enabled on their own. The checks
   below only catch generic-API modules enabled without any backend,
   which would otherwise leave the user with a non-functional binary. */

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
    !defined(HAL_ENABLE_SSD1306)
#error                                                                         \
    "HAL_ENABLE_DISPLAY requires at least one backend: HAL_ENABLE_TFT or HAL_ENABLE_SSD1306"
#endif

#if defined(HAL_ENABLE_TFT) && !defined(HAL_ENABLE_ILI9341) &&                 \
    !defined(HAL_ENABLE_ST7789) && !defined(HAL_ENABLE_ST7735) &&              \
    !defined(HAL_ENABLE_ST7796S)
#error                                                                         \
    "HAL_ENABLE_TFT requires at least one driver: HAL_ENABLE_ILI9341 / HAL_ENABLE_ST7789 / HAL_ENABLE_ST7735 / HAL_ENABLE_ST7796S"
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
#ifdef HAL_ENABLE_ONEWIRE
#pragma message("HAL_CONFIG: HAL_ENABLE_ONEWIRE")
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
#ifdef HAL_ENABLE_SSD1306
#pragma message("HAL_CONFIG: HAL_ENABLE_SSD1306")
#endif
#ifdef HAL_ENABLE_CRYPTO
#pragma message("HAL_CONFIG: HAL_ENABLE_CRYPTO")
#endif
#ifdef HAL_ENABLE_CJSON
#pragma message("HAL_CONFIG: HAL_ENABLE_CJSON")
#endif
#ifdef HAL_ENABLE_UNITY
#pragma message("HAL_CONFIG: HAL_ENABLE_UNITY")
#endif
#endif /* HAL_CONFIG_VERBOSE */

/* ── Platform-independent Arduino-compat macros ──────────────────────── */
/* Only define fallbacks when building WITHOUT Arduino - on Arduino the
   real F()/PROGMEM come from the core headers included later.          */

#ifndef ARDUINO

/**
 * @def PROGMEM
 * @brief No-op on platforms without a separate flash address space.
 *
 * On AVR/Arduino the real PROGMEM qualifier places data in flash.
 * On RP2040 and PC mock builds it expands to nothing, allowing the same
 * source files to compile without modification.
 */
#ifndef PROGMEM
#define PROGMEM /* no-op on platforms without separate flash address space */
#endif

/**
 * @def F(s)
 * @brief No-op identity macro for flash-string literals on non-AVR builds.
 *
 * On AVR Arduino, F() wraps a string literal so it is stored in flash.
 * On RP2040 and PC mock builds there is no Harvard architecture, so F()
 * simply returns the string pointer unchanged.
 */
#ifndef F
#define F(s) (s) /* mock build: F() is a no-op identity */
#endif

#endif /* !ARDUINO */

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
 * @def HAL_CAN_MAX_INSTANCES
 * Maximum number of MCP2515 CAN-bus interfaces.  One instance per
 * physical chip (CS pin).  Typical boards use 1–2 chips.
 */
#ifndef HAL_CAN_MAX_INSTANCES
#define HAL_CAN_MAX_INSTANCES 2
#endif

/**
 * @def HAL_SWSERIAL_MAX_INSTANCES
 * Maximum number of SoftwareSerial ports.
 * Each slot holds a SoftwareSerial object (~50 B on AVR, ~120 B on ARM).
 */
#ifndef HAL_SWSERIAL_MAX_INSTANCES
#define HAL_SWSERIAL_MAX_INSTANCES 4
#endif

/**
 * @def HAL_UART_MAX_INSTANCES
 * Maximum number of hardware UART handles.
 * Each slot stores lightweight metadata only; the hardware peripheral is owned
 * by the Arduino core.
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
  int swserial_max_instances; /**< Effective SoftwareSerial limit.    */
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
 * implementation is selected by the canonical HAL target. Hardware builds
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

#ifndef JH_HAL_NORETURN
#if defined(__GNUC__) || defined(__clang__)
#define JH_HAL_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define JH_HAL_NORETURN __declspec(noreturn)
#else
#define JH_HAL_NORETURN
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

JH_HAL_NORETURN void hal_assert_fail(const char *msg);

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
