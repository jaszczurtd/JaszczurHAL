# JaszczurHAL

Author: Marcin 'Jaszczur' Kielesinski

JaszczurHAL is a hardware abstraction layer and utility library for embedded projects.

Today the most complete backend targets RP2040 boards through Arduino-pico (STM32G474 target is in the works), but the long-term goal is to bring more targets.

## Why this exists

Typical embedded projects start as quick written code and later become harder to evolve because hardware access is mixed with business logic.

JaszczurHAL introduces a practical boundary:

- application layer: portable logic,
- HAL layer: one place for hardware/API details,
- mock layer: deterministic host testing.

This reduces lock-in to one runtime and makes migration to other SDKs much easier.

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

## What you get (high-level)

- Hardware abstraction layer for common embedded peripherals and system services
- Consistent, portable APIs that keep hardware details separate from application logic
- Optional modules controlled by compile-time `HAL_ENABLE_*` flags (opt-in)
- Built-in mock backend for deterministic host/unit testing
- Utility toolkit for common embedded patterns (timers, PID, watchdog, helpers)
- Optional connectivity/security/storage stack for connected firmware projects

## Supported modules and drivers (overview)

- Core HAL domains: GPIO, ADC, DAC, PWM, pulse counter (PCNT), timers, system, synchronization, serial I/O
- Peripheral domains: SPI/I2C/UART, CAN, displays, RGB LEDs, thermocouples, digital temperature sensors, RTC, GPS, external ADC, EEPROM, key-value storage, and SD logging
- Connected domains (opt-in): WiFi, NTP/system time, UDP, WireGuard, MQTT, OTA, LittleFS, crypto/auth helpers, cellular modem (SimCom A76xx via AT, including coarse cell-based location)
- Third-party drivers/frameworks are bundled inside the library and compiled only when related modules are enabled

## Thread safety (overview)

- On Arduino backend, runtime HAL calls are generally multicore-safe and internally synchronized
- As a project rule, initialization and teardown (`init/create/destroy/deinit`) should be done from one core
- Mock backend targets deterministic single-threaded tests rather than true concurrent synchronization
- Exact guarantees are documented per module in `JaszczurHAL_API.md`

For detailed signatures, module contracts, backend notes, and test coverage, see `JaszczurHAL_API.md`.

## Library structure

```text
src/
  JaszczurHAL.h            # primary public include
  HAL_FLAGS.txt            # HAL_ENABLE_* flag summary
  libConfig.h              # backward-compat include
  tools.h, tools_c.h       # utility aggregators (C++ / C)
  arduino_host_stubs/      # host-build Arduino compatibility headers
  hal/                     # HAL headers + common wrappers + backend dispatch
    impl/
      shared/              # backend-agnostic internal engine code reused by multiple backends
      arduino/             # Arduino/RP2040 backend
        drivers/           # bundled third-party Arduino drivers (pico compatible)
          rp2040/          # SoC-specific drivers (rp2040_fault, rp2040_system)
        frameworks/        # bundled high-level integrations (WireGuard/MQTT/GPS parser/SD logger, etc)
      .mock/               # deterministic host/test backend
      stm32g474/           # STM32G474 backend (boot/clock/GPIO/UART/DAC/PCNT/fault real; I2C/SPI/ADC/PWM/timer in progress)
        drivers/
          stm32g474/       # SoC-specific drivers (stm32g474_fault, stm32g474_system)
  utils/                   # helper modules and bundled optional utilities
examples/                 # ready-to-run Arduino sketches
tests/                     # host unit tests (CMake + Unity)
vscode-templates/          # ready-to-use VS Code project configurations
  windows/                 # Windows template (Python + Arduino CLI)
  linux/                   # Linux/macOS template (Bash)
```

Detailed per-file layout is maintained in `JaszczurHAL_API.md` (`## Library structure`).

Folder `src/hal/impl/shared/` is for internal, backend-agnostic implementation
pieces used by at least two hardware backends. Put there only code that:

- depends on HAL contracts,
- has identical behavior across targets,
- can be reused without per-target `#if HAL_TARGET_IS_*` forks in that file.

Do not place target-specific register access, pin/peripheral bring-up, ISR glue,
or SDK object ownership in `shared/` - those belong to backend folders.

## Quick start
See [examples](examples/).

## Examples

The `examples/` tree contains small, focused Arduino sketches. Each
example folder is self-contained: it includes its own
`hal_project_config.h` and `.vscode/` build configuration so it can be
opened and compiled directly in VS Code.

Every example folder now has a minimal `.vscode/` task that compiles the
sketch against the local `JaszczurHAL/src` tree, plus a matching
`hal_project_config.h` for module flags.

### Opening an example in VS Code

1. Open the example folder in VS Code: `code examples/01_blink/`
2. Edit `.vscode/settings.json` if needed (set `arduino.fqbn`, `arduino.uploadPort`)
3. Press `Ctrl+Shift+1` to build (or run the task **Build**)

## Module selection (quick)

JaszczurHAL uses an OPT-IN flag model: by default no optional module is
compiled. To enable the modules your project uses, define `HAL_ENABLE_*`
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

- `JaszczurHAL_API.md`
- `src/HAL_FLAGS.txt`

## Target selection (multiplatform)

Separate from the per-module flags, JaszczurHAL selects exactly one hardware
backend through a single canonical switch (`src/hal/hal_target.h`). Define one
of the following in `hal_project_config.h` (or via `-D`):

```c
#define HAL_TARGET_RP2040      // Raspberry Pi RP2040 / arduino-pico
#define HAL_TARGET_STM32G474   // STM32G474 (bare-metal backend)
#define HAL_TARGET_MOCK        // host unit-test / simulation backend
```

If you define none, the target is **auto-detected** from the toolchain, so
existing RP2040/Arduino projects need no change. Selecting two targets - or a
bare-metal ARM build with no detectable target - is a compile-time `#error`.
Backend files compile only for their selected target, so unused backends cost
zero code.

The same demo source builds on both backends from a single example folder:
[`examples/portable_blink/`](examples/portable_blink/) - an RP2040 sketch
(`setup()/loop()`) and a bare-metal STM32G474 entry (`g474/main.c`) share one
portable `blink_app.c`. The STM32G474 build exercises the first real bare-metal
backend (boot + SysTick time + GPIO + USART2 console + Cortex-M fault capture)
on the Nucleo-G474RE.

## Host tests (quick)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Detailed suite coverage, mock behavior notes, and testing workflow are in
`JaszczurHAL_API.md`.

## Continuous integration and quality gates

Every push and pull request to `main` runs the CI workflow
(`.github/workflows/ci.yml`). It builds the library and exercises several
layers of checks:

- **Host unit tests** - the suite runs against the deterministic mock backend
  (CMake + Unity).
- **Compile gates** - the Arduino/RP2040 static library is built across
  `HAL_ENABLE_*` flag profiles and all examples are compiled, while the
  STM32G474 backend is built with the host compiler to catch backend
  regressions.
- **Memory safety** - the host tests are re-run under Valgrind
  (`ctest -T memcheck`) to catch leaks, use-after-free, and invalid/uninitialised
  reads. This covers the portable logic and the mock backend.
- **Static analysis** - `clang-tidy` and `cppcheck` analyse the project's own
  code. cppcheck parses standalone, so it also reaches the Arduino backend
  adapters that never run on the host; clang-tidy covers the host-compilable
  production code (portable HAL, shared engine, STM32 backend). Bundled
  third-party libraries are excluded from both.

Tool configuration lives alongside the sources: `.clang-tidy`,
`tests/cppcheck-suppressions.txt`, and `tests/valgrind.supp`. The same checks
can be run locally:

```bash
# memory safety (requires valgrind)
ctest --test-dir build -T memcheck --output-on-failure

# static analysis (requires clang-tidy, cppcheck, clang-tools)
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cppcheck --enable=warning,performance,portability \
  --suppressions-list=tests/cppcheck-suppressions.txt src
run-clang-tidy -p build
```

## VS Code Development Environment

`vscode-templates/` contains ready-to-use VS Code project configurations for Arduino development on Windows and Linux/macOS:

### Features

- One-key build, upload, and debugging
- Persistent serial monitor (auto-reconnect on device replug)
- Cortex-Debug live debugging with breakpoints
- IntelliSense with full RP2040/RP2350 API
- Support for 6+ Raspberry Pi Pico variants
- UF2 bootloader upload mode
- Board selection with custom clock/optimization settings

### Platform-Specific Templates

- **[Linux/macOS](vscode-templates/linux/)** - Bash-based build and deployment scripts
  - Lightweight bash implementation
  - Compatible with standard GNU toolchains
  - Same feature set as Windows (build, upload, debug, monitor)

- **[Windows](vscode-templates/windows/)** - Complete setup with Python build orchestration, Arduino CLI integration, debugging, and serial monitor
  - Python-based build system for cross-platform consistency
  - Smart serial monitor with auto-reconnection (VID:PID aware)
  - Interactive board/options selector
  - Cortex-Debug integration for live debugging

Quick Start:

1. Choose your platform: [Windows](vscode-templates/windows/) or [Linux](vscode-templates/linux/)
2. Copy the template to your project directory
3. Configure environment variables (Arduino CLI path, serial port)
4. Start building with `Ctrl+Shift+1` (or `Ctrl+Shift+2` to upload)

See [vscode-templates/README.md](vscode-templates/README.md) for detailed setup, or visit your platform-specific folder.

## Building as a static library (.a)

The complete guide for compiling JaszczurHAL to a linkable static library
(`libJaszczurHAL.a`): [lib_compilation.md](lib_compilation.md)

## Documentation

Primary docs:

- API reference: `JaszczurHAL_API.md`
- Changelog: `CHANGELOG.md`
- Build-time flags summary: `src/HAL_FLAGS.txt`
- Linkable static library build guide: [lib_compilation.md](lib_compilation.md)
- VS Code setup (Windows & Linux): `vscode-templates/README.md`
  - Windows template: `vscode-templates/windows/README.md`
  - Linux template: `vscode-templates/linux/README.md`

`JaszczurHAL_API.md` is the canonical source for detailed API signatures,
module semantics, multicore/thread-safety policy, driver inventory/licenses,
examples, and host-test coverage.

## Notes and credits

- SmartTimers is based on [Nettigo Timers](https://github.com/nettigo/Timers)
  (fork of [garthoff/Timers](https://github.com/garthoff/Timers)).
- Unity test framework sources are bundled in src/:
  [ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity)
- cJSON/cJSON_Utils are bundled and optional via HAL_ENABLE_CJSON:
  [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)
- Bundled dependency authors (from upstream LICENSE/README files in src/hal/impl/arduino/drivers/ and src/hal/impl/arduino/frameworks/):
- [ADS1X15](https://github.com/RobTillaart/ADS1X15) - Rob Tillaart
- [Adafruit_BusIO](https://github.com/adafruit/Adafruit_BusIO) - Adafruit Industries
- [Adafruit_GFX_Library](https://github.com/adafruit/Adafruit-GFX-Library) - Limor Fried (Ladyada) for Adafruit Industries
- [Adafruit_ILI9341](https://github.com/adafruit/Adafruit_ILI9341) - Limor Fried (Ladyada) for Adafruit Industries
- [Adafruit_MCP9600](https://github.com/adafruit/Adafruit_MCP9600) - Kevin Townsend and Limor Fried for Adafruit Industries
- [Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) - Phil "Paint Your Dragon" Burgess (with contributions by PJRC and Michael Miller)
- [Adafruit_SSD1306](https://github.com/adafruit/Adafruit_SSD1306) - Limor Fried (Ladyada), with contributions by Michael Gregg and Andrew Canaday
- [Adafruit_ST7735_and_ST7789_Library](https://github.com/adafruit/Adafruit-ST7735-Library) - Limor Fried (Ladyada) for Adafruit Industries
- [Adafruit_Zero_DMA_Library](https://github.com/adafruit/Adafruit_ZeroDMA) - Phil "PaintYourDragon" Burgess for Adafruit Industries (with ASF-derived parts from Atmel Corporation)
- [MAX6675](https://github.com/adafruit/MAX6675-library) - Limor Fried for Adafruit Industries
- [OneWire](https://github.com/PaulStoffregen/OneWire) - Jim Studt (original), maintained by Paul Stoffregen
- [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) - Miles Burton
- [DS3231](https://github.com/NorthernWidget/DS3231) - Eric Ayars, Andrew Wickert, Jean-Claude Wippler, Northern Widget contributors
- [MCP2515](https://github.com/coryjfowler/MCP_CAN_lib) - Loovee / Seeed Technology, with contributions by Cory J. Fowler
- [arduino-wireguard-pico-w](https://github.com/jaszczurtd/arduino-wireguard-pico-w) - Kenta Ida (original WireGuard-ESP32 API), Daniel Hope (upstream WireGuard core), Marcin Kielesiński (RP2040/Pico W port)
- [PubSubClient](https://github.com/knolleary/pubsubclient) - Nick O'Leary
