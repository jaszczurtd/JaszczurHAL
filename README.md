# JaszczurHAL

Author: Marcin 'Jaszczur' Kielesinski

JaszczurHAL is a hardware abstraction layer and utility library for embedded projects.

RP2040 and RP2350 firmware builds directly against the official Pico SDK.
STM32G474 is supported through the repository's bare-metal and FreeRTOS
runtimes and linker flow. The mock backend provides deterministic host-side
validation.

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

## Supported modules (quick overview)

See [features.md](doc/features.md) for a compact inventory of supported
functionality and source links.

## Is this used by anything real?

Yes - by several of my more demanding projects.

The most visible example of JaszczurHAL in practice is the Fiesta project: https://github.com/jaszczurtd/Fiesta

It is my private retrofit/automotive-kind project built from several tightly integrated modules. The ECU module is probably the most demanding one: it uses JaszczurHAL for VP37 injection pump control, CAN communication with the rest of the system, OBD diagnostics, and other low-level functionality.

There are also smaller (but not trivial) projects, for example:

* https://github.com/jaszczurtd/doomConsole (port of Doom game with sound and TFT display)
* https://github.com/jaszczurtd/Ford-Mondeo-MK-DPF-Tracker (DPF regeneration cycles tracking device)
* https://github.com/jaszczurtd/lights-timer (Remote management of aquarium lighting using an Android application)

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

## Thread safety (overview)

Thread safety is a core design principle across all targets. Initialization
and teardown paths (`init` / `create` / `destroy` / `deinit`) are treated as
single-core operations; singleton and per-bus locks are created atomically on
first use through defensive lazy mutex creation. The mock backend targets
deterministic single-threaded tests, and the optional
`JH_ENABLE_FREERTOS_POSIX_TESTS` flag adds host-side FreeRTOS scheduler
coverage on top of it.

For detailed signatures, exact guarantees, module contracts, backend notes, and test coverage,
see [JaszczurHAL_API.md](doc/JaszczurHAL_API.md).

## Library structure

```text
CMakeLists.txt              # host/mock tests build
VERSION                     # project version
.build/                     # ignored root for all managed build artifacts
boards/                     # target, board and capability descriptors
rp_native_lib/              # Pico SDK RP2040/RP2350 static-library build
  MEMORY_MAP.md             # native RP firmware/storage/OTA layout
cmake/
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
  tools/create-vscode-example.py # standalone VS Code firmware project generator
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
  hal/                      # HAL public headers + common wrappers
    hal_target.h            # backend selection
    hal_config.h            # feature flags, dependency propagation, project hook
    impl/
      .mock/                # deterministic host/test backend
      rp2040/               # RP-family backend
        drivers/flash/      # native RP flash coordinator and storage partitions
        drivers/rp2040/     # RP2040 SoC services (fault/system)
        drivers/usb/        # native TinyUSB CDC configuration/descriptors
        freertos/           # native RP FreeRTOSConfig and hooks
        frameworks/         # RP-specific framework integrations
      shared/               # target-neutral drivers/engines reused by RP2040 + STM32
        debug/              # shared serial/debug formatting
        drivers/            # hardware-oriented drivers and transaction engines
        frameworks/         # reusable engines/stacks and bundled portable libs
        network/
          adapters/bsd/     # public BSD/POSIX adapter over HAL UDP/TCP
          services/         # HTTP, WebSocket, console and command services
      stm32g474/            # STM32G474 backend
        drivers/
          stm32g474/        # STM32G474 SoC services (fault/system)
        freertos/           # STM32 FreeRTOSConfig and hooks
        port/               # startup, SystemInit, linker-facing low-level glue
  utils/                    # tools, PID, watchdog, draw helpers, Unity
tests/                      # host unit tests (CMake + Unity)
  freertos_posix/           # optional host-side FreeRTOS POSIX scheduler tests
  hardware/                 # tracked RP fixture sources/manifests and host verifiers
third_party/                # tracked pins + ignored managed component installs
  update_components.sh      # synchronize every component to its tracked pin
  *_version.conf            # tracked source/tool/toolchain version definitions
  littlefs/                 # ignored pinned upstream filesystem checkout
```

`src/hal/impl/shared/` contains internal, backend-agnostic implementation code
reused by at least two hardware backends. It depends only on HAL-level
contracts, behaves identically across supported targets, and keeps per-target
`#if HAL_TARGET_IS_*` branches out of shared implementation files. The role
split (`drivers/`, `frameworks/`, `network/`, `debug/`) is described in
[JaszczurHAL_API.md](doc/JaszczurHAL_API.md).

## Quick start

There are two common starting points:

- To explore HAL APIs, portability patterns, and backend coverage, start with
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

The `examples/` tree contains small, focused applications that demonstrate HAL
modules. Each example is a portable `app.c`/`app.cpp` with a matching
`hal_project_config.h`, built on the portable entry-point contract:
`app_start()`, `app_task0()`, and optional `app_task1()`
(`HAL_ENABLE_APP_TASK1`, mapped to dual-core execution on RP and cooperative
calls on STM32G474).

Each numbered example is also a dispatcher-backed VS Code firmware project:
open any `examples/NN_name/` directory directly in VS Code to get the standard
JaszczurHAL tasks.

```bash
# full example matrix per target
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"

# single example
vscode/entry/jh-vscode build --project examples/01_blink --target rp2040
```

The build matrix, requirements, and per-example target coverage are maintained
in [examples/README.md](examples/README.md).

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

For the complete flag matrix, dependency propagation rules, and `HAL_ENABLE_*` options,
see:

- [JaszczurHAL_API.md](doc/JaszczurHAL_API.md)
- [doc/HAL_FLAGS.txt](doc/HAL_FLAGS.txt)

## Target selection (multiplatform)

Separate from the per-module flags, JaszczurHAL selects exactly one hardware
backend through `src/hal/hal_target.h`. Define one
of the following in `hal_project_config.h` (or via `-D`):

```c
#define HAL_TARGET_RP2040        // RP2040, Cortex-M0+
#define HAL_TARGET_RP2350_ARM    // RP2350, Cortex-M33
#define HAL_TARGET_RP2350_RISCV  // RP2350, Hazard3 RISC-V
#define HAL_TARGET_STM32G474     // STM32G474
#define HAL_TARGET_MOCK          // host unit-test / simulation backend
```

If you define none, the target is **auto-detected** from the toolchain.
Selecting two targets - or a
bare-metal build with no detectable target - is a compile-time `#error`.
Backend files compile only for their selected target, so unused backends cost zero code.

RP code uses `HAL_TARGET_IS_RP` for family-wide paths and the exact
`HAL_TARGET_IS_RP2040`, `HAL_TARGET_IS_RP2350_ARM`, or
`HAL_TARGET_IS_RP2350_RISCV` only for real chip differences.
`HAL_RP_ARCH_ARM`/`HAL_RP_ARCH_RISCV` select ISA-specific paths.

Official builds select a stable target and board ID through the generated board
registry. Supported profiles include `pico`, `picow`, `pico2`, `pico2w`,
`pico-rm2`, `rp2040-zero`, `rp2040-plus-4mb`, `nucleo-g474re`, and
`host-mock`. The build generator validates target compatibility, flash size,
pins, components, and feature contracts before toolchain import. See
[Target and board profiles](doc/boards_profiles_howto.md).

Runtime board capabilities (USB, CYW43, external radio) are exposed through the
`hal_board` API, described in [JaszczurHAL_API.md](doc/JaszczurHAL_API.md).
Native RP details live next to their subsystems: TinyUSB CDC ownership and the
flash transaction coordinator in [JaszczurHAL_API.md](doc/JaszczurHAL_API.md),
the firmware/storage/OTA flash layout in
[rp_native_lib/MEMORY_MAP.md](rp_native_lib/MEMORY_MAP.md), and the staged OTA
contract (boot applier, trial confirmation, rollback) in
[OTAWorkflow.md](doc/OTAWorkflow.md).

Dispatcher-backed VS Code projects select the active family/board with the
manifest, `.vscode/jaszczurhal.local.json`, or `--target`/`--board`; the
dispatcher then pins `JH_TARGET` for CMake and lets `hal_target.h` select the
HAL backend. See [FwProjectWorkflow.md](doc/FwProjectWorkflow.md) for the full
target/board/configuration model.

## FreeRTOS opt-in

FreeRTOS support is selected with an explicit compile-time flag:

```c
#define HAL_ENABLE_FREERTOS
```

Applications on RP2040, RP2350, and STM32G474 use the upstream FreeRTOS headers
and APIs directly; JaszczurHAL starts the scheduler and pins the optional
application entry tasks to their cores. RP builds use the pinned FreeRTOS-Kernel
with SMP ports for all three RP variants, STM32G474 uses the same pinned kernel
with its Cortex-M4F port. Kernel pinning, port notes, and build variants are
documented in [lib_compilation.md](doc/lib_compilation.md) and
[doc/api/04_multicore_drivers_migration.md](doc/api/04_multicore_drivers_migration.md).

## Building as a static library (.a)

The complete guide for compiling JaszczurHAL to a linkable static library
(`libJaszczurHAL.a`), including example-application builds and the core/entry
policy: [lib_compilation.md](doc/lib_compilation.md)

```bash
./scripts/build_rp_native_lib.sh --target rp2040
./scripts/build_rp_native_lib.sh --target rp2350-arm
./scripts/build_rp_native_lib.sh --target rp2350-riscv
./scripts/build_stm32_lib.sh
```

## Tests and quality gates

Host unit tests run against the deterministic mock backend:

```bash
cmake -S . -B .build/host
cmake --build .build/host
ctest --test-dir .build/host --output-on-failure
```

The full local gate mirrors CI (`.github/workflows/ci.yml`), which runs on
every push and pull request to `main`:

```bash
./runalltests.sh
```

It covers host unit tests (with FreeRTOS POSIX coverage), Valgrind memcheck,
`clang-tidy` and `cppcheck` static analysis, documentation link validation,
and library/firmware compile gates for RP2040, RP2350 ARM, RP2350 RISC-V and
STM32G474 across `HAL_ENABLE_*` profiles, including every declared example and
hardware-fixture compile matrix.

Tool configuration lives alongside the sources: `.clang-tidy`,
`tests/cppcheck-suppressions.txt`, `tests/valgrind.supp`, and
`scripts/clang_tidy_files.py`. Suite layout, dependencies, and instructions for
adding tests are in [doc/api/03_build_tests.md](doc/api/03_build_tests.md).

## Git hooks (format + commit message)

The repository ships versioned hooks in `.githooks/`: `pre-commit` (staged-file
normalization and `clang-format`) and `commit-msg` (conventional commits).
Install them once per clone together with the required tools:

```bash
./runmefirst.sh
```

During setup, the script checks the host OTA callback rule and asks before
persistently allowing LAN-scoped TCP/8266 traffic. It uses the active UFW,
firewalld, or iptables backend and leaves the firewall unchanged when the
request is declined.

Setup details are in [doc/api/00_scripts.md](doc/api/00_scripts.md); the manual
equivalent is `git config core.hooksPath .githooks` with `clang-format`
installed.

## Security and SBOM

JaszczurHAL keeps a lightweight software supply-chain record for bundled and
pinned dependencies:

- [SECURITY.md](SECURITY.md) - vulnerability reporting, triage and maintenance
  policy,
- [doc/security_supply_chain.md](doc/security_supply_chain.md) - SBOM
  generation, vulnerability checks, and the CI `security-scan` policy,
- [security/third_party.json](security/third_party.json) - human-maintained
  third-party inventory,
- [security/sbom.cdx.json](security/sbom.cdx.json) - generated CycloneDX SBOM.

## VS Code Development Environment

`vscode/` is the supported VS Code integration layer for firmware projects that
use JaszczurHAL. Projects call the stable entrypoint:

```text
libraries/JaszczurHAL/vscode/entry/jh-vscode
```

The entrypoint resolves project configuration, selects the active target/board,
builds dispatcher-backed CMake firmware, performs identity-verified serial
upload, handles RP2040 BOOTSEL/UF2 upload, delegates STM32 flashing to OpenOCD,
starts persistent serial monitors, and refreshes IntelliSense.

- CLI contract, task labels, keyboard shortcuts, and the project generator:
  [vscode/README.md](vscode/README.md)
- End-to-end project model and
  [adding project source files](doc/FwProjectWorkflow.md#adding-project-source-files):
  [FwProjectWorkflow.md](doc/FwProjectWorkflow.md)
- Native RP network updates, first flash, and firewall requirements:
  [OTAWorkflow.md](doc/OTAWorkflow.md)

Linux is the primary supported environment for this workflow. Windows users
should prefer WSL2 or a Bash-backed VS Code shell until native Windows parity is
explicitly implemented.

## Managed dependencies

Tracked pins for Pico SDK, picotool, the RP2350 RISC-V toolchain, FreeRTOS,
BearSSL, lwIP, and littlefs live in `third_party/*_version.conf`:

```bash
./third_party/update_components.sh
./third_party/update_components.sh --verify-only
```

The complete component contract, including the update checklist (security
inventory, SBOM, affected builds, full gate), is documented in
[third_party/README.md](third_party/README.md).

## Documentation

Primary docs:

- Process scripts and orchestration: [00_scripts.md](doc/api/00_scripts.md)
- API reference: [JaszczurHAL_API.md](doc/JaszczurHAL_API.md)
- Firmware project workflow: [FwProjectWorkflow.md](doc/FwProjectWorkflow.md)
- Native RP OTA workflow: [OTAWorkflow.md](doc/OTAWorkflow.md)
- Target and board profiles: [boards_profiles_howto.md](doc/boards_profiles_howto.md)
- Changelog: [CHANGELOG.md](doc/CHANGELOG.md)
- Build-time flags summary: [HAL_FLAGS](doc/HAL_FLAGS.txt)
- Linkable static library build guide: [lib_compilation.md](doc/lib_compilation.md)
- VS Code firmware workflow: [vscode/README.md](vscode/README.md)

## Notes and credits

- SmartTimers is based on [Nettigo Timers](https://github.com/nettigo/Timers)
  (fork of [garthoff/Timers](https://github.com/garthoff/Timers)).
- Unity test framework sources are bundled in src/:
  [ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity)
- The shared display stack (`src/hal/impl/shared/drivers/display/`) is a portable,
  HAL-based reimplementation. The GFX engine (`jh_gfx.*`) adapts rendering
  algorithms from [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library),
  and the panel drivers (`ili9341_driver.*`, `st77xx_driver.*`,
  `ssd1306_driver.*`) adapt the controller command sequences from the
  corresponding Adafruit ILI9341 / ST7735-ST7789 / SSD1306 libraries by
  Limor Fried (Ladyada) for Adafruit Industries (BSD-2-Clause). The SSD16xx
  and UC81xx e-paper protocol/state machines use the Zephyr drivers logic (Apache-2.0-Clause). See the file headers for per-module attribution.
- Bundled, ported, or locally adapted third-party components:
  [cJSON / cJSON_Utils](src/hal/impl/shared/frameworks/cjson/),
  [LodePNG](src/hal/impl/shared/frameworks/lodepng/),
  [JPEGDecoder / picojpeg](src/hal/impl/shared/frameworks/jpeg/),
  [FatFs](src/hal/impl/shared/frameworks/filesystem/ff16/),
  [FreeRTOS-Kernel pin](third_party/freertos_core_version.conf),
  [BearSSL pin](third_party/bearssl_version.conf),
  [lwIP pin](third_party/lwip_version.conf),
  [littlefs pin](third_party/littlefs_version.conf),
  [PubSubClient](src/hal/impl/shared/frameworks/PubSubClient/),
  [shared WireGuard/lwIP engine](src/hal/impl/shared/frameworks/wireguard/),
  [LiquidCrystal / HD44780](src/hal/impl/shared/drivers/hd44780/),
  [Brian Varren DACless](src/hal/impl/shared/drivers/dacless/),
  [Seeed/Loovee MCP_CAN / MCP2515](src/hal/impl/shared/drivers/mcp2515/),
  [MCP251XFD](src/hal/impl/shared/drivers/mcp251xfd/),
  [Adafruit NeoPixel](src/hal/impl/shared/drivers/neopixel/),
  [Adafruit STMPE610](src/hal/impl/shared/drivers/stmpe610/),
  [Adafruit TSC2007](src/hal/impl/shared/drivers/tsc2007/),
  [Paul Stoffregen OneWire](src/hal/impl/shared/drivers/onewire/),
  [Bonezegei DHT11/DHT22 by Bonezegei (Jofel Batutay)](src/hal/impl/shared/drivers/dht/),
  [Adafruit MAX6675](src/hal/impl/shared/drivers/max6675/),
  [Adafruit MCP9600](src/hal/impl/shared/drivers/mcp9600/),
  [ArtronShop BH1750](src/hal/impl/shared/drivers/bh1750/),
  [Eric Ayars / JeeLabs / RTClib-style DS3231](src/hal/impl/shared/drivers/ds3231/),
  [IRsmallDecoder / RC5 decoder attribution](src/hal/impl/shared/frameworks/irsmall_decoder/).
