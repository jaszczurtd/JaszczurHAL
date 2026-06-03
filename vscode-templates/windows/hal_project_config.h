#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration - project template.
 *
 * Copy this file to your sketch (project) directory and uncomment the
 * HAL_ENABLE_* flags for the modules your project uses.
 *
 * JaszczurHAL uses an OPT-IN model: by default *no* optional module is
 * compiled. Every module the project relies on must be explicitly enabled
 * here (or via -D on the command line). Unused modules cost zero code,
 * zero RAM, and pull in no third-party libraries via arduino-cli.
 *
 * Dependency propagation is automatic - e.g. enabling HAL_ENABLE_PCF8563
 * also enables HAL_ENABLE_RTC and HAL_ENABLE_I2C, enabling HAL_ENABLE_MQTT
 * also enables HAL_ENABLE_WIFI. You only need to enable the *leaf* module
 * you actually use. Facade modules (RTC, THERMOCOUPLE, DISPLAY, TFT) must
 * have at least one backend selected; the consistency check in
 * hal_config.h will emit a clear #error if you forget.
 *
 * Build requirement: the sketch directory must be on the compiler include
 * path. Add these flags to your arduino-cli invocation:
 *   --build-property "compiler.cpp.extra_flags=-I '/path/to/sketch'"
 *   --build-property "compiler.c.extra_flags=-I '/path/to/sketch'"
 * The provided tasks.json already does this via ${workspaceFolder}.
 */

/* ── Connectivity ──────────────────────────────────────────────────────── */
// #define HAL_ENABLE_WIFI            /* WiFi - needs PICO_W               */
// #define HAL_ENABLE_TIME            /* NTP / system time -> WiFi          */
// #define HAL_ENABLE_MQTT            /* PubSubClient wrapper -> WiFi       */
// #define HAL_ENABLE_UDP             /* WiFiUDP wrapper      -> WiFi       */
// #define HAL_ENABLE_OTA             /* ArduinoOTA wrapper   -> WiFi       */
// #define HAL_ENABLE_WIREGUARD       /* WireGuard wrapper    -> WiFi       */

/* ── Storage ──────────────────────────────────────────────────────────── */
// #define HAL_ENABLE_EEPROM          /* EEPROM / AT24C256                  */
// #define HAL_ENABLE_KV              /* Key-value store     -> EEPROM      */
// #define HAL_ENABLE_LITTLEFS        /* LittleFS helpers                   */

/* ── Buses ────────────────────────────────────────────────────────────── */
// #define HAL_ENABLE_UART            /* Hardware UART (SerialUART)         */
// #define HAL_ENABLE_SWSERIAL        /* SoftwareSerial                     */
// #define HAL_ENABLE_I2C             /* I2C master (Wire)                  */
// #define HAL_ENABLE_I2C_SLAVE       /* I2C slave / target                 */
// #define HAL_ENABLE_CAN             /* MCP2515 CAN bus                    */

/* ── Time-of-day (RTC) ────────────────────────────────────────────────── */
/* Enable a backend; HAL_ENABLE_RTC is propagated automatically.            */
// #define HAL_ENABLE_PCF8563         /* PCF8563 RTC backend  -> RTC, I2C   */
// #define HAL_ENABLE_DS3231          /* DS3231 RTC backend   -> RTC, I2C   */

/* ── Sensors ──────────────────────────────────────────────────────────── */
/* Thermocouple - enable a backend; HAL_ENABLE_THERMOCOUPLE is propagated.  */
// #define HAL_ENABLE_MCP9600         /* MCP9600/MCP9601 -> THERMOCOUPLE,I2C*/
// #define HAL_ENABLE_MAX6675         /* MAX6675         -> THERMOCOUPLE    */
// #define HAL_ENABLE_DS18B20         /* DS18B20 1-Wire  -> ONEWIRE         */
// #define HAL_ENABLE_ONEWIRE         /* Raw 1-Wire bus API                 */
// #define HAL_ENABLE_EXTERNAL_ADC    /* ADS1115         -> I2C             */
// #define HAL_ENABLE_GPS             /* NMEA GPS; needs SWSERIAL or UART   */

/* ── PWM / status ─────────────────────────────────────────────────────── */
// #define HAL_ENABLE_PWM_FREQ        /* Frequency-controlled PWM           */
// #define HAL_ENABLE_RGB_LED         /* NeoPixel RGB status LED            */

/* ── Display (fasada + backend) ───────────────────────────────────────── */
/* Enable a driver; HAL_ENABLE_TFT and HAL_ENABLE_DISPLAY are propagated.   */
// #define HAL_ENABLE_ILI9341         /* ILI9341 TFT  -> TFT, DISPLAY       */
// #define HAL_ENABLE_ST7789          /* ST7789 TFT   -> TFT, DISPLAY       */
// #define HAL_ENABLE_ST7735          /* ST7735 TFT   -> TFT, DISPLAY       */
// #define HAL_ENABLE_ST7796S         /* ST7796S TFT  -> TFT, DISPLAY       */
// #define HAL_ENABLE_SSD1306         /* SSD1306 OLED -> DISPLAY            */

/* ── Crypto / bundled libs ────────────────────────────────────────────── */
// #define HAL_ENABLE_CRYPTO          /* hal_crypto: Base64, MD5, SHA-256,
//                                       HMAC-SHA256, ChaCha20-Poly1305.
//                                       Also enables hal_sc_auth.          */
// #define HAL_ENABLE_CJSON           /* bundled cJSON / cJSON_Utils        */

/* ── Asserts (opt-OUT, like NDEBUG) ───────────────────────────────────── */
/* Asserts are ON by default. Uncomment to compile them out in release.    */
// #define HAL_DISABLE_ASSERTS
