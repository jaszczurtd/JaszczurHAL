# JaszczurHAL

Author: Marcin 'Jaszczur' Kielesinski

JaszczurHAL is a hardware abstraction layer and utility library for embedded projects.

The most complete backend currently targets RP2040/2350 boards through Arduino-Pico. STM32G474 is also supported as a real, fast bare-metal backend, with only a small number of modules still in progress. ESP32 is next in line.

## Why this exists

Many embedded projects start as quickly written code and become increasingly difficult to evolve over time, especially when hardware access is tightly coupled with application logic or when drivers are bound to a specific hardware target.

JaszczurHAL introduces a practical boundary:

- application layer: portable logic,
- HAL layer: consistent, portable APIs, that keep hardware details separate from application logic,
- Optional modules controlled by compile-time `HAL_ENABLE_*` flags (opt-in)
- Optional connectivity/security/storage stack for connected firmware projects
- Utility toolkit for common embedded patterns (timers, PID, watchdog, helpers)
- mock layer: deterministic host-side testing,
- reusable, thread-safe drivers shared across supported hardware targets,
- fully functional FreeRTOS support (V11.1.0).

This reduces lock-in to one runtime and makes migration from other SDKs much easier.

## Supported modules (quick overview)

See [features.md](doc/features.md) for a compact inventory of supported
functionality and source links.

## Is this used by anything real?

Yes - by several of my more demanding projects.

The most visible example of JaszczurHAL in practice is the Fiesta project: https://github.com/jaszczurtd/Fiesta

It is my private retrofit/automotive-kind project built from several tightly integrated modules. The ECU module is probably the most demanding one: it uses JaszczurHAL for VP37 injection pump control, CAN communication with the rest of the system, OBD diagnostics, and other low-level functionality.

There are also smaller (but not trivial) projects, for example:

* https://github.com/jaszczurtd/Ford-Mondeo-MK-DPF-Tracker
* https://github.com/jaszczurtd/lights-timer

Even these simpler projects exercise important parts of the library, including WireGuard-related cryptographic components, MQTT-based cloud connectivity, and GPS/LTE telemetry.

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

- Across all targets, thread safety is treated as a core design principle. Only a few modules intentionally deviate from this rule where enforcing thread safety would be impractical or unnatural,
- Initialization and teardown paths (`init` / `create` / `destroy` / `deinit`) are intentionally treated as single-core operations,
- Singleton and per-bus locks are initialized atomically on first use using defensive lazy mutex creation,
- The mock backend is intended for deterministic single-threaded tests rather than validating true concurrent synchronization, but FreeRTOS POSIX-based tests are also available through the optional `JH_ENABLE_FREERTOS_POSIX_TESTS` flag. `runalltests.sh` gate demostrates this in practice: it enables a host-side FreeRTOS POSIX scheduler test so `HAL_ENABLE_FREERTOS`, mutex/delay and lazy create-once behavior are covered in `ctest` without hardware.

For detailed signatures, exact guarantees, module contracts, backend notes, and test coverage,
see [JaszczurHAL_API.md](doc/JaszczurHAL_API.md).

## Library structure

```text
CMakeLists.txt              # host/mock tests build
library.properties          # Arduino library metadata
rp2040_lib/                 # RP2040 Arduino-pico static-library CMake glue
  MEMORY_MAP.md             # RP2040/RP2350 static-library memory-map notes
  toolchain_rp2040.cmake    # Arduino-pico CMake toolchain file
stm32_lib/                  # STM32G474 static-library CMake, toolchain, linker script
scripts/
  build_rp2040_lib.sh       # RP2040 static-library helper
  build_stm32_lib.sh        # STM32G474 static-library helper
  ensure_freertos_kernel.sh # pinned FreeRTOS-Kernel fetch/verify helper
runalltests.sh              # full local validation gate
runmefirst.sh               # one-time local toolchain setup
doc/
  JaszczurHAL_API.md        # detailed API/reference
  api/                      # split API chapters included by the main reference
  HAL_FLAGS.txt             # HAL_ENABLE_* flag summary
  lib_compilation.md        # static-library build guide
  features.md               # high-level feature matrix
  CHANGELOG.md              # project changelog
  STM32G474_porting_progress.md # STM32G474 backend status
  ESP32_porting_progress.md # ESP32 backend notes / future porting track
  datasheets/               # local reference PDFs and notes
  future_ideas.md           # architecture roadmap and backlog
examples/                   # buildable example apps for RP2040 and STM32G474
src/
  JaszczurHAL.h             # primary public include
  hal_app_entry.cpp         # optional portable app entry wrapper
  libConfig.h               # backward-compat include
  tools.h, tools_c.h        # utility aggregators (C++ / C)
  arpa/, netinet/, sys/     # host/embedded socket compatibility headers
  hal/                      # HAL public headers + common wrappers
    hal_target.h            # canonical backend selection
    hal_config.h            # feature flags, dependency propagation, project hook
    impl/
      .mock/                # deterministic host/test backend
      rp2040/               # RP2040 backend
        drivers/rp2040/     # RP2040 SoC services (fault/system)
        frameworks/         # Arduino-origin integrations (frameworks & drivers)
      shared/               # target-neutral drivers/engines reused by RP2040 + STM32
        compat/             # portable compatibility shims, e.g. BSD sockets/debug format
        drivers/            # hardware-oriented drivers: sensors, buses, displays
        frameworks/         # reusable engines/stacks and bundled portable libs
      stm32g474/            # STM32G474 backend
        drivers/
          littlefs/         # STM32 LittleFS backend support
          stm32g474/        # STM32G474 SoC services (fault/system)
        freertos/           # STM32 FreeRTOSConfig and hooks
        port/               # startup, SystemInit, linker-facing low-level glue
  utils/                    # tools, PID, watchdog, draw helpers, Unity
tests/                      # host unit tests (CMake + Unity)
  freertos_posix/           # optional host-side FreeRTOS POSIX scheduler tests
third_party/                # optional local dependencies, e.g. FreeRTOS-Kernel
vscode-templates/           # ready-to-use VS Code project configurations
  linux/                    # Linux/macOS template scripts and settings
  windows/                  # Windows template scripts and settings
```

The `src/hal/impl/shared/` folder contains internal, backend-agnostic implementation code reused by at least two hardware backends.
This code:

- depends only on HAL-level contracts,
- behaves identically across supported targets,
- avoids per-target `#if HAL_TARGET_IS_*` branches inside the shared implementation file.

Shared implementations are split by role:

- `shared/drivers/` contains hardware-oriented reusable drivers, for example
  `ads1x15/`, `digipot/`, `display/`, `mcp2515/`, `mcp251xfd/`,
  `pga2311/`, and sensor/display backends.
- `shared/frameworks/` contains larger reusable engines or protocol stacks,
  for example `filesystem/`, `gps/`, `irsmall_decoder/`, and `wireguard/`.

## Quick start
See [examples/README.md](examples/README.md) for the full build system guide.

## Examples

The `examples/` tree contains a number of small, focused applications that demonstrate
HAL modules. Each example is a portable `app.c`/`app.cpp` with a matching
`hal_project_config.h`.

A unified CMake build system compiles all examples for the selected backend:

```bash
# RP2040 (requires arduino-cli + rp2040 core)
cmake -S examples -B build_examples_rp2040 -DJH_EXAMPLE_TARGET=rp2040
cmake --build build_examples_rp2040

# STM32G474 (requires arm-none-eabi-gcc)
cmake -S examples -B build_examples_stm32 \
      -DJH_EXAMPLE_TARGET=stm32g474 \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32_lib/toolchain_stm32g474.cmake"
cmake --build build_examples_stm32

# Single example
cmake --build build_examples_rp2040 --target 01_blink_rp2040
```
Each example uses the portable entry-point contract (`app_start` /
`app_task0`, plus optional `app_task1`

The `app_task1` pattern (when `HAL_ENABLE_APP_TASK1` is defined) comes from RP2040/2350 nature - a dual-core execution.
STM32G474 supports the same application structure as well, but maps it to cooperative `app_task0` / `app_task1` calls.

When FreeRTOS is enabled, all targets gain full multithreading with consistent behavior. In that configuration, the `app_task1` pattern can simply be replaced by regular FreeRTOS tasks.

The vast majority of examples build for all supported targets and provide the same behavior across them. On STM32, the few remaining exceptions come from temporary gaps in module support, such as Wi-Fi See [examples/README.md](examples/README.md) for details.

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
- `doc/HAL_FLAGS.txt`

## FreeRTOS opt-in

FreeRTOS support is staged behind an explicit compile-time flag:

```c
#define HAL_ENABLE_FREERTOS
```

For FreeRTOS, there is no any `hal_rtos_*` wrapper API. Applications on RP2040/2350/STM32 just use native FreeRTOS headers and API directly.

On RP2040/RP2350, JaszczurHAL uses the FreeRTOS mode provided by Arduino-Pico.
On STM32G474, JaszczurHAL uses an integrated upstream FreeRTOS-Kernel checkout and provides FreeRTOS-aware implementations of mutexes, delays, idle handling, and optional application task startup.

Applications remain isolated from implementation differences below the FreeRTOS/JaszczurHAL API layer.

## Target selection (multiplatform)

Separate from the per-module flags, JaszczurHAL selects exactly one hardware
backend through a single canonical switch (`src/hal/hal_target.h`). Define one
of the following in `hal_project_config.h` (or via `-D`):

```c
#define HAL_TARGET_RP2040      // Raspberry Pi RP2040/2350
#define HAL_TARGET_STM32G474   // STM32G474
#define HAL_TARGET_MOCK        // host unit-test / simulation backend
```

If you define none, the target is **auto-detected** from the toolchain, so
existing RP2040/Arduino projects need no change. Selecting two targets - or a
bare-metal ARM build with no detectable target - is a compile-time `#error`.
Backend files compile only for their selected target, so unused backends cost zero code.

## Host tests (quick)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Detailed suite coverage, mock behavior notes, and testing workflow are in
[JaszczurHAL_API.md](doc/JaszczurHAL_API.md).

## Continuous integration and quality gates

Every push and pull request to `main` runs the CI workflow
(`.github/workflows/ci.yml`). It builds the library and exercises several layers of checks:

- **Host unit tests** - the suite runs against the deterministic mock backend
  (CMake + Unity).
- **Compile gates** - the RP2040/Arduino-pico static library is built across
  `HAL_ENABLE_*` flag profiles and all examples are compiled, while the
  STM32G474 backend is built with the host compiler to catch backend
  regressions.
- **Memory safety** - the host tests are re-run under Valgrind
  (`ctest -T memcheck`) to catch leaks, use-after-free, and invalid/uninitialised
  reads. This covers the portable logic and the mock backend.
- **Static analysis** - `clang-tidy` and `cppcheck` analyse the project's own
  code. cppcheck parses standalone, so it also reaches the RP2040 backend
  adapters that never run on the host; clang-tidy covers the host-compilable
  production code (portable HAL, shared engine, STM32 backend). Bundled
  third-party libraries are excluded from both.

Tool configuration lives alongside the sources: `.clang-tidy`,
`tests/cppcheck-suppressions.txt`, `tests/valgrind.supp`, and
`scripts/clang_tidy_files.py` (the clang-tidy include/exclude file lists). The
same checks can be run locally:

```bash
# memory safety (requires valgrind)
ctest --test-dir build -T memcheck --output-on-failure

# static analysis (requires clang-tidy, cppcheck, clang-tools)
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cppcheck --enable=warning,performance,portability \
  --suppressions-list=tests/cppcheck-suppressions.txt src
run-clang-tidy -p build
```

You can invoke all quality gates at once, by simply running
```bash
./runalltests.sh
```

## Git hooks (format + commit message)

Repository includes versioned hooks in `.githooks/`: `pre-commit` and `commit-msg` (conventional commits).

In order to make it work, it needs to be installed once per clone:

```bash
./runmefirst.sh
```

`runmefirst.sh` installs required tools (including `clang-format`) and configures:

```bash
git config core.hooksPath .githooks
```

Manual equivalent (without running setup script):

```bash
sudo apt-get install -y clang-format
chmod +x .githooks/pre-commit .githooks/commit-msg
git config core.hooksPath .githooks
```

## VS Code Development Environment

`vscode-templates/` contains ready-to-use VS Code project configurations, and scripts.

But please note: the primary development environment for this library is Linux, preferably a Debian-like distribution. Most scripts and configuration helpers are primarily targeted at that platform.

### Features

- One-key build, upload, and debug workflow
- Raspberry Pi Debug Probe support for RP2040/RP2350, including breakpoints and source-level inspection
- Persistent serial monitor with automatic reconnect after device replug
- Cortex-Debug integration for live sessions
- IntelliSense with full RP2040/RP2350 API support
- Support for multiple Raspberry Pi Pico variants
- UF2 bootloader upload mode
- Board selection with custom clock and optimization settings

### Platform-Specific Templates

- **[Linux/macOS](vscode-templates/linux/)** - Fully functional bash-based build and deployment scripts
  - Lightweight bash implementation
  - Compatible with standard GNU toolchains

- **[Windows](vscode-templates/windows/)** - nearly complete RP2040 development setup with Python-based build orchestration, Arduino CLI integration, debug support, and serial monitoring
  - Python-based build flow for cross-platform consistency
  - VID:PID-aware serial monitor with automatic reconnect
  - Interactive board and build option selector
  - Cortex-Debug integration for live debug sessions

Quick Start:

1. Choose your platform: [Windows](vscode-templates/windows/) or [Linux](vscode-templates/linux/)
2. Copy the template to your project directory
3. Configure environment variables (Arduino CLI path, serial port)
4. Start building with `Ctrl+Shift+1` (or `Ctrl+Shift+2` to upload)

See [vscode-templates/README.md](vscode-templates/README.md) for detailed setup, or visit your platform-specific folder.

## Building as a static library (.a)

The complete guide for compiling JaszczurHAL to a linkable static library
(`libJaszczurHAL.a`): [lib_compilation.md](doc/lib_compilation.md)

## Changing the RP2040 Arduino core version

The pinned version of the `earlephilhower/arduino-pico` core is defined in a single file:

```text
rp2040_core_version.conf    ← RP2040_CORE_VERSION=x.y.z
```

`runmefirst.sh` and the CI workflow source this file automatically;
`runalltests.sh` verifies that the RP2040 core is installed. To upgrade or downgrade:

1. Edit `rp2040_core_version.conf` - change the `RP2040_CORE_VERSION=` line.
2. Run `./runmefirst.sh` to install the new core locally.
3. Run `./runalltests.sh` (or at minimum `./scripts/build_rp2040_lib.sh`) to confirm the library still compiles against the new core.

No other files need to be touched.

## Documentation

Primary docs:

- API reference: [JaszczurHAL_API.md](doc/JaszczurHAL_API.md)
- Changelog: [CHANGELOG.md](doc/CHANGELOG.md)
- Build-time flags summary: [HAL_FLAGS](doc/HAL_FLAGS.txt)
- Linkable static library build guide: [lib_compilation.md](doc/lib_compilation.md)
- STM32G474 backend status: [STM32G474_porting_progress.md](doc/STM32G474_porting_progress.md)
- Architecture roadmap: [future_ideas.md](doc/future_ideas.md)
- VS Code setup (Windows & Linux): [vscode-templates/README.md](vscode-templates/README.md)
  - Windows template: [vscode-templates/windows/README.md](vscode-templates/windows/README.md)
  - Linux template: [vscode-templates/linux/README.md](vscode-templates/linux/README.md)


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
  Limor Fried (Ladyada) for Adafruit Industries (BSD-2-Clause). See the file
  headers for the per-module attribution.
- Bundled, ported, or locally adapted third-party components:
  [cJSON / cJSON_Utils](src/hal/impl/shared/frameworks/cjson/),
  [LodePNG](src/hal/impl/shared/frameworks/lodepng/),
  [JPEGDecoder / picojpeg](src/hal/impl/shared/frameworks/jpeg/),
  [FatFs](src/hal/impl/shared/frameworks/filesystem/ff16/),
  [FreeRTOS-Kernel](third_party/FreeRTOS-Kernel/),
  [PubSubClient](src/hal/impl/rp2040/frameworks/PubSubClient/),
  [arduino-wireguard-pico-w](src/hal/impl/rp2040/frameworks/arduino-wireguard-pico-w/),
  [WireGuard crypto core](src/hal/impl/shared/frameworks/wireguard/crypto/),
  [littlefs](src/hal/impl/stm32g474/drivers/littlefs/),
  [LiquidCrystal / HD44780](src/hal/impl/shared/drivers/hd44780/),
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
