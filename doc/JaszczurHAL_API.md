# JaszczurHAL - API Reference

Hardware Abstraction Layer for embedded projects.
The RP2040/RP2350 backend builds against the official Pico SDK. STM32G474 is
available as a bare-metal or FreeRTOS backend with native peripheral support
and the shared driver stack. The application-facing HAL API stays stable
across targets.

This document is the established, detailed API reference.
The top-level [README.md](../README.md) intentionally stays concise and links
here for full behavior/contracts.

**Author:** Marcin 'Jaszczur' Kielesiński

**Repository:** `git@github.com:jaszczurtd/JaszczurHAL.git`
**Include root:** `libraries/JaszczurHAL/src/` (registered in `otherLibrariesFolders`)

---

## Public include

Use:

```cpp
#include <JaszczurHAL.h>
```

The internal header can be used for advanced/internal usage.

```cpp
#include <hal/hal.h>
```

Utility-only includes are also available:

```cpp
#include <tools.h>    // C++ utility aggregator
```

```c
#include <tools_c.h>  // C-compatible utility API
```

---

## Library structure

```text
CMakeLists.txt              # host/mock tests build
VERSION                     # project version
.build/                     # ignored root for all managed build artifacts
boards/                     # target, board and capability descriptors
config/                     # declarative HAL feature registry and schema
rp_native_lib/              # Pico SDK RP2040/RP2350 static-library build
  MEMORY_MAP.md             # native RP firmware/storage/OTA layout
cmake/
  generated/                # generated production CMake feature resolver
  jh_rp_native_sdk.cmake    # shared RP library/firmware CMake glue
  targets/                  # VS Code dispatcher target recipes
stm32_lib/                  # STM32G474 static-library CMake, toolchain, linker script
scripts/
  # See doc/api/00_scripts.md for the complete process-script reference.
  build_rp_native_lib.sh    # RP ELF/BIN/UF2 build helper
  build_stm32_lib.sh        # STM32G474 static-library helper
  check_documentation_links.py # local Markdown link/anchor validation
  ensure_*.sh               # focused pinned-component fetch/verify helpers
  generate_sbom.py          # CycloneDX SBOM generator
  generate_hal_features.py  # feature registry validation, generation and lint
  check_vulnerabilities.sh  # optional local vulnerability scanner wrapper
runalltests.sh              # full local validation gate
runmefirst.sh               # one-time local toolchain setup
doc/
  JaszczurHAL_API.md        # detailed API/reference
  api/                      # split API chapters
    00_scripts.md           # essential process and orchestration architecture
  FwProjectWorkflow.md      # dispatcher-backed firmware project workflow
  OTAWorkflow.md            # native RP OTA build, upload, firewall and recovery
  HAL_FLAGS.txt             # HAL_ENABLE_* flag summary
  lib_compilation.md        # static-library build guide
  features.md               # high-level feature matrix
  CHANGELOG.md              # project changelog
  datasheets/               # local reference PDFs and notes
  security_supply_chain.md  # SBOM and vulnerability tracking process
examples/                   # buildable example apps for RP2040 and STM32G474
vscode/                     # shared jh-vscode entry, schema, docs, generator
  entry/                    # Unix, Windows and public Python launchers
  tools/create-vscode-example.py # standalone VS Code firmware project generator
  tools/manage_vscode_extensions.py # checked/consented extension setup
security/
  third_party.json          # third-party component inventory
  sbom.cdx.json             # generated CycloneDX SBOM
  vulnerability_log.md      # CVE/CVSS assessment and patch log
src/
  JaszczurHAL.h             # primary public include
  hal_app_entry.cpp         # optional portable app entry wrapper
  libConfig.h               # backward-compat include
  tools.h, tools_c.h        # utility aggregators (C++ / C)
  arpa/, netinet/, sys/     # host/embedded socket compatibility headers
  hal/                      # HAL umbrella + thematic shared domains
    hal.h                   # HAL-only umbrella include
    core/                   # configuration, status, assertions, compatibility
    bluetooth/              # BLE public API, facade, and shared BTstack glue
    i2c/, spi/, serial/     # bus and serial APIs with common implementations
    time/, rtc/             # time-of-day, calendar, NTP, and RTC drivers
    timers/                 # hardware, extended, soft, and SmartTimers APIs
    temperature/            # DHT, DS18B20, MAX6675, and MCP9600
    network/                # core TCP/UDP/Wi-Fi and shared network runtime
      http/, mqtt/, ota/    # co-located public APIs and implementations
      tls/, wireguard/      # secure transports and reusable engines
      websocket/            # WebSocket public API and implementation
      net_console/          # remote console public API and implementation
      net_commands/         # command API and implementation
      cyw43/, lwip/         # radio and IP-stack integration
    storage/                # EEPROM, KV, SD logger, filesystem, flash helpers
    display/, gpio/         # display/GFX and GPIO-oriented drivers
    analog/, audio/, can/   # additional thematic API and driver domains
    generated/              # generated production C feature closure/report
    impl/
      .mock/                # deterministic host/test backend
      rp2040/               # RP-family backend
        drivers/flash/      # native RP flash coordinator and storage partitions
        drivers/rp2040/     # RP2040 SoC services (fault/system)
        drivers/usb/        # native TinyUSB CDC configuration/descriptors
        freertos/           # native RP FreeRTOSConfig and hooks
        frameworks/         # RP-specific framework integrations
      stm32g474/            # STM32G474 backend
        drivers/
          stm32g474/        # STM32G474 SoC services (fault/system)
        freertos/           # STM32 FreeRTOSConfig and hooks
        port/               # startup, SystemInit, linker-facing low-level glue
  utils/                    # tools, PID, watchdog, draw helpers, Unity integration
tests/                      # host unit tests (CMake + Unity)
  freertos_posix/           # optional host-side FreeRTOS POSIX scheduler tests
  hardware/                 # tracked RP fixture sources/manifests and host verifiers
third_party/                # tracked pins + ignored managed component installs
  update_components.sh      # synchronize every component to its tracked pin
  *_version.conf            # tracked source/tool/toolchain version definitions
  littlefs/                 # ignored pinned upstream filesystem checkout
```

Target-independent code is co-located with its public API in the corresponding
`src/hal/<domain>/` directory. A domain may contain public `hal_*.h` headers,
common facades, private `jh_*` helpers, device-driver subdirectories, and
reusable engines. This keeps one thematic hierarchy for both declarations and
implementations. `src/hal/impl/` is reserved for target-specific ports and
backends; portable domain code must depend only on HAL-level contracts.

### Compile-time feature resolution

The versioned registry under `config/features/` generates the production C and
CMake resolvers. `hal_config.h` includes the C closure, while RP and STM32G474
CMake builds use the same resolved set for source and dependency selection.
The board generator records both `requestedFeatures` and `resolvedFeatures`;
its feature hash and link contract use the resolved set. `jh-vscode` resolves
the active profile and variant into the same closure and publishes the registry
digest, closure digest, and request provenance through `config-dump`.

Conditional defaults, provider choices, board capability checks, and target
constraints remain in `hal_config.h`. `HAL_CONFIG_VERBOSE` activates the
generated report of every active registered flag. CI treats registry drift
and raw/effective feature lint as errors. Installed RP and STM32G474 packages
carry the generated feature/board headers, resolution JSON, link-contract
header, and reference source, so direct compiler consumers can compile and
link the fixed package without invoking Python.

- `CMakeLists.txt` - repository-root host/mock tests build.
- `rp_native_lib/` - official Pico SDK static library and firmware probes.
- `stm32_lib/` - STM32G474 static-library CMake, toolchain file, and linker script.
- `scripts/build_rp_native_lib.sh` - RP2040/RP2350 native build helper,
  including the optional pinned FreeRTOS SMP matrix.
- `scripts/build_stm32_lib.sh` - STM32G474 static-library helper.
- `third_party/update_components.sh` - synchronizes BearSSL, cJSON, LodePNG,
  TJpgDec, FatFs, Unity, lwIP, littlefs, BTstack, the Semtech SX126x driver,
  FreeRTOS, Pico SDK, picotool, PMD CPD and the RP2350 RISC-V toolchain to their
  tracked `third_party/*_version.conf` pins.
- `scripts/generate_sbom.py` - deterministic CycloneDX SBOM generator for the
  security inventory.
- `scripts/check_sbom.sh` - verifies that the committed SBOM matches the
  security inventory.
- `scripts/check_vulnerabilities.sh` - optional local vulnerability scanner
  wrapper that regenerates the SBOM and runs available source/vendored
  dependency scanners.
- `doc/api/00_scripts.md` - essential operational architecture reference for
  repository process scripts, orchestration entrypoints, options, artifacts,
  and their relationships.
- `runalltests.sh` - full local validation gate.
- `runmefirst.sh` - one-time local toolchain setup.
- `doc/FwProjectWorkflow.md` - dispatcher-backed firmware project workflow:
  manifest model, target/board selection, source discovery, upload/debug-build
  behavior, build directories, and generated files.
- `doc/boards_profiles_howto.md` - declarative target/board descriptors,
  generated configuration, board-aware static libraries, and the procedure
  for adding a physical board.
- `doc/OTAWorkflow.md` - complete native RP OTA contract: firmware
  integration, build artifacts, VS Code upload, firewall, trial confirmation,
  rollback, and recovery.
- `SECURITY.md` - vulnerability reporting, triage and maintenance policy.
- `security/third_party.json` - human-maintained third-party inventory.
- `security/sbom.cdx.json` - generated CycloneDX SBOM.
- `security/vulnerability_log.md` - CVE/CVSS assessment and patch-decision log.
- `src/JaszczurHAL.h` - umbrella include for HAL + utility modules.
- `doc/HAL_FLAGS.txt` - concise `HAL_ENABLE_*` flag summary.
- `src/libConfig.h` - backward-compat redirect to `hal/core/hal_config.h`.
- `src/tools.h` - C++ utility aggregator.
- `src/tools_c.h` - C-compatible utility declarations.
- `src/hal/hal.h` - HAL-only umbrella include.
- `src/hal/core/hal_config.h` - compatibility facade for build-time feature
  selection, dependency propagation and project configuration.
- `src/hal/core/hal_runtime_config.h` and `src/hal/core/hal_config.cpp` - runtime
  pool-limit configuration API and implementation.
- `src/hal/core/hal_assert.h` and `src/hal/core/hal_assert.cpp` - portable assertion API
  and target-aware failure implementation.
- `src/hal/core/hal_compat.h` - `PROGMEM`, `F()`, `hal_min()` and `hal_max()`
  source-compatibility helpers.
- `src/hal/<domain>/hal_*.h` - public HAL module interfaces grouped by topic,
  such as GPIO, buses, serial, security, sensors, storage, display, and network.
- `src/hal/can/hal_can_util.cpp`, `src/hal/security/hal_crypto.cpp`, `src/hal/security/hal_crc.cpp`, `src/hal/gps/hal_gps.cpp`, `src/hal/storage/hal_kv.cpp`, `src/hal/audio/hal_pga2311.cpp`, `src/hal/rtc/hal_rtc.cpp`, `src/hal/timers/hal_soft_timer.cpp`, `src/hal/control/hal_pid_controller.cpp` - shared HAL wrapper and facade implementations.
- `src/hal/serial/hal_uart_config.h` - UART configuration constants and helpers.
- `src/hal/core/hal_status.h` - shared `hal_status_t` result codes for new public
  APIs.
- `src/hal/system/hal_board.h` and `src/hal/system/hal_board.cpp` - board-profile identity,
  compile-time physical facts and thread-safe runtime capability state.
- `src/hal/impl/rp2040/` - RP-family backend.
- `src/hal/impl/stm32g474/` - STM32G474 backend (real register-level core domains; remaining modules in progress).
- `src/hal/impl/.mock/` - deterministic host-test backend.
- `src/hal/<domain>/` - public headers and backend-agnostic implementations in
  one thematic directory; examples include `bluetooth/`, `network/`, `time/`,
  `timers/`, `temperature/`, `i2c/`, `spi/`, `display/`, and `storage/`.
- `src/hal/impl/rp2040/drivers/` - bundled low-level third-party drivers used by optional HAL modules.
- `src/hal/impl/rp2040/drivers/rp2040/` - SoC-specific drivers: `rp2040_fault.{h,cpp}` (HardFault capture, stack guard, reset-reason latch) and `rp2040_system.{h,cpp}` (watchdog, USB-boot entry, on-die temperature, free-heap, unique board id, idle hint).
- `src/hal/impl/stm32g474/drivers/stm32g474/` - SoC-specific drivers: `stm32g474_fault.{h,cpp}` (reset-reason classification, retained fault handoff, stack guard) and `stm32g474_system.{h,cpp}` (time, delay, watchdog, temperature, UID, idle / ISR-sensitive helpers).
- `src/hal/network/mqtt/PubSubClient/` and
  `src/hal/network/wireguard/core/` - bundled target-neutral network engines.
- `src/utils/` - higher-level utilities: `tools`, `pidController`, `multicoreWatchdog`, `draw7Segment`, and managed Unity integration.

`JaszczurHAL.h` is the current top-level public include and should be the
default include in project code. `hal/hal.h` remains available as a HAL-only
aggregator, but it is not the primary include exported by the current library
metadata.

---

## Memory maps

Target-specific memory layout notes live next to the build glue for each
backend:

- [RP memory map](../rp_native_lib/MEMORY_MAP.md) - application and OTA linker
  layouts, persistent flash regions, SRAM, heap, and stacks.
- [STM32G474 memory map](../stm32_lib/MEMORY_MAP.md) - bare-metal linker
  regions, reserved flash EEPROM/KV pages, RAM sections, heap, and stack.

---

## Documentation scope

Recommended split of responsibilities:

- [00_scripts.md](api/00_scripts.md): an essential part of the JaszczurHAL
  documentation that explains how setup, dependency management, builds,
  examples, validation, security tooling, and VS Code orchestration work
  together; read it to understand how the library operates as a complete
  development system
- [FwProjectWorkflow.md](FwProjectWorkflow.md): dispatcher-backed firmware
  project workflow, including manifest/target/source/build/upload behavior
- [OTAWorkflow.md](OTAWorkflow.md): native RP OTA configuration, provisioning,
  upload, network/firewall, confirmation, rollback, and recovery
- `doc/JaszczurHAL_API.md`: module layout, migration notes, public API details, feature-flag reference

Each document owns the details in its assigned scope. The others should provide
short context and link to that owner instead of repeating commands, contracts,
or configuration examples.

---

## Public API vs helper modules

The repository contains both the HAL itself and a set of utility modules.

### HAL public API

These are the portability-oriented interfaces intended to decouple application
logic from board-specific SDK calls:

- `hal_gpio`, `hal_adc`, `hal_pwm`, `hal_pwm_freq`
- `hal_timer`, `hal_soft_timer`, `hal_system`, `hal_bits`, `hal_sync`,
  `hal_usb`, `hal_serial`
- `hal_board` for target-independent board identity and runtime hardware state
- `hal_crypto`, `hal_crc`
- `hal_pid_controller`
- `hal_uart`, `hal_swserial`, `hal_spi`, `hal_i2c`, `hal_onewire`
- `hal_lora_radio` for provider-neutral raw LoRa/SX1262 operation
- `hal_can`, `hal_display`, `hal_rgb_led`
- `hal_thermocouple`, `hal_ds18b20`, `hal_rtc`, `hal_external_adc`, `hal_gps`, `hal_digipot`, `hal_pga2311`, `hal_pn532`
- `hal_eeprom`, `hal_kv`, `hal_sdlogger`, `hal_wifi`, `hal_littlefs`, `hal_udp`, `hal_http_server`, `hal_http_files`, `hal_websocket`, `hal_net_console`, `hal_net_commands`, `hal_wireguard`, `hal_mqtt`, `hal_ota`, `hal_time`
- always-available `hal_time` helpers for deterministic Gregorian
  date/time-to-epoch conversion, CET/CEST adjustment, half-open range checks,
  and minute extraction; optional NTP/local-time APIs remain flag-controlled
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

## Board profiles and runtime capabilities

`HAL_TARGET_*` identifies the MCU and ISA. `JH_BOARD` selects the physical
profile from `boards/profiles/`; the generator emits the matching
`HAL_BOARD_PROFILE_*` selector and target configuration. Supported profiles
include `pico`, `picow`, `pico2`, `pico2w`, `pico-rm2`,
`rp2040-plus-4mb`, `rp2040-zero`, `rp2040-lora-lf`, and `nucleo-g474re`.

`HAL_BOARD_DECLARED_CAPABILITIES` describes fitted hardware at compile time.
Runtime users should query `hal_board_get_info()` or
`hal_board_get_capability_state()`, then use
`hal_board_require_capabilities()` before operations that require one or more
of `HAL_BOARD_CAP_USB_DEVICE`, `HAL_BOARD_CAP_CYW43`,
`HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND`, `HAL_BOARD_CAP_SX1262_RADIO`, and
`HAL_BOARD_CAP_BLUETOOTH_CONTROLLER`.
A declared capability is initially
`HAL_BOARD_CAP_INACTIVE`; its owner moves it to `AVAILABLE` or `FAILED`.
The RP CYW43 provider publishes these transitions during init/deinit.

Public types and functions (`hal/system/hal_board.h`):

```c
typedef uint32_t hal_board_capabilities_t;   /* HAL_BOARD_CAP_* bitmask */

typedef enum {                               /* stable board identity */
  HAL_BOARD_RP_PICO = 1, HAL_BOARD_RP_PICO_W, HAL_BOARD_RP_PICO_2,
  HAL_BOARD_RP_PICO_2_W, HAL_BOARD_RP_PICO_PIM730,
  HAL_BOARD_STM32G474_NUCLEO_G474RE, HAL_BOARD_HOST_MOCK,
  HAL_BOARD_RP2040_ZERO, HAL_BOARD_RP2040_PLUS_4MB,
  HAL_BOARD_RP2040_LORA_LF,
  HAL_BOARD_STM32G474_NUCLEO_PIM730
} hal_board_profile_t;

typedef enum {                               /* runtime state of one capability */
  HAL_BOARD_CAP_NOT_PRESENT = 0,
  HAL_BOARD_CAP_INACTIVE = 1,
  HAL_BOARD_CAP_AVAILABLE = 2,
  HAL_BOARD_CAP_FAILED = 3
} hal_board_capability_state_t;

typedef struct {                             /* consistent snapshot */
  hal_board_profile_t profile;
  const char *name;                          /* e.g. "pico2w" */
  hal_board_capabilities_t declared;
  hal_board_capabilities_t available;
  hal_board_capabilities_t failed;
} hal_board_info_t;

hal_status_t hal_board_get_info(hal_board_info_t *out_info);
hal_status_t hal_board_get_capability_state(
    hal_board_capabilities_t capability,
    hal_board_capability_state_t *out_state);
hal_status_t hal_board_require_capabilities(
    hal_board_capabilities_t capabilities);
```

`hal_board_require_capabilities()` returns `HAL_OK` when every requested
capability is available, `HAL_EUNSUPPORTED` when the board does not declare
one, `HAL_EUNINIT` while a declared capability is still inactive, and
`HAL_EHW` when its owner reported a failure:

```c
if (hal_status_is_ok(hal_board_require_capabilities(HAL_BOARD_CAP_CYW43))) {
    /* radio paths are safe to use on this board */
}
```

---

## API reference sections

The complete reference is split across the following focused documents:

| # | File | Contents |
|---|------|----------|
| 0 | [Process scripts and orchestration](api/00_scripts.md) | Essential operational architecture: workstation setup, managed dependencies, build entrypoints, examples, validation, security tooling, VS Code integration, artifact ownership, and the relationships between these processes |
| 1 | [Library compilation guide](lib_compilation.md) | Building for all targets, generated board contracts, static-library helpers, FreeRTOS build variants, and firmware integration |
| P | [Firmware project workflow](FwProjectWorkflow.md) | Dispatcher-backed VS Code firmware projects: manifest model, target/board selection, source discovery, per-target CMake cache layout, upload/debug-build behavior and generated files |
| O | [Native RP OTA workflow](OTAWorkflow.md) | End-to-end OTA project/firmware configuration, artifacts, first flash, VS Code integration, firewall, confirmation, rollback, recovery and security boundary |
| 2 | [Module flags and configuration](api/02_module_flags.md) | `HAL_ENABLE_*` opt-in flags, dependency propagation, FreeRTOS selection, stack-size overrides, and core modules |
| A | [Status API](api/01_status_api.md) | Foundational, cross-cutting: `hal_status_t` result codes, in-place migration of fallible `void` operations, `_ex` companions for retained value/handle/`bool` APIs, output-parameter forms and collision fallback. |
| 3 | [Build dependencies and unit tests](api/03_build_tests.md) | Hardware and mock/PC dependency tables, ctest build/run instructions, full test-suite inventory, how to add a new test suite, mock time control |
| 4 | [Multicore safety and drivers](api/04_multicore_drivers_migration.md) | Multicore init/runtime rules, bundled driver inventory and licences, logging timestamp hook, time conversion helper, examples overview, host-test coverage, and portable API mapping |
| S | [Security supply chain](security_supply_chain.md) | Third-party inventory, CycloneDX SBOM generation, vulnerability scanning and CVE/CVSS assessment workflow |
| 5 | [GPIO, ADC and PWM](api/05_gpio_adc_pwm.md) | `hal_gpio`, `hal_pwm`, `hal_dac`, `hal_pcnt`, `hal_pwm_freq`, `hal_dacless`, `hal_adc` |
| 6 | [Timers and system](api/06_timers_system.md) | `hal_timer` (alarms + managed timers), `hal_system` (millis/watchdog/crash diagnostics/UID), `hal_bits`, `hal_compiler` (portable attributes and builtins), `hal_math` |
| 7 | [Cryptography](api/07_crypto.md) | `hal_crypto` - Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20, ChaCha20-Poly1305 |
| 8 | [Sync, USB, serial, framing and auth](api/08_sync_serial.md) | `hal_sync` (mutex/critical-section), `hal_usb` (status-first USB lifecycle and CDC), `hal_serial` (one TX-serialized core with link-time transport ports, streamed debug formatting, ISR-deferred logging and rate-limiter), `hal_serial_session` (framed SC protocol), `hal_serial_frame` (wire codec), `hal_sc_auth` (HMAC challenge/response) |
| 9 | [Communication buses](api/09_buses.md) | `hal_spi` (status `_ex` transfer and DMA helpers), `hal_i2c` (status-first master API, bounded scanner with watchdog callback, one-shot helpers and bus clear), `hal_i2c_slave` (register map), `hal_uart`, `hal_swserial`, `hal_onewire` |
| 10 | [CAN bus and display](api/10_can_display.md) | `hal_can` (backend-selected CAN: MCP2515 classic CAN, MCP251XFD CAN FD, and STM32G474 native FDCAN), `hal_display` (status-first TFT/OLED/LCD/EPD facade, raw writes, EPD refresh, GFX primitives, streaming, text and fonts) |
| 11 | [Sensors](api/11_sensors.md) | `hal_thermocouple` (MCP9600/MAX6675), `hal_ds18b20` (non-blocking workflow), `hal_dht` (DHT11/DHT22), `hal_bh1750` (ambient light), `hal_adp5360` (PMIC charger/fuel-gauge/regulators), `hal_mcp3221` (I2C 12-bit ADC), `hal_rtc` (PCF8563/DS3231), `hal_external_adc` (ADS1115), `hal_gps` (NMEA, auto-detect framing) |
| 12 | [Cellular modem](api/12_modem.md) | `hal_modem_at` (AT engine, URC, watchdog cooperation), `hal_simcom_a76xx` (A7670/A7672 - power, boot, SIM, PDP, LBS, GNSS, MQTT subscribe) |
| 13 | [Output devices](api/13_output_devices.md) | `hal_rgb_led` (NeoPixel, PIO/GPIO transport), `hal_digipot` (MCP401x/MAX5395 I2C digital potentiometers), `hal_pga2311` (stereo volume controller), `hal_mcp23017`/`hal_pca9654e`/`hal_pcf8574` (I2C GPIO/output expanders), `hal_hc595` (SPI shift-register output expander), `hal_mcp4725` (I2C 12-bit DAC), `hal_mfrc522`/`hal_pn532` (RFID/NFC readers), `hal_math` (constrain, map, roundToN) |
| 14 | [Storage](api/14_storage.md) | `hal_eeprom` (target flash / AT24C256), `hal_kv` (append-only KV store with GC), `hal_littlefs` (LittleFS mount/format helpers), `hal_sdlogger` (SD-card buffered logger and crash reporter) |
| 15 | [Network connectivity](api/15_connectivity.md) | status-returning `_ex` APIs for `hal_wifi`, resolver, `hal_udp`, `hal_tcp`, `hal_tls`, `hal_mqtt` and `hal_wireguard`; `hal_http_server`, `hal_http_files`, `hal_websocket`, `hal_net_console`, `hal_net_commands`, independent BSD sockets adapter with `getaddrinfo()` and optional TLS transport, `hal_ota`, always-available `hal_time` calendar helpers plus optional NTP/local time |
| 16 | [Utilities](api/16_utilities.md) | `hal_soft_timer` (C wrapper over SmartTimers), `hal_pid_controller` (C wrapper over pidController), `hal_crc` (generic CRC-8/16/32 checksums), `tools.h/cpp` helper functions, `SmartTimers`, `pidController`, `multicoreWatchdog`, `draw7Segment` |
| 17 | [cJSON](api/17_cJSON.md) | Managed `cJSON` / `cJSON_Utils`, include patterns, ownership rules, parsing, printing, JSON Pointer/Patch/Merge Patch examples |
| 18 | [LodePNG](api/18_LodePNG.md) | Managed `LodePNG`, include patterns, embedded profile, memory ownership, PNG/Base64 asset script and RGB565 examples |
| 19 | [JPEG](api/19_JPEG.md) | Managed `TJpgDec` core, embedded profile, memory ownership, JPEG/Base64 asset script and RGB565 examples |
| 20 | [Bluetooth Low Energy](api/20_bluetooth.md) | Experimental Peripheral lifecycle, advertising, connection events, ATT MTU, bounded queues, board support, coexistence, and BTstack distribution boundary |
| 21 | [Raw LoRa radio](api/21_lora.md) | SX1262 profiles, DIO1-driven asynchronous TX/RX, callbacks, cancellation, explicit states, diagnostics and time-on-air |

---

## Quick module lookup

| Module | Section |
|--------|---------|
| `hal_adc` | [GPIO, ADC and PWM](api/05_gpio_adc_pwm.md) |
| `hal_bh1750` | [Sensors](api/11_sensors.md) |
| `hal_ble` | [Bluetooth Low Energy](api/20_bluetooth.md) |
| `hal_adp5360` | [Sensors](api/11_sensors.md) |
| `hal_bits` | [Timers and system](api/06_timers_system.md) |
| `hal_can` | [CAN and display](api/10_can_display.md) |
| `hal_crc` | [Utilities](api/16_utilities.md) |
| `hal_crypto` | [Cryptography](api/07_crypto.md) |
| `cJSON` / `cJSON_Utils` | [cJSON](api/17_cJSON.md) |
| `LodePNG` | [LodePNG](api/18_LodePNG.md) |
| `TJpgDec` | [JPEG](api/19_JPEG.md) |
| `hal_digipot` | [Output devices](api/13_output_devices.md) |
| `hal_display` | [CAN and display](api/10_can_display.md) |
| `hal_ds18b20` | [Sensors](api/11_sensors.md) |
| `hal_dht` | [Sensors](api/11_sensors.md) |
| `hal_eeprom` | [Storage](api/14_storage.md) |
| `hal_external_adc` | [Sensors](api/11_sensors.md) |
| `hal_gps` | [Sensors](api/11_sensors.md) |
| `hal_gpio` | [GPIO, ADC and PWM](api/05_gpio_adc_pwm.md) |
| `hal_hc595` | [Output devices](api/13_output_devices.md) |
| `hal_i2c` / `hal_i2c_slave` | [Communication buses](api/09_buses.md) |
| `hal_kv` | [Storage](api/14_storage.md) |
| `hal_littlefs` | [Storage](api/14_storage.md) |
| `hal_lora_radio` | [Raw LoRa radio](api/21_lora.md) |
| `hal_math` | [Timers and system](api/06_timers_system.md) / [Output devices](api/13_output_devices.md) |
| `hal_mcp23017` | [Output devices](api/13_output_devices.md) |
| `hal_mcp3221` | [Sensors](api/11_sensors.md) |
| `hal_mcp4725` | [Output devices](api/13_output_devices.md) |
| `hal_modem_at` | [Cellular modem](api/12_modem.md) |
| `hal_mqtt` | [Network connectivity](api/15_connectivity.md) |
| `hal_onewire` | [Communication buses](api/09_buses.md) |
| `hal_ota` | [Network connectivity](api/15_connectivity.md) |
| `hal_pca9654e` | [Output devices](api/13_output_devices.md) |
| `hal_pcf8574` | [Output devices](api/13_output_devices.md) |
| `hal_pga2311` | [Output devices](api/13_output_devices.md) |
| `hal_pn532` | [Output devices](api/13_output_devices.md) |
| `hal_pid_controller` | [Utilities](api/16_utilities.md) |
| `hal_pwm` / `hal_pwm_freq` / `hal_dacless` / `hal_pcnt` | [GPIO, ADC and PWM](api/05_gpio_adc_pwm.md) |
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
| `hal_usb` | [Sync, USB, serial, framing](api/08_sync_serial.md) |
| `hal_timer` | [Timers and system](api/06_timers_system.md) |
| `hal_uart` | [Communication buses](api/09_buses.md) |
| `hal_udp` | [Network connectivity](api/15_connectivity.md) |
| `hal_tcp` | [Network connectivity](api/15_connectivity.md) |
| `hal_http_server` | [Network connectivity](api/15_connectivity.md) |
| `hal_http_files` | [Network connectivity](api/15_connectivity.md) |
| `hal_websocket` | [Network connectivity](api/15_connectivity.md) |
| `hal_net_console` | [Network connectivity](api/15_connectivity.md) |
| `hal_net_commands` | [Network connectivity](api/15_connectivity.md) |
| BSD sockets adapter | [Network connectivity](api/15_connectivity.md) |
| `hal_wifi` | [Network connectivity](api/15_connectivity.md) |
| `hal_wireguard` | [Network connectivity](api/15_connectivity.md) |
| `multicoreWatchdog` | [Utilities](api/16_utilities.md) |
| `pidController` | [Utilities](api/16_utilities.md) |
| `SmartTimers` | [Utilities](api/16_utilities.md) |
| `draw7Segment` | [Utilities](api/16_utilities.md) |
