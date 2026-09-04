# JaszczurHAL

*Also available in [Polish](README.pl.md).*

Author: Marcin 'Jaszczur' Kielesinski

JaszczurHAL is a hardware abstraction layer and utility library for embedded projects using RP2040/2350/STM32/ESP32.
Start with the [English documentation index](doc/table_of_contents.md), or see
the [feature overview](doc/en/features.md) for a compact inventory of supported
modules and functionality.

## How do you even pronounce this library name?

Like this: **"YASH-choor-HAL"**. You're welcome. :)

## Why this exists?

Many embedded projects start as quickly written code and become increasingly
difficult to evolve over time, especially when hardware access is tightly
coupled with application logic or when drivers are bound to a specific hardware
target.

JaszczurHAL introduces a practical boundary:

- application layer: portable logic,
- HAL layer: consistent, portable APIs that keep hardware details separate
  from application logic,
- mock layer: deterministic host-side testing,
- reusable, thread-safe drivers shared across all supported hardware targets,
- optional modules controlled by compile-time `HAL_ENABLE_*` flags (opt-in),
- optional connectivity/security/storage stack for connected firmware projects,
- utility toolkit for common embedded patterns (timers, PID, watchdog, helpers),
- fully functional FreeRTOS support (V11.3.0).

Application code stays portable across the supported targets and runtimes.

The project is already very useful, but there are still a few areas here and there that remain WIP. Unfortunately, besides working on a hobby project, one also has to earn a living - and find time for life itself. :)

## Is this used by anything real?

Yes - by several of my more demanding projects.

The most visible example of JaszczurHAL in practice is the Fiesta project: https://github.com/jaszczurtd/Fiesta

It is my private retrofit/automotive-kind project built from several tightly integrated modules. The ECU module is probably the most demanding one: it uses JaszczurHAL in two cores mode, for VP37 injection pump control, CAN communication with the rest of the system, OBD diagnostics, and other low-level functionality.

There are also smaller (but not trivial) projects, for example:

* https://github.com/jaszczurtd/doomConsole (port of Doom game with sound, TFT display, and 8BitD0 Bluetooth gamepad)
* https://github.com/jaszczurtd/Ford-Mondeo-MK-DPF-Tracker (DPF regeneration cycles tracking device)
* https://github.com/jaszczurtd/lights-timer (Remote management of aquarium lighting using an Android application)

## Quick start

There are two common starting points:

- To explore [HAL APIs](doc/en/JaszczurHAL_API.md), portability patterns, and backend coverage, start with
  the checked-in examples: [examples/README.md](examples/README.md).
- To create a new target-selectable firmware project for day-to-day work in
  VS Code, use the project generator:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output your-example-project-name
```

The generated project starts on `rp2040/pico` (change it with
`--target`/`--board` or the `Project: Select board` task) and ships with
ready-to-run VS Code tasks for build/upload/monitor. Generator options, first
flash of a blank board, and the full task reference are documented in
[vscode/README.md](vscode/README.md).

## Examples

The `examples/` tree contains consolidated applications that demonstrate
related HAL modules together. Each example is a portable `app.c`/`app.cpp` with a matching
`hal_project_config.h`, built on the portable entry-point interface:
`app_start()`, `app_task0()`, and optional `app_task1()`
(`HAL_ENABLE_APP_TASK1`, mapped to dual-core execution on RP and cooperative
calls on bare-metal STM32G474). ESP-IDF maps `app_start()`, `app_task0()`, and
optional `app_task1()` to its already-running FreeRTOS scheduler. ESP32-S3
defaults task0/task1 to cores 0/1 and allows explicit affinity overrides.

The build matrix, requirements, per-example target coverage, and the rule to
extend an existing project or variant before creating another directory are
maintained in [examples/README.md](examples/README.md).

## Supported targets and modules (quick overview)

RP2040 and RP2350 firmware is built directly against the official Pico SDK.
STM32G474 is supported through the repository's bare-metal implementation and
linker flow. The ESP32-S3 target is built on top of ESP-IDF. FreeRTOS is optional
on RP and STM32G474 and required by the ESP-IDF runtime. The mock backend provides deterministic host-side validation.

See the [feature overview](doc/en/features.md) for a compact inventory of
supported functionality and modules.

## Module selection (quick)

JaszczurHAL uses an OPT-IN flag model: by default no optional module is compiled. To enable the modules your project uses, define `HAL_ENABLE_*`
flags in a project-local
`hal_project_config.h`:

```c
#pragma once
#define HAL_ENABLE_WIFI
#define HAL_ENABLE_TIME
#define HAL_ENABLE_GPS
```

The project uses its own validation mechanisms to check whether a given flag is valid and supported, and whether its parameters are correct.

For the complete flag matrix, dependency propagation rules, and `HAL_ENABLE_*` options,
see:

- [JaszczurHAL_API.md](doc/en/JaszczurHAL_API.md)
- [doc/api/en/02_module_flags.md](doc/api/en/02_module_flags.md)
- [doc/HAL_FLAGS.txt](doc/HAL_FLAGS.txt)

## Target selection example (multiplatform)

Separate from the per-module flags, JaszczurHAL selects exactly one hardware
backend through `src/hal/core/hal_target.h`. Define one
of the following in `hal_project_config.h` (or via `-D`):

```c
#define HAL_TARGET_RP2040        // RP2040, Cortex-M0+
#define HAL_TARGET_RP2350_ARM    // RP2350, Cortex-M33
#define HAL_TARGET_RP2350_RISCV  // RP2350, Hazard3 RISC-V
#define HAL_TARGET_STM32G474     // STM32G474
#define HAL_TARGET_ESP32_S3      // ESP32-S3, native ESP-IDF
#define HAL_TARGET_MOCK          // deterministic host test backend
```

If you define none, the target is **auto-detected** from the toolchain.
Backend files compile only for their selected target, so unused backends cost zero code.

Official builds select a stable target and board ID through the generated board
registry. See
[Target and board profiles](doc/en/boards_profiles_howto.md).
Also see [FwProjectWorkflow.md](doc/en/FwProjectWorkflow.md) for the full
target/board/configuration model.

In practice, you do not need to know how the target-selection logic works internally.
Just press `Ctrl+Shift+Alt+1` in your project and select the target from the menu.

This is the [full list](vscode/README.md#vs-code-keyboard-shortcuts) of available VS Code Keyboard Shortcuts.

## FreeRTOS opt-in

FreeRTOS support is selected with an explicit compile-time flag:

```c
#define HAL_ENABLE_FREERTOS
```

Applications use the standard upstream FreeRTOS headers and APIs directly on all supported targets.

JaszczurHAL hides the target-specific startup details, such as scheduler startup
and optional application task placement. RP targets use the pinned
FreeRTOS-Kernel with SMP support, while STM32G474 uses the same kernel with the
Cortex-M4F port. ESP32-S3 uses the FreeRTOS instance supplied by the pinned
ESP-IDF; its target descriptor adds `HAL_ENABLE_FREERTOS` as a required feature
and supports the optional second application task.

Detailed notes about kernel pinning, ports, and build variants are available in [lib_compilation.md](doc/en/lib_compilation.md) and [doc/api/en/04_multicore_drivers_migration.md](doc/api/en/04_multicore_drivers_migration.md).

## Thread safety (overview)

Thread/multicore safety is a core design principle across all targets. Initialization
and teardown paths (`init` / `create` / `destroy` / `deinit`) are treated as
single-core operations; singleton and per-bus locks are created atomically on
first use through defensive lazy mutex creation. The mock backend targets
deterministic single-threaded tests, and the optional
`JH_ENABLE_FREERTOS_POSIX_TESTS` flag adds host-side FreeRTOS scheduler
coverage on top of it.

For detailed signatures, exact guarantees, module behavior, backend notes, and test coverage,
see [JaszczurHAL_API.md](doc/en/JaszczurHAL_API.md).

## Building as a static library (.a)

The complete guide for compiling JaszczurHAL to a linkable static library
(`libJaszczurHAL.a`), including example-application builds and the core/entry
policy: [lib_compilation.md](doc/en/lib_compilation.md). Installed RP and
STM32G474 packages include the generated feature and board headers, resolved
board metadata, and link-compatibility reference source required by a direct
compiler consumer. Compiling and linking the installed package does not invoke
Python.

## Tests and quality gates

```bash
./runalltests.sh
```

It covers host unit tests (with FreeRTOS POSIX coverage), Clang
ASan/UBSan/libFuzzer checks, Valgrind memcheck, static analysis, duplicate and
documentation checks, and target/firmware build matrices. Physical hardware
fixtures are documented and executed separately.
The complete
test architecture, requirements, configuration, extension rules, fixture
procedures, and recorded results are in
[Build dependencies, tests, and hardware fixtures](doc/api/en/03_build_tests.md).
Runner and quality-gate implementation details are in
[JaszczurHAL Process Scripts](doc/api/en/00_scripts.md).

## Security and SBOM

JaszczurHAL keeps a lightweight software supply-chain record for bundled and
pinned dependencies:

- [SECURITY.md](SECURITY.md) - vulnerability reporting, triage and maintenance
  policy,
- [doc/en/security_supply_chain.md](doc/en/security_supply_chain.md) - SBOM
  generation, vulnerability checks, and the CI `security-scan` policy,
- [security/third_party.json](security/third_party.json) - human-maintained
  third-party inventory,
- [security/sbom.cdx.json](security/sbom.cdx.json) - generated CycloneDX SBOM.

## VS Code Development Environment

`vscode/` is the supported VS Code integration layer for firmware projects that
use JaszczurHAL. Projects call the stable entrypoint:

```text
libraries/JaszczurHAL/vscode/entry/jh-vscode
libraries/JaszczurHAL/vscode/entry/jh-vscode.cmd
```

The entrypoint resolves project configuration, selects the active target/board,
builds dispatcher-backed CMake firmware, performs identity-verified serial
upload, handles RP2040 BOOTSEL/UF2 upload, delegates STM32 flashing to OpenOCD,
delegates ESP32-S3 build/flash to the production ESP-IDF runner, starts
persistent serial monitors, and refreshes IntelliSense from the active
toolchain's compile database.

- CLI interface, task labels, keyboard shortcuts, and the project generator:
  [vscode/README.md](vscode/README.md)
- End-to-end project model and
  [adding project source files](doc/en/FwProjectWorkflow.md#adding-project-source-files):
  [FwProjectWorkflow.md](doc/en/FwProjectWorkflow.md)
- Native RP and ESP32-S3 network updates, first flash, and security boundaries:
  [OTAWorkflow.md](doc/en/OTAWorkflow.md)
- [Full list of keyboard shortcuts](vscode/README.md#vs-code-keyboard-shortcuts)

When the JaszczurHAL repository root itself is opened in VS Code, the tracked
`.vscode/` configuration provides a separate static-library workflow. The
existing global shortcuts build, install, clean, and refresh IntelliSense for
one target/board profile selected directly from the shared board registry.
Artifacts remain below `.build/vscode/library/`; details are in the
[library compilation guide](doc/en/lib_compilation.md#repository-workspace-and-vs-code).

## Debugging with VS Code

Generated Cortex-Debug profiles support RP2040 and RP2350 Arm over SWD with a
Raspberry Pi Debug Probe or a Pico running Debug Probe/Picoprobe firmware.
STM32G474 projects use the on-board ST-Link of the NUCLEO-G474RE. The VS Code
Run and Debug workflow builds and loads the Debug ELF with managed OpenOCD and
an Arm-capable GDB on Windows and Linux; see
[Native Windows Setup](doc/en/windows_setup.md) for wiring and setup details.

Both Linux and native Windows provide the VS Code firmware-development
workflow for the released target paths. RP/STM targets provide their documented
build, upload, monitor, OTA, and debug capabilities. ESP32-S3 provides build,
serial upload, monitor, IntelliSense, and raw application OTA, but no managed
debug profile; its OTA path still requires the Phase 3.5 hardware validation
described above. Linux provides the full repository quality gate, including
Valgrind, static analysis, and POSIX-only host integrations. See
[Native Windows Setup](doc/en/windows_setup.md) for setup, verification, and the
explicit Linux-only boundaries.

## Managed dependencies

Tracked pins for Pico SDK, ESP-IDF, picotool, PMD CPD, the RP2350 RISC-V
toolchain, FreeRTOS, BearSSL, cJSON, LodePNG, TJpgDec, FatFs, Unity, lwIP,
littlefs, BTstack, and the Semtech SX126x driver live in
`third_party/*_version.conf`:

```bash
./third_party/update_components.sh
./third_party/update_components.sh --verify-only
```

The complete component policy, including the update checklist (security
inventory, SBOM, affected builds, full gate), is documented in
[third_party/README.md](third_party/README.md).

## Documentation

Primary docs:

- Complete index: [table_of_contents.md](doc/table_of_contents.md)
- Feature overview: [features.md](doc/en/features.md)
- Process scripts and orchestration: [00_scripts.md](doc/api/en/00_scripts.md)
- API reference: [JaszczurHAL_API.md](doc/en/JaszczurHAL_API.md)
- Firmware project workflow: [FwProjectWorkflow.md](doc/en/FwProjectWorkflow.md)
- Native OTA workflow: [OTAWorkflow.md](doc/en/OTAWorkflow.md)
- Target and board profiles: [boards_profiles_howto.md](doc/en/boards_profiles_howto.md)
- Build-time flags summary: [HAL_FLAGS](doc/HAL_FLAGS.txt)
- Linkable static library build guide: [lib_compilation.md](doc/en/lib_compilation.md)
- VS Code firmware workflow: [vscode/README.md](vscode/README.md)

## Notes and credits

- SmartTimers is based on [Nettigo Timers](https://github.com/nettigo/Timers)
  (fork of [garthoff/Timers](https://github.com/garthoff/Timers)).
- Unity test framework is pinned to the project fork:
  [Unity pin](third_party/unity_version.conf)
- The shared display stack (`src/hal/display/drivers/`) is a portable,
  HAL-based reimplementation. The GFX engine (`jh_gfx.*`) adapts rendering
  algorithms from [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library),
  and the panel drivers (`ili9341_driver.*`, `st77xx_driver.*`,
  `ssd1306_driver.*`) adapt the controller command sequences from the
  corresponding Adafruit ILI9341 / ST7735-ST7789 / SSD1306 libraries by
  Limor Fried (Ladyada) for Adafruit Industries (BSD-2-Clause). The SSD16xx
  and UC81xx e-paper protocol/state machines use the Zephyr drivers logic (Apache-2.0-Clause). See the file headers for per-module attribution.
- Bundled, ported, or locally adapted third-party components:
  [cJSON pin](third_party/cjson_version.conf),
  [LodePNG pin](third_party/lodepng_version.conf),
  [TJpgDec pin](third_party/jpeg_version.conf),
  [FatFs pin](third_party/fatfs_version.conf),
  [Unity pin](third_party/unity_version.conf),
  [FreeRTOS-Kernel pin](third_party/freertos_core_version.conf),
  [BearSSL pin](third_party/bearssl_version.conf),
  [lwIP pin](third_party/lwip_version.conf),
  [littlefs pin](third_party/littlefs_version.conf),
  [Semtech SX126x driver pin](third_party/sx126x_driver_version.conf),
  [PubSubClient](src/hal/network/mqtt/PubSubClient/),
  [shared WireGuard/lwIP engine](src/hal/network/wireguard/core/),
  [LiquidCrystal / HD44780](src/hal/display/hd44780/),
  [Brian Varren DACless](src/hal/audio/dacless/),
  [Seeed/Loovee MCP_CAN / MCP2515](src/hal/can/mcp2515/),
  [MCP251XFD](src/hal/can/mcp251xfd/),
  [Adafruit NeoPixel](src/hal/gpio/neopixel/),
  [Adafruit STMPE610](src/hal/input/stmpe610/),
  [Adafruit TSC2007](src/hal/input/tsc2007/),
  [Paul Stoffregen OneWire](src/hal/onewire/),
  [Bonezegei DHT11/DHT22 by Bonezegei (Jofel Batutay)](src/hal/temperature/dht/),
  [Adafruit MAX6675](src/hal/temperature/max6675/),
  [Adafruit MCP9600](src/hal/temperature/mcp9600/),
  [ArtronShop BH1750](src/hal/sensors/bh1750/),
  [Eric Ayars / JeeLabs / RTClib-style DS3231](src/hal/rtc/ds3231/),
  [IRsmallDecoder / RC5 decoder attribution](src/hal/input/irsmall_decoder/).
