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

#include <stdint.h>
#include "hal_uart_config.h"

/* ── Application feature toggles ─────────────────────────────────────── */
/* These were previously in libConfig.h.  Override with -D flags or edit
   directly as needed for your project.                                  */

#ifndef SUPPORT_TRANSACTIONS
#define SUPPORT_TRANSACTIONS
#endif

/* Supported EEPROM types */
#define EEPROM_TYPE_AT24C256  1
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
 *   #define SD_LOGGER
 *   #define I2C_SCANNER
 *   #define RESET_EEPROM
 *   #define PICO_W
 *   #define FREE_RTOS
 */

/* ── Project-level configuration hook ────────────────────────────────── */
/* If the sketch (project) directory contains a file named
   hal_project_config.h, it is automatically included here.  Use it to
   define HAL_DISABLE_* flags, override pool sizes, or set feature
  toggles - without modifying the library itself.
  Note: this requires the sketch directory to be present on the include
  path for library compilation units (for example via
  compiler.cpp.extra_flags / compiler.c.extra_flags).                    */
#if defined(__has_include)
  #if __has_include("hal_project_config.h")
    #include "hal_project_config.h"
  #endif
#endif

/* ── Module disable flags ────────────────────────────────────────────── */
/* Define any of the HAL_DISABLE_* macros (via -D compiler flags, or in
   hal_project_config.h in your sketch directory) to exclude the
   corresponding HAL module from the build.  Both the header declarations
   and the implementation file are compiled out, and any third-party
   libraries they depend on are no longer pulled in by the arduino-cli
   library resolver.

   Supported flags:
     HAL_DISABLE_WIFI           - WiFi (requires PICO_W to be useful anyway)
     HAL_DISABLE_TIME           - NTP / system time (depends on WiFi)
     HAL_DISABLE_EEPROM         - EEPROM (AT24C256 / RP2040 flash)
     HAL_DISABLE_KV             - Key-value store (depends on EEPROM)
     HAL_DISABLE_GPS            - GPS / NMEA receiver (depends on SWSERIAL)
     HAL_DISABLE_THERMOCOUPLE   - all thermocouple backends (MCP9600 + MAX6675)
     HAL_DISABLE_MCP9600        - Adafruit MCP9600/MCP9601 backend only;
                                  MAX6675 remains available
     HAL_DISABLE_MAX6675        - MAX6675 backend only; MCP9600 remains available
     HAL_DISABLE_UART           - Hardware UART (SerialUART)
     HAL_DISABLE_SWSERIAL       - SoftwareSerial
     HAL_DISABLE_I2C            - I2C bus (Wire, master mode)
     HAL_DISABLE_I2C_SLAVE      - I2C slave/target mode with register map
     HAL_DISABLE_EXTERNAL_ADC   - ADS1115 external ADC (depends on I2C)
     HAL_DISABLE_PWM_FREQ       - Frequency-controlled PWM
     HAL_DISABLE_RGB_LED        - NeoPixel RGB status LED
     HAL_DISABLE_CAN            - MCP2515 CAN bus
     HAL_DISABLE_DISPLAY        - TFT / OLED display (excludes both backends)
     HAL_DISABLE_TFT            - SPI TFT drivers only (ILI9341/ST7789/ST7735/ST7796S);
                                  SSD1306 remains available
     HAL_DISABLE_SSD1306        - SSD1306 OLED driver only; TFT remains available
     HAL_DISABLE_ILI9341        - ILI9341 TFT driver only
     HAL_DISABLE_ST7789         - ST7789 TFT driver only
     HAL_DISABLE_ST7735         - ST7735 TFT driver only
     HAL_DISABLE_ST7796S        - ST7796S TFT driver only
     HAL_DISABLE_UNITY          - disable bundled Unity framework includes/sources

   Dependency propagation - disabling a base module automatically
   disables modules that depend on it:                                   */

#ifdef HAL_DISABLE_EEPROM
  #ifndef HAL_DISABLE_KV
    #define HAL_DISABLE_KV
  #endif
#endif

#ifdef HAL_DISABLE_WIFI
  #ifndef HAL_DISABLE_TIME
    #define HAL_DISABLE_TIME
  #endif
#endif

#ifdef HAL_DISABLE_I2C
  #ifndef HAL_DISABLE_EXTERNAL_ADC
    #define HAL_DISABLE_EXTERNAL_ADC
  #endif
#endif

#ifdef HAL_DISABLE_SWSERIAL
  #ifndef HAL_DISABLE_GPS
    #define HAL_DISABLE_GPS
  #endif
#endif

#if defined(HAL_DISABLE_MCP9600) && defined(HAL_DISABLE_MAX6675)
  #ifndef HAL_DISABLE_THERMOCOUPLE
    #define HAL_DISABLE_THERMOCOUPLE
  #endif
#endif

#if defined(HAL_DISABLE_ILI9341) && defined(HAL_DISABLE_ST7789) && \
    defined(HAL_DISABLE_ST7735) && defined(HAL_DISABLE_ST7796S)
  #ifndef HAL_DISABLE_TFT
    #define HAL_DISABLE_TFT
  #endif
#endif

#if defined(HAL_DISABLE_TFT) && defined(HAL_DISABLE_SSD1306)
  #ifndef HAL_DISABLE_DISPLAY
    #define HAL_DISABLE_DISPLAY
  #endif
#endif

/* ── Module enable flags (opt-in) ────────────────────────────────────── */
/* These pull in optional code that is OFF by default. Define them in
   `hal_project_config.h` (or via `-D`) when the project actually uses the
   corresponding API.

   Supported flags:
     HAL_ENABLE_CJSON           - bundled cJSON / cJSON_Utils sources.
     HAL_ENABLE_CRYPTO          - `hal_crypto` (Base64, MD5, SHA-256,
                                  HMAC-SHA256, ChaCha20 / -Poly1305) and
                                  the dependent `hal_sc_auth` helper.
                                  Without this flag the headers expand
                                  to nothing and the implementation TUs
                                  produce empty objects.
                                  `hal_serial_session` keeps working -
                                  the `SC_AUTH_BEGIN` / `SC_AUTH_PROVE`
                                  handlers are simply compiled out and
                                  the session never enters the
                                  authenticated state.                   */

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
#define PROGMEM   /* no-op on platforms without separate flash address space */
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
#define F(s) (s)  /* mock build: F() is a no-op identity */
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
 * @def HAL_THERMOCOUPLE_MAX_INSTANCES
 * Maximum number of simultaneous thermocouple instances (MCP9600 / MAX6675).
 * Each slot holds one driver object placed in a static storage buffer.
 * Override with -DHAL_THERMOCOUPLE_MAX_INSTANCES=N if more are needed.
 */
#ifndef HAL_THERMOCOUPLE_MAX_INSTANCES
#define HAL_THERMOCOUPLE_MAX_INSTANCES 4
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
    int pwm_freq_max_channels;    /**< Effective PWM-freq channel limit.  */
    int can_max_instances;        /**< Effective CAN instance limit.      */
    int uart_max_instances;       /**< Effective hardware UART limit.     */
    int swserial_max_instances;   /**< Effective SoftwareSerial limit.    */
    int mock_can_max_inst;        /**< Mock CAN instance limit.           */
    int mock_can_buf_size;        /**< Mock CAN ring-buffer depth.        */
    int mock_max_alarms;          /**< Mock timer alarm limit.            */
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
const hal_config_t* hal_get_config(void);

#ifdef __cplusplus
}
#endif

/* ── Debug-assert mechanism ──────────────────────────────────────────── */

/**
 * @def HAL_ASSERT(cond, msg)
 * Lightweight assert for HAL resource exhaustion.
 *
 * When the condition is false the macro prints @p msg through the
 * serial debug channel.  On the real target it then enters an
 * infinite loop so the watchdog can reset the system; in mock/test
 * builds it calls @c abort().
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

#ifdef ARDUINO
  /* Real target - print and hang (watchdog will fire).                */
  #include <Arduino.h>
  #define HAL_ASSERT(cond, msg)                                       \
      do {                                                            \
          if (!(cond)) {                                              \
              Serial.print("HAL ASSERT FAIL: ");                      \
              Serial.println(msg);                                    \
              for (;;) { /* let watchdog bite */ }                     \
          }                                                           \
      } while (0)
#else
  /* Mock / hosted build - print to stderr and abort.                  */
  #include <stdio.h>
  #include <stdlib.h>
  #define HAL_ASSERT(cond, msg)                                       \
      do {                                                            \
          if (!(cond)) {                                              \
              fprintf(stderr, "HAL ASSERT FAIL: %s\n", (msg));        \
              abort();                                                \
          }                                                           \
      } while (0)
#endif

#endif /* HAL_DISABLE_ASSERTS */
