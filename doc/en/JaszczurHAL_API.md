# JaszczurHAL - API Reference

*Also available in [Polish](../pl/JaszczurHAL_API.md).*

Hardware Abstraction Layer for embedded projects.
The RP2040/RP2350 backend builds against the official Pico SDK. STM32G474 is
available as a bare-metal or FreeRTOS backend with native peripheral support
and the shared driver stack. ESP32-S3 provides exact target/board identity,
ESP-IDF application entry and build-compatibility validation plus the delivered
core/peripheral backend set and native WiFi/lwIP connectivity graph. Its
network surface includes TLS and HTTPS clients plus plaintext HTTP and
WebSocket servers; no public TLS server, HTTPS server, WSS, or WebSocket-client
API is defined. The application-facing HAL API stays stable across targets.

This document is the established, detailed API reference.
The top-level [README.md](../../README.md) intentionally stays concise and links
here for full behavior and guarantees.

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
  esp-idf/                  # controlled native ESP-IDF component recipe
  generated/                # generated production CMake feature resolver
  jh_rp_native_sdk.cmake    # shared RP library/firmware CMake integration
  targets/                  # VS Code dispatcher target recipes
stm32_lib/                  # STM32G474 static-library CMake, toolchain, linker script
scripts/
  # See doc/api/en/00_scripts.md for the complete process-script reference.
  build_rp_native_lib.sh    # RP ELF/BIN/UF2 build helper
  build_stm32_lib.sh        # STM32G474 static-library helper
  build_esp_idf.py          # ESP-IDF project build/artifact/flash runner
  check_documentation_links.py # local Markdown link/anchor validation
  ensure_*.sh               # focused pinned-component fetch/verify helpers
  generate_sbom.py          # CycloneDX SBOM generator
  generate_hal_features.py  # feature registry validation, generation and lint
  check_vulnerabilities.sh  # optional local vulnerability scanner wrapper
runalltests.sh              # full local validation gate
runmefirst.sh               # one-time local toolchain setup
doc/
  HAL_FLAGS.txt             # shared HAL_ENABLE_* flag summary
  table_of_contents.md      # English documentation index
  table_of_contents.pl.md   # Polish documentation index
  en/                       # English reference docs
    features.md             # high-level feature matrix
    JaszczurHAL_API.md      # detailed API/reference
    FwProjectWorkflow.md    # dispatcher-backed firmware project workflow
    OTAWorkflow.md          # native RP/ESP OTA build, upload and recovery
    lib_compilation.md      # static-library build guide
    security_supply_chain.md # SBOM and vulnerability tracking process
    boards_profiles_howto.md
    windows_setup.md
  pl/                       # Polish translations of the doc/en/ files above
    features.md             # high-level feature matrix
  api/
    en/                     # split API chapters (English)
      00_scripts.md         # essential process and orchestration architecture
    pl/                     # split API chapters (Polish)
examples/                   # buildable example apps for the RP family and STM32G474
vscode/                     # shared jh-vscode entry, schema, docs, generator
  entry/                    # Unix, Windows and public Python launchers
  tools/create-vscode-example.py # standalone VS Code firmware project generator
  tools/manage_vscode_extensions.py # checked/consented extension setup
security/
  third_party.json          # third-party component inventory
  esp_idf_tools.json        # reviewed ESP-IDF target-tool snapshot
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
    commands/               # transport-neutral command router and wire messages
    core/                   # configuration, status, assertions, compatibility
    bluetooth/              # BLE public API, facade, and shared BTstack support
    i2c/, spi/, serial/     # bus and serial APIs with common implementations
    time/, rtc/, power/     # wall time, RTC wake, and power management
    timers/                 # hardware, extended, soft, and SmartTimers APIs
    temperature/            # DHT, DS18B20, MAX6675, and MCP9600
    network/                # core TCP/UDP/Wi-Fi and shared network runtime
      http/, mqtt/, ota/    # co-located public APIs and implementations
      tls/, wireguard/      # secure transports and reusable engines
      websocket/            # WebSocket public API and implementation
      net_console/          # remote console public API and implementation
      net_commands/         # HTTP/WebSocket command adapter
      cyw43/, lwip/         # radio and IP-stack integration
    radio/                  # raw LoRa, reliable link, and command adapter
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
        port/               # startup, SystemInit, linker-facing low-level support
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
backends; portable domain code must depend only on HAL-level APIs.

### Compile-time feature resolution

The versioned registry under `config/features/` generates the production C and
CMake resolvers. `hal_config.h` includes the C closure, while RP and STM32G474
CMake builds use it for source and dependency selection. The ESP-IDF runner
resolves the same request graph, rejects features outside the target
descriptor's allowlist, and selects the supported baseline, peripheral, and
network source graph. It records requested and resolved features together with
board and link provenance.
The board generator records both `requestedFeatures` and `resolvedFeatures`;
its feature hash and link signature use the resolved set. `jh-vscode` resolves
the active profile and variant into the same closure and publishes the registry
digest, closure digest, and request provenance through `config-dump`.

Conditional defaults, provider choices, board capability checks, and target
constraints remain in `hal_config.h`. `HAL_CONFIG_VERBOSE` activates the
generated report of every active registered flag. CI treats registry drift
and raw/effective feature lint as errors. Installed RP and STM32G474 packages
carry the generated feature/board headers, resolution JSON, link-signature
header, and reference source, so direct compiler consumers can compile and
link the fixed package without invoking Python.

- `CMakeLists.txt` - repository-root host/mock tests build.
- `rp_native_lib/` - official Pico SDK static library and firmware probes.
- `stm32_lib/` - STM32G474 static-library CMake, toolchain file, and linker script.
- `scripts/build_rp_native_lib.sh` - RP2040/RP2350 static-library and optional
  firmware-probe helper, including an archive-only `--library-only` mode and
  the optional pinned FreeRTOS SMP matrix.
- `scripts/build_stm32_lib.sh` - STM32G474 static-library helper.
- `scripts/build_esp_idf.py` - production ESP-IDF project build, artifact
  validation, and flash helper with a relocatable multi-image manifest.
- `third_party/update_components.sh` - synchronizes BearSSL, cJSON, LodePNG,
  TJpgDec, FatFs, Unity, lwIP, littlefs, BTstack, the Semtech SX126x driver,
  FreeRTOS, Pico SDK, picotool, PMD CPD and the RP2350 RISC-V toolchain to their
  tracked `third_party/*_version.conf` pins. ESP-IDF is prepared on demand by
  its production runner or focused ensure command.
- `scripts/generate_sbom.py` - deterministic CycloneDX SBOM generator with a
  read-only `--check` mode.
- `scripts/check_sbom.sh` - compatibility wrapper for focused SBOM checking;
  the shared generated-artifact runner owns repository-wide synchronization.
- `scripts/check_vulnerabilities.sh` - optional local vulnerability scanner
  wrapper that regenerates the SBOM and runs available source/vendored
  dependency scanners.
- `doc/api/en/00_scripts.md` - essential operational architecture reference for
  repository process scripts, orchestration entrypoints, options, artifacts,
  and their relationships.
- `runalltests.sh` - full local validation gate.
- `runmefirst.sh` - one-time local toolchain setup.
- `doc/en/FwProjectWorkflow.md` - dispatcher-backed firmware project workflow:
  manifest model, target/board selection, source discovery, upload/debug-build
  behavior, build directories, and generated files.
- `doc/en/boards_profiles_howto.md` - declarative target/board descriptors,
  generated configuration, board-aware static libraries, and the procedure
  for adding a physical board.
- `doc/en/OTAWorkflow.md` - native RP and ESP32-S3 OTA requirements: firmware
  integration, target-specific artifacts, VS Code upload, firewall, trial
  confirmation, rollback, and recovery.
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
- `src/hal/time/hal_time_ntp.cpp` and `src/hal/storage/hal_eeprom.cpp` - shared
  thread-safe wall-clock/NTP/RTC integration and provider-dispatched EEPROM
  facades; portable AT24C256 and
  buffered-flash provider code stays beside the EEPROM API, while target
  directories supply only physical flash mechanisms.
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

Target-specific memory layout notes live next to the build support for each
backend:

- [RP memory map](../../rp_native_lib/MEMORY_MAP.md) - application and OTA linker
  layouts, persistent flash regions, SRAM, heap, and stacks.
- [STM32G474 memory map](../../stm32_lib/MEMORY_MAP.md) - bare-metal linker
  regions, reserved flash EEPROM/KV pages, RAM sections, heap, and stack.

---

## Documentation scope

Recommended split of responsibilities:

- [00_scripts.md](../api/en/00_scripts.md): an essential part of the JaszczurHAL
  documentation that explains how setup, dependency management, builds,
  examples, validation, security tooling, and VS Code orchestration work
  together; read it to understand how the library operates as a complete
  development system
- [FwProjectWorkflow.md](FwProjectWorkflow.md): dispatcher-backed firmware
  project workflow, including manifest/target/source/build/upload behavior
- [OTAWorkflow.md](OTAWorkflow.md): native RP and ESP32-S3 OTA configuration,
  provisioning, upload, network/firewall, confirmation, rollback, and recovery
- `doc/en/JaszczurHAL_API.md`: module layout, migration notes, public API details, feature-flag reference

Each document owns the details in its assigned scope. The others should provide
short context and link to that owner instead of repeating commands, interfaces,
or configuration examples.

---

## Public API vs helper modules

The repository contains both the HAL itself and a set of utility modules.

### HAL public API

These are the portability-oriented interfaces intended to decouple application
logic from board-specific SDK calls:

- core and system: `hal_config`, `hal_status`, `hal_bits`, `hal_math`,
  `hal_board`, `hal_system`, `hal_power`, `hal_sync`, `hal_timer`,
  `hal_soft_timer`, and `hal_pid_controller`
- analog, GPIO, and audio: `hal_gpio`, `hal_adc`, `hal_dac`, `hal_pwm`,
  `hal_pwm_freq`, `hal_pcnt`, `hal_dacless`, and `hal_dma_pwm_audio`
- buses and serial: `hal_uart`, `hal_swserial`, `hal_serial`, `hal_usb`,
  `hal_spi`, `hal_spi_device`, `hal_i2c`, `hal_i2c_slave`, and `hal_onewire`
- security and connectivity: `hal_crypto`, `hal_crc`, `hal_net`, `hal_wifi`,
  `hal_udp`, `hal_tcp`, `hal_tls`, `hal_http_client`, `hal_http_server`,
  `hal_http_files`, `hal_websocket`, `hal_net_console`, `hal_net_commands`,
  `hal_notify`, `hal_wireguard`, `hal_mqtt`, `hal_ota`, `hal_time`,
  `hal_ble`, and `hal_ble_stream`
- `hal_command_router` and `hal_command_wire` for transport-neutral command
  policy, dispatch and bounded binary messages
- `hal_lora_radio` for provider-neutral raw LoRa operation with SX126x and
  SX127x family providers
- `hal_lora_link` for addressed, acknowledged, fragmented private messages
  over one raw LoRa radio, with optional authenticated encryption
- `hal_lora_commands` for command requests, responses and events over one
  exclusively owned reliable LoRa link
- devices and media: `hal_can`, `hal_display`, `hal_hd44780`, `hal_rgb_led`,
  `hal_thermocouple`, `hal_ds18b20`, `hal_rtc`, `hal_external_adc`,
  `hal_gps`, `hal_tsc2007`, `hal_stmpe610`, `hal_irsmall_decoder`,
  `hal_digipot`, `hal_pga2311`, `hal_mfrc522`, and `hal_pn532`
- storage: `hal_eeprom`, `hal_kv`, `hal_littlefs`, and `hal_sdlogger`
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
`HAL_BOARD_PROFILE_*` selector and target configuration. The board descriptors
are the authoritative profile inventory. List the current IDs with:

```bash
python3 scripts/generate_board_config.py --boards-root boards --list boards
```

The ESP32-S3 component consumes generated target/board facts and the link
metadata and compiles the public `hal_board` runtime facade. Capability state
still follows the shared owner model: a declared capability remains
`HAL_BOARD_CAP_INACTIVE` until the module that owns it publishes an available
or failed state.

`HAL_BOARD_DECLARED_CAPABILITIES` describes fitted hardware at compile time.
On targets that build the runtime facade, users should query
`hal_board_get_info()` or
`hal_board_get_capability_state()`, then use
`hal_board_require_capabilities()` before operations that require one or more
of `HAL_BOARD_CAP_USB_DEVICE`, `HAL_BOARD_CAP_CYW43`,
`HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND`, `HAL_BOARD_CAP_SX1262_RADIO`, and
`HAL_BOARD_CAP_BLUETOOTH_CONTROLLER`, or `HAL_BOARD_CAP_NATIVE_WIFI`.
A declared capability is initially
`HAL_BOARD_CAP_INACTIVE`; its owner moves it to `AVAILABLE` or `FAILED`.
The RP CYW43 provider publishes these transitions during init/deinit.

`hal/system/hal_board.h` defines the stable profile enum, capability bitmask,
runtime states, snapshot type, and query functions. The generated
`src/hal/generated/jh_board_registry.h` maps every registry profile to that
public identity without maintaining another hand-written profile list here.

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
| 0 | [Process scripts and orchestration](../api/en/00_scripts.md) | Essential operational architecture: workstation setup, managed dependencies, build entrypoints, examples, validation, security tooling, VS Code integration, artifact ownership, and the relationships between these processes |
| 1 | [Library compilation guide](lib_compilation.md) | Building for all targets, generated board metadata, static-library helpers, FreeRTOS build variants, and firmware integration |
| P | [Firmware project workflow](FwProjectWorkflow.md) | Dispatcher-backed VS Code firmware projects: manifest model, target/board selection, source discovery, per-target CMake cache layout, upload/debug-build behavior and generated files |
| O | [Native OTA workflow](OTAWorkflow.md) | Target-specific RP and ESP32-S3 OTA project/firmware configuration, artifacts, first flash, VS Code integration, firewall, confirmation, rollback, recovery and security boundary |
| 2 | [Module flags and configuration](../api/en/02_module_flags.md) | `HAL_ENABLE_*` opt-in flags, dependency propagation, FreeRTOS selection, stack-size overrides, and core modules |
| A | [Status API](../api/en/01_status_api.md) | Foundational, cross-cutting: `hal_status_t` result codes, in-place migration of fallible `void` operations, `_ex` companions for retained value/handle/`bool` APIs, output-parameter forms and collision fallback. |
| 3 | [Build dependencies, tests, and hardware fixtures](../api/en/03_build_tests.md) | Test architecture and sources of truth, dependencies, host/CI execution, full suite inventory, extension rules, mock time control, and centralized hardware-fixture procedures and results |
| 4 | [Multicore safety and drivers](../api/en/04_multicore_drivers_migration.md) | Multicore init/runtime rules, bundled driver inventory and licences, logging timestamp hook, time conversion helper, examples overview, host-test coverage, and portable API mapping |
| S | [Security supply chain](security_supply_chain.md) | Third-party inventory, CycloneDX SBOM generation, vulnerability scanning and CVE/CVSS assessment workflow |
| 5 | [GPIO, ADC and PWM](../api/en/05_gpio_adc_pwm.md) | `hal_gpio`, `hal_pwm`, `hal_dac`, `hal_pcnt`, `hal_pwm_freq`, `hal_dacless`, `hal_adc` |
| 6 | [Timers and system](../api/en/06_timers_system.md) | `hal_timer` (alarms + managed timers), `hal_system` (millis/watchdog/crash diagnostics/UID), `hal_power` (Sleep/deep-sleep/power-down transitions), `hal_bits`, `hal_compiler` (portable attributes and builtins), `hal_math` |
| 7 | [Cryptography](../api/en/07_crypto.md) | `hal_crypto` - Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20, ChaCha20-Poly1305 |
| 8 | [Sync, USB, serial, framing and auth](../api/en/08_sync_serial.md) | `hal_sync` (mutex/critical-section), `hal_usb` (status-first USB lifecycle and CDC), `hal_serial` (one TX-serialized core with link-time transport ports, streamed debug formatting, ISR-deferred logging and rate-limiter), `hal_serial_session` (framed SC protocol), `hal_serial_commands` (text router adapter), `hal_serial_frame` (wire codec), `hal_sc_auth` (HMAC challenge/response) |
| 9 | [Communication buses](../api/en/09_buses.md) | `hal_spi` (status `_ex` transfer and DMA helpers), `hal_i2c` (status-first master API, bounded scanner with watchdog callback, one-shot helpers and bus clear), `hal_i2c_slave` (register map), `hal_uart`, `hal_swserial`, `hal_onewire` |
| 10 | [CAN bus and display](../api/en/10_can_display.md) | `hal_can` (backend-selected CAN: MCP2515 classic CAN, MCP251XFD CAN FD, and STM32G474 native FDCAN), `hal_display` (status-first TFT/OLED/LCD/EPD facade, raw writes, EPD refresh, GFX primitives, streaming, text and fonts) |
| 11 | [Sensors](../api/en/11_sensors.md) | `hal_thermocouple` (one provider-dispatched MCP9600/MAX6675/mock facade), `hal_ds18b20` (non-blocking workflow), `hal_dht` (DHT11/DHT22), `hal_bh1750` (ambient light), `hal_adp5360` (PMIC charger/fuel-gauge/regulators), `hal_mcp3221` (I2C 12-bit ADC), `hal_rtc` (PCF8563/DS3231/internal AON providers and relative wake), `hal_external_adc` (ADS1115), `hal_gps` (NMEA, auto-detect framing) |
| 12 | [Cellular modem](../api/en/12_modem.md) | `hal_modem_at` (AT engine, URC, watchdog cooperation), `hal_simcom_a76xx` (A7670/A7672 - power, boot, SIM, PDP, LBS, GNSS, MQTT subscribe) |
| 13 | [Output devices](../api/en/13_output_devices.md) | `hal_rgb_led` (NeoPixel, PIO/GPIO transport), `hal_digipot` (MCP401x/MAX5395 I2C digital potentiometers), `hal_pga2311` (stereo volume controller), `hal_mcp23017`/`hal_pca9654e`/`hal_pcf8574` (I2C GPIO/output expanders), `hal_hc595` (SPI shift-register output expander), `hal_mcp4725` (I2C 12-bit DAC), `hal_mfrc522`/`hal_pn532` (RFID/NFC readers), `hal_math` (constrain, map, roundToN) |
| 14 | [Storage](../api/en/14_storage.md) | `hal_eeprom` (target flash / AT24C256), `hal_kv` (append-only KV store with GC), `hal_littlefs` (LittleFS mount/format helpers), `hal_sdlogger` (SD-card buffered logger and crash reporter) |
| 15 | [Network connectivity](../api/en/15_connectivity.md) | status-returning `_ex` APIs for `hal_wifi`, resolver, `hal_udp`, `hal_tcp`, `hal_tls`, `hal_mqtt` and `hal_wireguard`; `hal_http_server`, `hal_http_files`, `hal_websocket`, `hal_net_console`, `hal_net_commands`, `hal_notify`, independent BSD sockets adapter with `getaddrinfo()` and optional TLS transport, `hal_ota`, always-available `hal_time` calendar helpers plus optional NTP/local time |
| 16 | [Utilities](../api/en/16_utilities.md) | `hal_soft_timer` (C wrapper over SmartTimers), `hal_pid_controller` (C wrapper over pidController), `hal_crc` (generic CRC-8/16/32 checksums), `tools.h/cpp` helper functions, `SmartTimers`, `pidController`, `multicoreWatchdog`, `draw7Segment` |
| 17 | [cJSON](../api/en/17_cJSON.md) | Managed `cJSON` / `cJSON_Utils`, include patterns, ownership rules, parsing, printing, JSON Pointer/Patch/Merge Patch examples |
| 18 | [LodePNG](../api/en/18_LodePNG.md) | Managed `LodePNG`, include patterns, embedded profile, memory ownership, PNG/Base64 asset script and RGB565 examples |
| 19 | [JPEG](../api/en/19_JPEG.md) | Managed `TJpgDec` core, embedded profile, memory ownership, JPEG/Base64 asset script and RGB565 examples |
| 20 | [Bluetooth](../api/en/20_bluetooth.md) | BLE Peripheral/Observer and Classic HID gamepad lifecycle, pairing, reconnect, normalized snapshots, authenticated Stream, bounded queues, board support, coexistence, and BTstack distribution boundary |
| 21 | [Raw LoRa radio](../api/en/21_lora.md) | Validated SX1262 profiles plus experimental software-only SX1261/SX1276/SX1278, asynchronous TX/RX/CAD, current RSSI, capabilities, callbacks, diagnostics and time-on-air |
| 22 | [Reliable LoRa link](../api/en/22_lora_link.md) | 16-bit addressing, message sequences, ACK/retry, duplicate suppression, fragmentation and optional ChaCha20-Poly1305 over `hal_lora_radio` |
| 23 | [Command routing](../api/en/23_commands.md) | Transport-neutral handler registration and policy, bounded request/response/event wire messages, network compatibility, framed serial, reliable LoRa and authenticated BLE Stream adapters |

---

## Quick module lookup

| Module | Section |
|--------|---------|
| `hal_adc` | [GPIO, ADC and PWM](../api/en/05_gpio_adc_pwm.md) |
| `hal_bh1750` | [Sensors](../api/en/11_sensors.md) |
| `hal_ble` | [Bluetooth Low Energy](../api/en/20_bluetooth.md) |
| `hal_gamepad` | [Bluetooth Classic HID gamepad](../api/en/20_bluetooth.md#bluetooth-classic-hid-gamepad) |
| `hal_adp5360` | [Sensors](../api/en/11_sensors.md) |
| `hal_bits` | [Timers and system](../api/en/06_timers_system.md) |
| `hal_can` | [CAN and display](../api/en/10_can_display.md) |
| `hal_command_router` / `hal_command_wire` | [Command routing](../api/en/23_commands.md) |
| `hal_crc` | [Utilities](../api/en/16_utilities.md) |
| `hal_crypto` | [Cryptography](../api/en/07_crypto.md) |
| `cJSON` / `cJSON_Utils` | [cJSON](../api/en/17_cJSON.md) |
| `LodePNG` | [LodePNG](../api/en/18_LodePNG.md) |
| `TJpgDec` | [JPEG](../api/en/19_JPEG.md) |
| `hal_digipot` | [Output devices](../api/en/13_output_devices.md) |
| `hal_display` | [CAN and display](../api/en/10_can_display.md) |
| `hal_ds18b20` | [Sensors](../api/en/11_sensors.md) |
| `hal_dht` | [Sensors](../api/en/11_sensors.md) |
| `hal_eeprom` | [Storage](../api/en/14_storage.md) |
| `hal_external_adc` | [Sensors](../api/en/11_sensors.md) |
| `hal_gps` | [Sensors](../api/en/11_sensors.md) |
| `hal_gpio` | [GPIO, ADC and PWM](../api/en/05_gpio_adc_pwm.md) |
| `hal_hc595` | [Output devices](../api/en/13_output_devices.md) |
| `hal_i2c` / `hal_i2c_slave` | [Communication buses](../api/en/09_buses.md) |
| `hal_kv` | [Storage](../api/en/14_storage.md) |
| `hal_littlefs` | [Storage](../api/en/14_storage.md) |
| `hal_lora_radio` | [Raw LoRa radio](../api/en/21_lora.md) |
| `hal_lora_link` | [Reliable LoRa link](../api/en/22_lora_link.md) |
| `hal_lora_commands` | [Command routing](../api/en/23_commands.md) |
| `hal_math` | [Timers and system](../api/en/06_timers_system.md) / [Output devices](../api/en/13_output_devices.md) |
| `hal_mcp23017` | [Output devices](../api/en/13_output_devices.md) |
| `hal_mcp3221` | [Sensors](../api/en/11_sensors.md) |
| `hal_mcp4725` | [Output devices](../api/en/13_output_devices.md) |
| `hal_modem_at` | [Cellular modem](../api/en/12_modem.md) |
| `hal_mqtt` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_notify` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_onewire` | [Communication buses](../api/en/09_buses.md) |
| `hal_ota` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_pca9654e` | [Output devices](../api/en/13_output_devices.md) |
| `hal_pcf8574` | [Output devices](../api/en/13_output_devices.md) |
| `hal_pga2311` | [Output devices](../api/en/13_output_devices.md) |
| `hal_pn532` | [Output devices](../api/en/13_output_devices.md) |
| `hal_pid_controller` | [Utilities](../api/en/16_utilities.md) |
| `hal_power` | [Timers and system](../api/en/06_timers_system.md) |
| `hal_pwm` / `hal_pwm_freq` / `hal_dacless` / `hal_pcnt` | [GPIO, ADC and PWM](../api/en/05_gpio_adc_pwm.md) |
| `hal_rgb_led` | [Output devices](../api/en/13_output_devices.md) |
| `hal_rtc` | [Sensors](../api/en/11_sensors.md) |
| `hal_sc_auth` | [Sync, serial, framing](../api/en/08_sync_serial.md) |
| `hal_sdlogger` | [Storage](../api/en/14_storage.md) |
| `hal_serial` | [Sync, serial, framing](../api/en/08_sync_serial.md) |
| `hal_serial_commands` | [Command routing](../api/en/23_commands.md#framed-serial-session-adapter) |
| `hal_serial_frame` | [Sync, serial, framing](../api/en/08_sync_serial.md) |
| `hal_serial_session` | [Sync, serial, framing](../api/en/08_sync_serial.md) |
| `hal_simcom_a76xx` | [Cellular modem](../api/en/12_modem.md) |
| `hal_soft_timer` | [Utilities](../api/en/16_utilities.md) |
| `hal_spi` | [Communication buses](../api/en/09_buses.md) |
| `hal_swserial` | [Communication buses](../api/en/09_buses.md) |
| `hal_sync` | [Sync, serial, framing](../api/en/08_sync_serial.md) |
| `hal_system` | [Timers and system](../api/en/06_timers_system.md) |
| `hal_thermocouple` | [Sensors](../api/en/11_sensors.md) |
| `hal_time` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_usb` | [Sync, USB, serial, framing](../api/en/08_sync_serial.md) |
| `hal_timer` | [Timers and system](../api/en/06_timers_system.md) |
| `hal_uart` | [Communication buses](../api/en/09_buses.md) |
| `hal_udp` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_tcp` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_http_server` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_http_files` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_websocket` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_net_console` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_net_commands` | [Network connectivity](../api/en/15_connectivity.md) |
| BSD sockets adapter | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_wifi` | [Network connectivity](../api/en/15_connectivity.md) |
| `hal_wireguard` | [Network connectivity](../api/en/15_connectivity.md) |
| `multicoreWatchdog` | [Utilities](../api/en/16_utilities.md) |
| `pidController` | [Utilities](../api/en/16_utilities.md) |
| `SmartTimers` | [Utilities](../api/en/16_utilities.md) |
| `draw7Segment` | [Utilities](../api/en/16_utilities.md) |
