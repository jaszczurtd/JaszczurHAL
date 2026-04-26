#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration — project template.
 *
 * Copy this file to your sketch (project) directory and uncomment the
 * HAL_DISABLE_* flags for modules your project does not use.
 *
 * This file is automatically picked up by hal_config.h via __has_include.
 * Dependency propagation (e.g. EEPROM → KV) is handled by hal_config.h —
 * you only need to disable the base module.
 *
 * Build requirement: the sketch directory must be on the compiler include
 * path. Add these flags to your arduino-cli invocation:
 *   --build-property "compiler.cpp.extra_flags=-I '/path/to/sketch'"
 *   --build-property "compiler.c.extra_flags=-I '/path/to/sketch'"
 * The provided tasks.json already does this via ${workspaceFolder}.
 */

/* ── Opt-in modules (uncomment to enable) ─────────────────────────────── */

// #define HAL_ENABLE_CRYPTO           /* hal_crypto: Base64, MD5, SHA-256,
//                                        HMAC-SHA256, ChaCha20-Poly1305.
//                                        Also enables hal_sc_auth (per-
//                                        device key + SC_AUTH handshake
//                                        on top of hal_serial_session).   */
// #define HAL_ENABLE_CJSON            /* bundled cJSON / cJSON_Utils       */

/* ── Uncomment to disable ─────────────────────────────────────────────── */

// #define HAL_DISABLE_WIFI            /* WiFi — needs PICO_W               */
// #define HAL_DISABLE_EEPROM          /* EEPROM / AT24C256 → propagates KV */
// #define HAL_DISABLE_GPS             /* GPS / NMEA (TinyGPS++)            */
// #define HAL_DISABLE_THERMOCOUPLE    /* MCP9600 / MAX6675                 */
// #define HAL_DISABLE_UART            /* Hardware UART (SerialUART)        */
// #define HAL_DISABLE_SWSERIAL        /* SoftwareSerial → propagates GPS   */
// #define HAL_DISABLE_I2C             /* I2C (Wire) → propagates EXT. ADC  */
// #define HAL_DISABLE_EXTERNAL_ADC    /* ADS1115 external ADC              */
// #define HAL_DISABLE_PWM_FREQ        /* Frequency-controlled PWM          */
// #define HAL_DISABLE_RGB_LED         /* NeoPixel RGB status LED            */
// #define HAL_DISABLE_CAN             /* MCP2515 CAN bus                    */
// #define HAL_DISABLE_DISPLAY         /* TFT / OLED display                 */
// #define HAL_DISABLE_KV              /* Key-value store (needs EEPROM)    */
// #define HAL_DISABLE_TIME            /* NTP / system time (needs WiFi)    */
