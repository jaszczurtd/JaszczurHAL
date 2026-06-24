# ESP32 Porting Progress

Last updated: 2026-06-24

## Current Status

- ESP32 backend is planned, but not implemented yet.
- No `HAL_TARGET_ESP32` target exists in `src/hal/hal_target.h`.
- No `src/hal/impl/esp32_arduino/` backend folder exists yet.
- No ESP32 static-library build or ESP32 example build flow exists yet.
- The port is technically feasible, but it is a real backend port, not a small
  flag change.
- Recommended carrier: Arduino-ESP32, because it provides familiar Arduino
  APIs, WiFi, UDP, OTA and FreeRTOS on top of ESP-IDF.

## What Must Be Added First

- Add `HAL_TARGET_ESP32` and `HAL_TARGET_IS_ESP32`.
- Auto-detect ESP32 from `ARDUINO_ARCH_ESP32` / `ESP32`.
- Extend exactly-one-target validation.
- Extend `HAL_TARGET_NAME`.
- Add an ESP32 branch in `src/hal_app_entry.cpp`.
- Allow ESP32 in `HAL_ENABLE_FREERTOS` checks.
- Add `esp32` to `library.properties`.
- Create `src/hal/impl/esp32_arduino/`.
- Add ESP32 build scripts and CMake/static-library support.
- Add ESP32 example build support.

## Reusable Pieces

- Shared drivers in `src/hal/impl/shared/` should mostly work once ESP32 has
  working HAL primitives and target guards are widened.
- Good reusable shared-driver candidates:
  - ADS1x15
  - BH1750
  - DS18B20 / OneWire
  - DS3231 / PCF8563
  - MCP9600 / MAX6675
  - MCP401x / MAX5395 digipots
  - MCP2515 / MCP251XFD CAN over SPI
  - ILI9341 / ST77xx / SSD1306 display logic
  - PGA2311
  - GPS NMEA parser
  - WireGuard crypto
- HAL-level portable modules should also carry over where they only depend on
  shared HAL interfaces.

## Modules That Need Real ESP32 Backends

- `hal_system`
  - Use `esp_timer_get_time`, ESP-IDF delay APIs, heap APIs, reset reason,
    watchdog and MAC/efuse identity helpers.
- `hal_sync`
  - Use FreeRTOS mutexes and ESP-IDF critical-section primitives.
- `hal_gpio`
  - Use Arduino GPIO or ESP-IDF GPIO APIs, including interrupt support.
- `hal_i2c`
  - Implement with Arduino `Wire` / `Wire1` or ESP-IDF I2C master.
- `hal_spi`
  - Implement with Arduino `SPIClass` or ESP-IDF SPI master.
- `hal_timer`
  - Implement with `esp_timer`.
- `hal_pwm` / `hal_pwm_freq`
  - Use LEDC.
- `hal_rgb_led`
  - Use RMT or an ESP32-compatible NeoPixel path.
- `hal_pcnt`
  - Use ESP32 PCNT where available; stub or fallback on variants without PCNT.
- `hal_i2c_slave`
  - Needs an ESP-IDF I2C slave implementation.
- `hal_dac`
  - ESP32 has an 8-bit DAC on GPIO25/GPIO26.
- `hal_wireguard`
  - Needs an ESP32 WireGuard library evaluation and integration.
- `hal_littlefs`
  - Must use ESP32 partition-table based LittleFS, not RP2040 flash offsets.

## Likely Easy Ports

- `hal_serial`
- `hal_uart`
- `hal_adc`
- `hal_wifi`
- `hal_udp`
- `hal_mqtt`
- `hal_ota`
- `hal_time`
- `hal_swserial`
- `hal_eeprom`
- `hal_gps`
- `hal_rtc`
- `hal_thermocouple`
- `hal_sdlogger`

These still need compile validation under Arduino-ESP32. Some may only need
guard changes, but they should not be marked complete before an ESP32 build
exists.

## Suggested Porting Order

- Phase 1: target scaffolding.
  - Add target macros, app entry, config guards, backend folder, build scripts
    and stub implementations.
  - Goal: ESP32 firmware compiles with no-op / unsupported backend stubs.

- Phase 2: core HAL.
  - Implement `hal_system`, `hal_sync`, `hal_gpio`, `hal_serial`, `hal_uart`,
    `hal_adc`, `hal_i2c`, `hal_spi` and `hal_timer`.
  - Goal: blink, debug serial, I2C scan and SPI smoke tests work.

- Phase 3: connectivity.
  - Implement or enable `hal_wifi`, `hal_udp`, `hal_mqtt`, `hal_ota` and
    `hal_time`.
  - Goal: WiFi, MQTT, OTA and NTP examples work.

- Phase 4: device modules.
  - Enable shared I2C/SPI/GPIO-based drivers after the core HAL is stable.
  - Goal: RTC, thermocouple, ADC, digipot, display and external CAN paths build
    and run where hardware is available.

- Phase 5: advanced modules.
  - Add PWM frequency, RGB LED, PCNT, I2C slave, LittleFS, WireGuard and SDLOG.
  - Goal: feature parity where ESP32 hardware and libraries support it.

## Main Risks

- ESP32 is a family, not one chip.
  - Start with original ESP32 and ESP32-S3.
  - Stub or conditionally support features missing on ESP32-C3/C6/H2.

- FreeRTOS semantics differ from RP2040.
  - On ESP32, FreeRTOS is always present.
  - `HAL_ENABLE_FREERTOS` should be treated as documentation or feature policy,
    not as a separate runtime selection.

- Dual-core task placement must be decided.
  - `app_task0()` can use the normal Arduino `loop()`.
  - `app_task1()` should likely be a FreeRTOS task pinned to a chosen core.

- Flash filesystem layout differs.
  - ESP32 LittleFS depends on partition tables.
  - RP2040-style fixed flash offsets do not apply.

- WireGuard is not portable as-is.
  - The current Pico W WireGuard library cannot be reused directly.
  - ESP32 alternatives must be evaluated before enabling `hal_wireguard`.

## Minimum Done Definition

- `HAL_TARGET_ESP32` builds without affecting RP2040, STM32G474 or mock builds.
- ESP32 examples compile through the supported example build flow.
- Blink, serial, delay, GPIO input, I2C and SPI smoke tests run on hardware.
- WiFi + UDP or WiFi + MQTT works on hardware.
- Shared drivers are enabled only after their dependency HAL modules are tested.
- `./runalltests.sh` still passes for existing targets when toolchains are
  available.
