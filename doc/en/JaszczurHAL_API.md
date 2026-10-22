# JaszczurHAL - API Reference

*Also available in [Polish](../pl/JaszczurHAL_API.md).*

JaszczurHAL provides a common hardware API for embedded applications. This guide explains the library layout, feature configuration, and how to find the reference for each module.

**Author:** Marcin 'Jaszczur' Kielesiński

- **Repository:** `git@github.com:jaszczurtd/JaszczurHAL.git`
- **Include roots:** `src/` of the HAL checkout and `third_party/jhbase/include` (status codes and `COUNTOF`; HAL's CMake builds add it)

---

## Public include

Include the umbrella header in application code:

```c
#include <JaszczurHAL.h>
```

For internal code or direct access to the HAL layer, include:

```c
#include <hal/hal.h>
```

To use only the utility functions, include the appropriate utility header:

```cpp
#include <tools.h>    // C++ utility aggregator
```

```c
#include <tools_c.h>  // C-compatible utility API
```

### C API

New application code should use the C declarations from `JaszczurHAL.h` or
the corresponding thematic `hal_*` header. Functions that can fail use
`hal_status_t`; stateful modules use opaque handles where appropriate. These
handles are provided by `hal_hd44780`, `hal_dacless`, `hal_mfrc522`, and
`hal_pn532`, among others. The small `utils/draw7Segment.h` helper is also
available directly from C code.

A `.c` translation unit can include these headers directly. JaszczurHAL still
contains C++ implementation units, so use the supplied CMake integration for
the final link, or select a C++ linker when integrating the archive manually.

### C++ API

Selected public `hal_*` headers continue to expose their existing C++ classes
when compiled as C++. Compatibility utility headers likewise retain their
established names. Those entry points remain maintained, and existing C++
applications do not need an immediate rewrite. This reference presents the C
API recommended for new code first and lists the existing C++ API separately.

`tools.h` remains the C++ utility aggregator. C code should include
`tools_c.h` or the owning thematic header instead.

---

## Library structure

Portable modules keep their public headers, shared implementations and device
drivers together under `src/hal/<domain>/`. Platform-specific code belongs in
`src/hal/impl/`. Shared code uses HAL APIs rather than calling a platform
implementation directly.

| Location | Purpose |
|---|---|
| `src/hal/core/` | Configuration, status codes, assertions, compiler helpers and runtime pool limits. |
| `src/hal/<domain>/` | APIs and portable implementations for buses, sensors, storage, display, networking and other domains. |
| `src/hal/impl/rp2040/` | RP2040 and RP2350 implementations, including flash coordination and USB integration. |
| `src/hal/impl/stm32g474/` | STM32G474 peripheral implementations, startup code and FreeRTOS integration. |
| `src/hal/impl/esp32/` | ESP32-family implementations using ESP-IDF. |
| `src/hal/impl/.mock/` | Host implementations for tests, with an optional operating-system network backend. |
| `src/utils/` | C++ compatibility utilities, PID, watchdog and drawing helpers, plus Unity integration. |
| `boards/`, `config/` | Target and board facts, feature dependencies, and hardware schemas and bindings. |
| `cmake/`, `link_libraries/` | Firmware integration and static-library build recipes. |
| `tools/jh/` | Host JSON inspection and schema-validation CLI. |
| `scripts/`, `vscode/` | Setup, generation, build, upload, debug and validation tools. |
| `tests/` | Host tests and independent fixtures; device tests are selected separately under `tests/hardware/`. |
| `third_party/` | Version pins and ignored managed installations. |
| `security/` | Dependency inventory, generated SBOM and vulnerability assessments. |
| `doc/` | Guides and API chapters in English and Polish. |

The [feature overview](features.md) and [build and validation guide](../api/en/03_build_tests.md)
describe platform support and its validation limits. Directory presence alone
does not establish hardware support for every module.

Use the [script reference](../api/en/00_scripts.md) for tool entry points,
[the library build guide](lib_compilation.md) for archives and generated headers,
and [the component guide](../../third_party/README.md) for dependency preparation.
The [jh CLI guide](../../tools/jh/README.md) covers the separate host project.
Core declarations are in `hal/core/hal_runtime_config.h`, `hal_assert.h` and
`hal_compat.h`; their parameters and constraints are documented in those headers.

### Compile-time feature resolution

The registry in `config/features/` defines the generated feature configuration for C and CMake. `hal_config.h` includes the C dependency-resolution header, and the RP and STM32G474 CMake builds use the resolved set to select sources and dependencies. The ESP-IDF runner applies the same rules, rejects features outside the target allowlist, and selects supported core, peripheral, and network sources. It records requested and resolved features with board and link provenance. The board generator stores both sets as `requestedFeatures` and `resolvedFeatures`; feature hashes and link signatures use the resolved set. `jh-vscode` resolves that same set for the active profile and variant. `config-dump` reports the registry and resolved-set digests and the origin of each feature request.

Conditional defaults, provider choices, board capability checks, and target
constraints remain in `hal_config.h`. `HAL_CONFIG_VERBOSE` activates the
generated report of every active registered flag. CI treats registry drift
and raw/effective feature lint as errors. Installed RP and STM32G474 packages
carry the generated feature/board headers, resolution JSON, link-signature
header, and reference source, so direct compiler consumers can compile and
link the fixed package without invoking Python.

---

## Memory maps

Memory layouts are documented alongside each platform's build configuration:

- [RP memory map](../../link_libraries/rp_pico_lib/MEMORY_MAP.md) - application and OTA linker
  layouts, persistent flash regions, SRAM, heap, and stacks.
- [STM32G474 memory map](../../link_libraries/stm32_lib/MEMORY_MAP.md) - bare-metal linker
  regions, reserved flash EEPROM/KV pages, RAM sections, heap, and stack.

---

<a id="suggested-documentation-readings-order"></a>

## Where to start

- [00_scripts.md](../api/en/00_scripts.md) explains which scripts prepare the environment, manage dependencies, build projects, and run checks, and how they integrate with VS Code.
- [FwProjectWorkflow.md](FwProjectWorkflow.md) covers the manifest, target and source selection, compilation, and firmware upload.
- [OTAWorkflow.md](OTAWorkflow.md) covers RP and ESP32-S3 OTA provisioning, network and firewall configuration, updates, boot confirmation, and recovery.

Each guide contains the commands, interfaces, and configuration examples for its subject. Related guides provide context and links rather than duplicate those details.

---

## Public API vs helper modules

The repository contains both the HAL itself and a set of utility modules.

### HAL public API

The public HAL interfaces separate application logic from platform-specific SDK calls:

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

Utility modules complement the HAL; they do not replace its hardware abstraction:

- `tools`
- `SmartTimers`
- `pidController`
- `multicoreWatchdog`
- `draw7Segment`

In new applications, use HAL APIs for hardware access. Use utility modules where they help, but do not treat them as a replacement for the portability layer.

---

## Board profiles and runtime capabilities

`HAL_TARGET_*` identifies the MCU and ISA. `JH_BOARD` selects the physical
profile from `boards/profiles/`; the generator emits the matching
`HAL_BOARD_PROFILE_*` selector and target configuration. The board descriptors
are the authoritative profile inventory. List the current IDs with:

```bash
python3 scripts/generate_board_config.py --boards-root boards --list boards
```

The ESP32-S3 component provides `hal_board` using generated target, board, and link metadata. A declared hardware capability remains `HAL_BOARD_CAP_INACTIVE` until the responsible module reports that it is available or has failed.

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

`hal/system/hal_board.h` defines stable profile identifiers, the capability bitmask, availability states, a board-information snapshot, and query functions. The generated `src/hal/generated/jh_board_registry.h` maps registry profiles to those public identifiers without requiring a second, manually maintained list.

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
| 0 | [Process scripts and orchestration](../api/en/00_scripts.md) | Prepare the environment, run builds and checks, and locate each tool's inputs and outputs. |
| 1 | [Library compilation guide](lib_compilation.md) | Building for all targets, generated board metadata, static-library helpers, FreeRTOS build variants, and firmware integration |
| P | [Firmware project workflow](FwProjectWorkflow.md) | Configure, build, upload and debug a firmware project through VS Code or the CLI. |
| O | [Native OTA workflow](OTAWorkflow.md) | Prepare RP or ESP32-S3 for OTA, update firmware, confirm trial boots and recover failed updates. Includes security limits. |
| 2 | [Module flags and configuration](../api/en/02_module_flags.md) | `HAL_ENABLE_*` opt-in flags, dependency propagation, FreeRTOS selection, stack-size overrides, and core modules |
| A | [Status API](../api/en/01_status_api.md) | Interpret operation results and migrate calls to status-returning APIs. |
| 3 | [Build dependencies, tests, and hardware fixtures](../api/en/03_build_tests.md) | Build and run host tests, understand CI coverage and select device-test procedures. |
| 4 | [Multicore safety and drivers](../api/en/04_multicore_drivers_migration.md) | Share modules between cores and tasks, initialize resources and configure logging. |
| S | [Security supply chain](security_supply_chain.md) | Third-party inventory, CycloneDX SBOM generation, vulnerability scanning and CVE/CVSS assessment workflow |
| 5 | [GPIO, ADC and PWM](../api/en/05_gpio_adc_pwm.md) | `hal_gpio`, `hal_pwm`, `hal_dac`, `hal_pcnt`, `hal_pwm_freq`, `hal_dacless`, `hal_adc` |
| 6 | [Timers and system](../api/en/06_timers_system.md) | Schedule timers, read clocks, control power modes and use system diagnostics and portable compiler helpers. |
| 7 | [Cryptography](../api/en/07_crypto.md) | `hal_crypto` - Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20, ChaCha20-Poly1305 |
| 8 | [Sync, USB, serial, framing and auth](../api/en/08_sync_serial.md) | Synchronize work, use USB and serial diagnostics, and exchange framed or authenticated commands. |
| 9 | [Communication buses](../api/en/09_buses.md) | Transfer data over SPI, I2C, UART and 1-Wire; check bus locking and transfer limits. |
| 10 | [CAN bus and display](../api/en/10_can_display.md) | Use Classical CAN or CAN FD and draw text or images through the display API. |
| 11 | [Sensors](../api/en/11_sensors.md) | Read sensors and GPS data, use RTCs and handle decoded input signals. |
| 12 | [Cellular modem](../api/en/12_modem.md) | `hal_modem_at` (AT engine, URC, watchdog cooperation), `hal_simcom_a76xx` (A7670/A7672 - power, boot, SIM, PDP, LBS, GNSS, MQTT subscribe) |
| 13 | [Output devices](../api/en/13_output_devices.md) | Control audio output, LEDs and output devices. |
| 14 | [Storage](../api/en/14_storage.md) | Store settings and files, manage flash and record logs. Includes persistence and concurrency rules. |
| 15 | [Network connectivity](../api/en/15_connectivity.md) | Use WiFi, TCP/UDP and network services, including TLS, HTTP, MQTT, OTA and NTP. |
| 16 | [Utilities](../api/en/16_utilities.md) | Use domain helpers, software timers, PID control and C++ compatibility utilities. |
| 17 | [cJSON](../api/en/17_cJSON.md) | Parse and write JSON, manage object ownership and apply Pointer or Patch operations. |
| 18 | [LodePNG](../api/en/18_LodePNG.md) | Decode PNG images and prepare RGB565 assets within embedded memory limits. |
| 19 | [JPEG](../api/en/19_JPEG.md) | Decode JPEG images and prepare RGB565 assets within embedded memory limits. |
| 20 | [Bluetooth](../api/en/20_bluetooth.md) | Use BLE, Classic profiles, gamepads, audio and authenticated streams. Check board support and radio-sharing rules. |
| 21 | [Raw LoRa radio](../api/en/21_lora.md) | Configure LoRa radios and transfer packets. SX1262 is hardware-validated; SX1261/SX1276/SX1278 remain experimental. |
| 22 | [Reliable LoRa link](../api/en/22_lora_link.md) | Exchange addressed LoRa messages with acknowledgements, retries, fragmentation and optional encryption. |
| 23 | [Command routing](../api/en/23_commands.md) | Register and dispatch commands, encode messages and connect serial, network, LoRa or BLE adapters. |
| 24 | [Hardware period capture](../api/en/24_pulse_capture.md) | Hardware timestamps and 32-period frequency windows on RP, STM32G474, ESP32-S3, and mock |
| 25 | [Hardware-paced ADC scan](../api/en/25_adc_scan.md) | Continuous DMA scan of a pin set into caller-owned blocks on RP, STM32G474, ESP32-S3, and mock |

---

## Quick module lookup

| Module | Section |
|--------|---------|
| `hal_adc` | [GPIO, ADC and PWM](../api/en/05_gpio_adc_pwm.md) |
| `hal_bh1750` | [Sensors](../api/en/11_sensors.md) |
| `hal_ble` | [Bluetooth Low Energy](../api/en/20_bluetooth.md) |
| `hal_ble_stream` | [JH BLE Stream v1](../api/en/20_bluetooth.md#jh-ble-stream-v1) |
| `hal_ble_commands` | [Authenticated BLE Stream adapter](../api/en/23_commands.md#authenticated-ble-stream-adapter) |
| `hal_bluetooth_classic` | [Bluetooth Classic manager](../api/en/20_bluetooth.md#classic-manager) |
| `hal_bluetooth_hid_host` | [Generic Classic HID Host](../api/en/20_bluetooth.md#generic-hid-host) |
| `hal_gamepad` | [Bluetooth gamepad adapter](../api/en/20_bluetooth.md#gamepad-adapter) |
| `hal_bluetooth_a2dp_sink` | [A2DP Sink](../api/en/20_bluetooth.md#a2dp-sink-and-avrcp-target) |
| `hal_bluetooth_avrcp_target` | [AVRCP Target](../api/en/20_bluetooth.md#a2dp-sink-and-avrcp-target) |
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
| `hal_pulse_capture` | [Hardware period capture](../api/en/24_pulse_capture.md) |
| `hal_adc_scan` | [Hardware-paced ADC scan](../api/en/25_adc_scan.md) |
