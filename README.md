# JaszczurHAL

Author: Marcin 'Jaszczur' Kielesinski

JaszczurHAL is a hardware abstraction layer and utility library for embedded projects.

Today the most complete backend targets RP2040 boards through Arduino-pico.
STM32G474 is available as a real bare-metal backend for core domains and an
expanding set of shared portable drivers; a few modules are still in progress.

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
- Singleton and per-bus locks use atomic create-once fallbacks where defensive lazy creation remains possible
- Mock backend targets deterministic single-threaded tests rather than true concurrent synchronization
- Exact guarantees are documented per module in [JaszczurHAL_API.md](doc/JaszczurHAL_API.md)

For detailed signatures, module contracts, backend notes, and test coverage,
see [JaszczurHAL_API.md](doc/JaszczurHAL_API.md).

## Library structure

```text
CMakeLists.txt              # host/mock tests build
arduino_lib/                # RP2040 Arduino-pico static-library CMake glue
stm32_lib/                  # STM32G474 static-library CMake, toolchain, linker script
scripts/
  build_arduino_lib.sh      # RP2040 static-library helper
  build_stm32_lib.sh        # STM32G474 static-library helper
  ensure_freertos_kernel.sh # pinned FreeRTOS-Kernel fetch/verify helper
runalltests.sh              # full local validation gate
runmefirst.sh               # one-time local toolchain setup
doc/
  JaszczurHAL_API.md        # detailed API/reference
  lib_compilation.md        # static-library build guide
  CHANGELOG.md              # project changelog
  Thread-SafetyAudit.md     # thread-safety audit for FreeRTOS work
  STM32G474_porting_progress.md # STM32G474 backend status
  future_ideas.md           # architecture roadmap and backlog
examples/                   # buildable example apps for RP2040 and STM32G474
src/
  JaszczurHAL.h             # primary public include
  HAL_FLAGS.txt             # HAL_ENABLE_* flag summary
  libConfig.h               # backward-compat include
  tools.h, tools_c.h        # utility aggregators (C++ / C)
  arduino_host_stubs/       # host-build Arduino compatibility headers
  datasheets/               # local reference PDFs and notes
  hal/                      # HAL public headers + common wrappers
    impl/
      .mock/                # deterministic host/test backend
      arduino/              # Arduino/RP2040 backend
        drivers/rp2040/     # RP2040 SoC services (fault/system)
        frameworks/         # Arduino-origin integrations (PubSubClient, WireGuard, SD logger)
      shared/               # target-neutral drivers/engines reused by RP2040 + STM32
        ads1x15/ digipot/ display/ ds18b20/ ds3231/ gps/
        max6675/ mcp2515/ mcp9600/ neopixel/ onewire/
        pcf8563/ pga2311/ wireguard/
      stm32g474/            # STM32G474 backend
        drivers/
          stm32g474/        # STM32G474 SoC services (fault/system)
        freertos/           # STM32 FreeRTOSConfig and hooks
        port/               # startup, SystemInit, linker-facing low-level glue
  utils/                    # SmartTimers, PID, watchdog, tools, cJSON, Unity
tests/                      # host unit tests (CMake + Unity)
third_party/                # optional local dependencies, e.g. FreeRTOS-Kernel
vscode-templates/           # ready-to-use VS Code project configurations
  linux/                    # Linux/macOS template scripts and settings
  windows/                  # Windows template scripts and settings
```

Detailed per-file layout is maintained in
[JaszczurHAL_API.md](doc/JaszczurHAL_API.md) (`## Library structure`).

Folder `src/hal/impl/shared/` is for internal, backend-agnostic implementation
pieces used by at least two hardware backends. Put there only code that:

- depends on HAL contracts,
- has identical behavior across targets,
- can be reused without per-target `#if HAL_TARGET_IS_*` forks in that file.

Shared device/engine code lives in per-driver subfolders, for example:
`shared/ads1x15/`, `shared/digipot/`, `shared/display/`, `shared/pga2311/`, etc.

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
`app_task0`, plus optional `app_task1` when `HAL_ENABLE_APP_TASK1` is defined)
- the same source compiles on both backends. See
[examples/README.md](examples/README.md) for details.

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

- [JaszczurHAL_API.md](doc/JaszczurHAL_API.md)
- `src/HAL_FLAGS.txt`

## FreeRTOS opt-in

FreeRTOS support is staged behind an explicit compile-time flag:

```c
#define HAL_ENABLE_FREERTOS
```

This flag does not introduce a `hal_rtos_*` wrapper API. Applications use native
FreeRTOS headers directly when their target build provides them.

- RP2040 uses arduino-pico's own FreeRTOS mode. `HAL_ENABLE_FREERTOS` requires
  `__FREERTOS`, selected through the Arduino-pico board option
  `Operating System -> FreeRTOS SMP` or an equivalent FQBN option such as
  `os=freertos`. The checked-in RP2040 build helpers now expose this as
  `./scripts/build_arduino_lib.sh --freertos` and
  `cmake -S examples -B build_examples_rp2040_freertos -DJH_EXAMPLE_TARGET=rp2040 -DJH_RP2040_FREERTOS=ON`.
- STM32G474 uses a pinned upstream `FreeRTOS-Kernel` checkout managed by
  `freertos_core_version.conf` and `scripts/ensure_freertos_kernel.sh`. At this
  stage, `HAL_ENABLE_FREERTOS` compiles an explicit Cortex-M4F kernel source
  list, uses the target `FreeRTOSConfig.h`, lets the FreeRTOS port own
  SVC/PendSV/SysTick, and selects FreeRTOS-aware `hal_mutex_*`,
  `hal_delay_ms()`, and `hal_idle()` paths. With `HAL_PROVIDE_APP_ENTRY`, STM32
  FreeRTOS builds run `app_task0()` and optional `app_task1()` as FreeRTOS
  tasks; stack sizes and priorities can be overridden with
  `HAL_FREERTOS_TASK{0,1}_STACK` and `HAL_FREERTOS_TASK{0,1}_PRIORITY`. Use
  `./scripts/build_stm32_lib.sh --freertos` or the `stm32g474-freertos`
  examples preset; both run the helper before CMake needs the kernel sources.

Current FreeRTOS support covers RP2040 and STM32G474 FreeRTOS-aware core
mutex/delay/idle primitives, portable app entry mapping, singleton/per-bus mutex
creation hardening, and the RP2040 I2C-slave callback path. RP2040 still uses
arduino-pico scheduler ownership (`loop()` / optional `loop1()`); STM32 starts
the scheduler from the HAL-provided entry. Hard
`hal_critical_section_*` still masks interrupts for timing-sensitive code; it is
not a scheduler lock. Timer callback context, Arduino-origin wrapper internals,
and documented single-owner modules are summarized in
[Thread-SafetyAudit.md](doc/Thread-SafetyAudit.md).
The full `runalltests.sh` gate also enables a host-side FreeRTOS POSIX scheduler
test so `HAL_ENABLE_FREERTOS` mutex/delay and lazy create-once behavior are
covered in `ctest` without hardware.

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
[`examples/01_blink/`](examples/01_blink/) shows a portable `app.c` that
compiles and runs on both RP2040 and STM32G474. The STM32G474 build exercises
the real bare-metal backend targeting the Nucleo-G474RE.

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
(`libJaszczurHAL.a`): [lib_compilation.md](doc/lib_compilation.md)

## Changing the Arduino RP2040 core version

The pinned version of the `earlephilhower/arduino-pico` core is defined in a
single file:

```text
arduino_core_version.conf   ← RP2040_CORE_VERSION=x.y.z
```

`runmefirst.sh` and the CI workflow source this file automatically;
`runalltests.sh` verifies that the RP2040 core is installed. To upgrade or
downgrade:

1. Edit `arduino_core_version.conf` - change the `RP2040_CORE_VERSION=` line.
2. Run `./runmefirst.sh` to install the new core locally.
3. Run `./runalltests.sh` (or at minimum `./scripts/build_arduino_lib.sh`) to
   confirm the library still compiles against the new core.

No other files need to be touched.

## Documentation

Primary docs:

- API reference: [JaszczurHAL_API.md](doc/JaszczurHAL_API.md)
- Changelog: [CHANGELOG.md](doc/CHANGELOG.md)
- Build-time flags summary: `src/HAL_FLAGS.txt`
- Linkable static library build guide: [lib_compilation.md](doc/lib_compilation.md)
- FreeRTOS thread-safety audit: [Thread-SafetyAudit.md](doc/Thread-SafetyAudit.md)
- STM32G474 backend status: [STM32G474_porting_progress.md](doc/STM32G474_porting_progress.md)
- Architecture roadmap: [future_ideas.md](doc/future_ideas.md)
- VS Code setup (Windows & Linux): [vscode-templates/README.md](vscode-templates/README.md)
  - Windows template: [vscode-templates/windows/README.md](vscode-templates/windows/README.md)
  - Linux template: [vscode-templates/linux/README.md](vscode-templates/linux/README.md)

[JaszczurHAL_API.md](doc/JaszczurHAL_API.md) is the canonical source for
detailed API signatures, module semantics, multicore/thread-safety policy,
driver inventory/licenses, examples, and host-test coverage.

## Notes and credits

- SmartTimers is based on [Nettigo Timers](https://github.com/nettigo/Timers)
  (fork of [garthoff/Timers](https://github.com/garthoff/Timers)).
- Unity test framework sources are bundled in src/:
  [ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity)
- cJSON/cJSON_Utils are bundled and optional via HAL_ENABLE_CJSON:
  [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)
- The shared display stack (`src/hal/impl/shared/display/`) is a portable,
  HAL-based reimplementation. The GFX engine (`jh_gfx.*`) adapts rendering
  algorithms from [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library),
  and the panel drivers (`ili9341_driver.*`, `st77xx_driver.*`,
  `ssd1306_driver.*`) adapt the controller command sequences from the
  corresponding Adafruit ILI9341 / ST7735-ST7789 / SSD1306 libraries by
  Limor Fried (Ladyada) for Adafruit Industries (BSD-2-Clause). See the file
  headers for the per-module attribution.
- WireGuard cryptographic primitives now live in a shared backend under
  `src/hal/impl/shared/wireguard/crypto/` and are reused by both the WireGuard
  integration and `hal_crypto` ChaCha20/Poly1305 helpers.
- Bundled dependency authors (from upstream LICENSE/README files in src/hal/impl/arduino/drivers/ and src/hal/impl/arduino/frameworks/):
- [arduino-wireguard-pico-w](https://github.com/jaszczurtd/arduino-wireguard-pico-w) - Kenta Ida (original WireGuard-ESP32 API), Daniel Hope (upstream WireGuard core), Marcin Kielesiński (RP2040/Pico W port)
- [PubSubClient](https://github.com/knolleary/pubsubclient) - Nick O'Leary
