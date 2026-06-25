# JaszczurHAL - API Reference

Hardware Abstraction Layer for embedded projects.
The primary backend is RP2040 via Arduino-pico, with STM32G474 available as a
real bare-metal backend for core domains and an expanding shared-driver stack,
while keeping the application-facing HAL API stable across targets.

This document is the established, detailed API reference.
The top-level [README.md](../README.md) intentionally stays concise and links
here for full behavior/contracts.

Current RP2040 backend requirement: Earle Philhower Arduino core for RP2040/RP2350
(arduino-pico): https://github.com/earlephilhower/arduino-pico
Minimum version for RP2350 support: 4.0.0 (latest stable recommended).

**Author:** Marcin 'Jaszczur' Kielesiński

**Repository:** `git@github.com:jaszczurtd/JaszczurHAL.git`
**Include root:** `libraries/JaszczurHAL/src/` (registered in `otherLibrariesFolders`)
**Public include:** `#include <JaszczurHAL.h>`
**Internal HAL-only include:** `#include <hal/hal.h>`

---

## Library structure

- `CMakeLists.txt` - repository-root host/mock tests build.
- `rp2040_lib/` - RP2040 Arduino-pico static-library CMake glue.
- `stm32_lib/` - STM32G474 static-library CMake, toolchain file, and linker script.
- `scripts/build_rp2040_lib.sh` - RP2040 static-library helper.
- `scripts/build_stm32_lib.sh` - STM32G474 static-library helper.
- `scripts/ensure_freertos_kernel.sh` - shared helper for fetching/verifying
  the pinned `third_party/FreeRTOS-Kernel` checkout.
- `runalltests.sh` - full local validation gate.
- `runmefirst.sh` - one-time local toolchain setup.
- `src/JaszczurHAL.h` - umbrella include for HAL + utility modules.
- `src/HAL_FLAGS.txt` - concise `HAL_ENABLE_*` flag summary.
- `src/libConfig.h` - backward-compat redirect to `hal/hal_config.h`.
- `src/tools.h` - C++ utility aggregator.
- `src/tools_c.h` - C-compatible utility declarations.
- `src/hal/hal.h` - HAL-only umbrella include.
- `src/hal/hal_config.h` and `src/hal/hal_config.cpp` - build-time feature flags and runtime config helpers.
- `src/hal/*.h` - public HAL module interfaces such as GPIO, ADC, PWM, timers, sync, serial, crypto, I2C, SPI, OneWire, CAN, display, thermocouple/DS18B20 sensors, RTC, GPS, EEPROM, SD logger, WiFi, UDP, WireGuard, MQTT, and time.
- `src/hal/hal_can_util.cpp`, `src/hal/hal_crypto.cpp`, `src/hal/hal_kv.cpp`, `src/hal/hal_pga2311.cpp`, `src/hal/hal_soft_timer.cpp`, `src/hal/hal_pid_controller.cpp` - shared HAL wrapper implementations.
- `src/hal/hal_uart_config.h` - UART configuration constants and helpers.
- `src/hal/impl/rp2040/` - RP2040 backend.
- `src/hal/impl/stm32g474/` - STM32G474 backend (real register-level core domains; remaining modules in progress).
- `src/hal/impl/.mock/` - deterministic host-test backend.
- `src/hal/impl/shared/` - internal backend-agnostic code reused by multiple hardware backends. Hardware-oriented modules live under `drivers/` (`ads1x15/`, `digipot/`, `display/`, `ds18b20/`, `ds3231/`, `max6675/`, `mcp2515/`, `mcp251xfd/`, `mcp9600/`, `neopixel/`, `onewire/`, `pcf8563/`, `pga2311/`, etc.). Larger reusable stacks and engines live under `frameworks/` (`cjson/`, `filesystem/`, `gps/`, `irsmall_decoder/`, `jpeg/`, `lodepng/`, `smart_timers/`, `wireguard/crypto/`).
- `src/hal/impl/rp2040/drivers/` - bundled low-level third-party drivers used by optional HAL modules.
- `src/hal/impl/rp2040/drivers/rp2040/` - SoC-specific drivers: `rp2040_fault.{h,cpp}` (HardFault capture, stack guard, reset-reason latch) and `rp2040_system.{h,cpp}` (watchdog, USB-boot entry, on-die temperature, free-heap, unique board id, idle hint).
- `src/hal/impl/stm32g474/drivers/stm32g474/` - SoC-specific drivers: `stm32g474_fault.{h,cpp}` (reset-reason classification, retained fault handoff, stack guard) and `stm32g474_system.{h,cpp}` (time, delay, watchdog, temperature, UID, idle / ISR-sensitive helpers).
- `src/hal/impl/rp2040/frameworks/` - bundled high-level integration frameworks (`arduino-wireguard-pico-w`, `PubSubClient`).
- `src/utils/` - higher-level utilities: `tools`, `pidController`, `multicoreWatchdog`, `draw7Segment`, and bundled Unity sources.

`JaszczurHAL.h` is the current top-level public include and should be the
default include in project code. `hal/hal.h` remains available as a HAL-only
aggregator, but it is not the primary include exported by the current library
metadata.

---

## Memory maps

Target-specific memory layout notes live next to the build glue for each
backend:

- [RP2040 memory map](../rp2040_lib/MEMORY_MAP.md) - arduino-pico generated
  linker layout, flash/FS/EEPROM markers, SRAM regions, and stack overrides.
- [STM32G474 memory map](../stm32_lib/MEMORY_MAP.md) - bare-metal linker
  regions, reserved flash EEPROM/KV pages, RAM sections, heap, and stack.

---

## Documentation scope

This file is the API-oriented companion to [README.md](../README.md).

Recommended split of responsibilities:

- [README.md](../README.md): overview, architecture, quick start, build/test entry points, practical examples
- `doc/JaszczurHAL_API.md`: module layout, migration notes, public API details, feature-flag reference

Where both documents touch the same topic, [README.md](../README.md) should be
treated as the short onboarding guide, while this file stays focused on
reference material.

---

## Public API vs helper modules

The repository contains both the HAL itself and a set of utility modules.

### HAL public API

These are the portability-oriented interfaces intended to decouple application
logic from Arduino and other board-specific SDK calls:

- `hal_gpio`, `hal_adc`, `hal_pwm`, `hal_pwm_freq`
- `hal_timer`, `hal_soft_timer`, `hal_system`, `hal_bits`, `hal_sync`, `hal_serial`
- `hal_crypto`
- `hal_pid_controller`
- `hal_uart`, `hal_swserial`, `hal_spi`, `hal_i2c`, `hal_onewire`
- `hal_can`, `hal_display`, `hal_rgb_led`
- `hal_thermocouple`, `hal_ds18b20`, `hal_rtc`, `hal_external_adc`, `hal_gps`, `hal_digipot`, `hal_pga2311`
- `hal_eeprom`, `hal_kv`, `hal_sdlogger`, `hal_wifi`, `hal_littlefs`, `hal_udp`, `hal_wireguard`, `hal_mqtt`, `hal_ota`, `hal_time`
- `hal_time_from_components(...)` for deterministic date/time-to-epoch conversion
- optional timestamp hook for error logging via `hal_debug_set_timestamp_hook(...)`

### Helper / utility modules

These are convenient adjuncts, but they are not the portability boundary
itself:

- `tools`
- `SmartTimers`
- `pidController`
- `multicoreWatchdog`
- `draw7Segment`

When designing new application code, prefer depending on the HAL layer first.
Helper modules are useful building blocks, but they should not replace the HAL
boundary conceptually.

---

---

## API reference sections

Detailed per-module reference is split across the following files in the `api/` subfolder:

| # | File | Contents |
|---|------|----------|
| 1 | [Library compilation guide](lib_compilation.md) | Building for all targets (host/mock, RP2040/Arduino, STM32G474 bare-metal), static-library helpers, FreeRTOS build variants, CMake presets, stack-size overrides, linking with a firmware project |
| 2 | [Module flags and configuration](api/02_module_flags.md) | `HAL_ENABLE_*` opt-in flags, dependency propagation, FreeRTOS flag, stack-size overrides, core modules, `library.properties` note |
| 3 | [Build dependencies and unit tests](api/03_build_tests.md) | Hardware and mock/PC dependency tables, ctest build/run instructions, full test-suite inventory, how to add a new test suite, mock time control |
| 4 | [Multicore safety, drivers, migration](api/04_multicore_drivers_migration.md) | Multicore init/runtime rules, bundled driver inventory and licences, logging timestamp hook, time conversion helper, examples overview, host-test coverage, migration table from Arduino/pico SDK |
| 5 | [GPIO, ADC and PWM](api/05_gpio_adc_pwm.md) | `hal_gpio`, `hal_pwm`, `hal_pwm_freq`, `hal_adc` |
| 6 | [Timers and system](api/06_timers_system.md) | `hal_timer` (alarms + managed timers), `hal_system` (millis/watchdog/crash diagnostics/UID), `hal_bits`, `hal_math` |
| 7 | [Cryptography](api/07_crypto.md) | `hal_crypto` - Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20, ChaCha20-Poly1305 |
| 8 | [Sync, serial, framing and auth](api/08_sync_serial.md) | `hal_sync` (mutex/critical-section), `hal_serial` (TX-serialized console output, streamed debug formatting, ISR-deferred logging, rate-limiter), `hal_serial_session` (framed SC protocol), `hal_serial_frame` (wire codec), `hal_sc_auth` (HMAC challenge/response) |
| 9 | [Communication buses](api/09_buses.md) | `hal_spi`, `hal_i2c` (master, one-shot helpers, bus-clear), `hal_i2c_slave` (register map), `hal_uart`, `hal_swserial`, `hal_onewire` |
| 10 | [CAN bus and display](api/10_can_display.md) | `hal_can` (backend-selected CAN: MCP2515 classic CAN, MCP251XFD CAN FD, and STM32G474 native FDCAN), `hal_display` (ILI9341, ST77xx, SSD1306, GFX primitives, text, fonts) |
| 11 | [Sensors](api/11_sensors.md) | `hal_thermocouple` (MCP9600/MAX6675), `hal_ds18b20` (non-blocking workflow), `hal_bh1750` (ambient light), `hal_rtc` (PCF8563/DS3231), `hal_external_adc` (ADS1115), `hal_gps` (NMEA, auto-detect framing) |
| 12 | [Cellular modem](api/12_modem.md) | `hal_modem_at` (AT engine, URC, watchdog cooperation), `hal_simcom_a76xx` (A7670/A7672 - power, boot, SIM, PDP, LBS, GNSS, MQTT subscribe) |
| 13 | [Output devices](api/13_output_devices.md) | `hal_rgb_led` (NeoPixel, PIO/GPIO transport), `hal_pga2311` (stereo volume controller), `hal_math` (constrain, map, roundToN) |
| 14 | [Storage](api/14_storage.md) | `hal_eeprom` (target flash / AT24C256), `hal_kv` (append-only KV store with GC), `hal_littlefs` (LittleFS mount/format helpers), `hal_sdlogger` (SD-card buffered logger and crash reporter) |
| 15 | [Network connectivity](api/15_connectivity.md) | `hal_wifi`, `hal_udp`, `hal_tcp`, BSD sockets adapter with IPv4 `getaddrinfo()`, `hal_wireguard`, `hal_mqtt`, `hal_ota`, `hal_time` (NTP/local time) |
| 16 | [Utilities](api/16_utilities.md) | `hal_soft_timer` (C wrapper over SmartTimers), `hal_pid_controller` (C wrapper over pidController), `tools.h/cpp` helper functions, `SmartTimers`, `pidController`, `multicoreWatchdog`, `draw7Segment` |
| 17 | [cJSON](api/17_cJSON.md) | Bundled `cJSON` / `cJSON_Utils`, include patterns, ownership rules, parsing, printing, JSON Pointer/Patch/Merge Patch examples |
| 18 | [LodePNG](api/18_LodePNG.md) | Bundled `LodePNG`, include patterns, embedded profile, memory ownership, PNG/Base64 asset script and RGB565 examples |
| 19 | [JPEG](api/19_JPEG.md) | Bundled `JPEGDecoder` / `picojpeg`, include patterns, embedded profile, memory ownership, JPEG/Base64 asset script and RGB565 examples |

---

## Quick module lookup

| Module | Section |
|--------|---------|
| `hal_adc` | [GPIO, ADC and PWM](api/05_gpio_adc_pwm.md) |
| `hal_bh1750` | [Sensors](api/11_sensors.md) |
| `hal_bits` | [Timers and system](api/06_timers_system.md) |
| `hal_can` | [CAN and display](api/10_can_display.md) |
| `hal_crypto` | [Cryptography](api/07_crypto.md) |
| `cJSON` / `cJSON_Utils` | [cJSON](api/17_cJSON.md) |
| `LodePNG` | [LodePNG](api/18_LodePNG.md) |
| `JPEGDecoder` / `picojpeg` | [JPEG](api/19_JPEG.md) |
| `hal_digipot` | [Module flags](api/02_module_flags.md) (flag table) |
| `hal_display` | [CAN and display](api/10_can_display.md) |
| `hal_ds18b20` | [Sensors](api/11_sensors.md) |
| `hal_eeprom` | [Storage](api/14_storage.md) |
| `hal_external_adc` | [Sensors](api/11_sensors.md) |
| `hal_gps` | [Sensors](api/11_sensors.md) |
| `hal_gpio` | [GPIO, ADC and PWM](api/05_gpio_adc_pwm.md) |
| `hal_i2c` / `hal_i2c_slave` | [Communication buses](api/09_buses.md) |
| `hal_kv` | [Storage](api/14_storage.md) |
| `hal_littlefs` | [Storage](api/14_storage.md) |
| `hal_math` | [Timers and system](api/06_timers_system.md) / [Output devices](api/13_output_devices.md) |
| `hal_modem_at` | [Cellular modem](api/12_modem.md) |
| `hal_mqtt` | [Network connectivity](api/15_connectivity.md) |
| `hal_onewire` | [Communication buses](api/09_buses.md) |
| `hal_ota` | [Network connectivity](api/15_connectivity.md) |
| `hal_pga2311` | [Output devices](api/13_output_devices.md) |
| `hal_pid_controller` | [Utilities](api/16_utilities.md) |
| `hal_pwm` / `hal_pwm_freq` | [GPIO, ADC and PWM](api/05_gpio_adc_pwm.md) |
| `hal_rgb_led` | [Output devices](api/13_output_devices.md) |
| `hal_rtc` | [Sensors](api/11_sensors.md) |
| `hal_sc_auth` | [Sync, serial, framing](api/08_sync_serial.md) |
| `hal_sdlogger` | [Storage](api/14_storage.md) |
| `hal_serial` | [Sync, serial, framing](api/08_sync_serial.md) |
| `hal_serial_frame` | [Sync, serial, framing](api/08_sync_serial.md) |
| `hal_serial_session` | [Sync, serial, framing](api/08_sync_serial.md) |
| `hal_simcom_a76xx` | [Cellular modem](api/12_modem.md) |
| `hal_soft_timer` | [Utilities](api/16_utilities.md) |
| `hal_spi` | [Communication buses](api/09_buses.md) |
| `hal_swserial` | [Communication buses](api/09_buses.md) |
| `hal_sync` | [Sync, serial, framing](api/08_sync_serial.md) |
| `hal_system` | [Timers and system](api/06_timers_system.md) |
| `hal_thermocouple` | [Sensors](api/11_sensors.md) |
| `hal_time` | [Network connectivity](api/15_connectivity.md) |
| `hal_timer` | [Timers and system](api/06_timers_system.md) |
| `hal_uart` | [Communication buses](api/09_buses.md) |
| `hal_udp` | [Network connectivity](api/15_connectivity.md) |
| `hal_tcp` | [Network connectivity](api/15_connectivity.md) |
| BSD sockets adapter | [Network connectivity](api/15_connectivity.md) |
| `hal_wifi` | [Network connectivity](api/15_connectivity.md) |
| `hal_wireguard` | [Network connectivity](api/15_connectivity.md) |
| `multicoreWatchdog` | [Utilities](api/16_utilities.md) |
| `pidController` | [Utilities](api/16_utilities.md) |
| `SmartTimers` | [Utilities](api/16_utilities.md) |
| `draw7Segment` | [Utilities](api/16_utilities.md) |
