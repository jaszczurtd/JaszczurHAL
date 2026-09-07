# JaszczurHAL

*Also available in [Polish](README.pl.md).*

Author: Marcin 'Jaszczur' Kielesinski

JaszczurHAL is a hardware abstraction layer (HAL) and utility library for
embedded systems based on RP2040, RP2350, STM32, and ESP32.
The [feature overview](doc/en/features.md) introduces the available modules
and functions. For the full documentation, see the
[English documentation index](doc/table_of_contents.md).

## How do you even pronounce this library name?

Like this: **"YASH-choor-HAL"**. You're welcome. :)

<a id="why-this-exists"></a>

## Why does this exist?

A valid question - Arduino is already out there, and in a way it allows you to write
and compile the same code quickly and easily for many platforms.
But without going too deep into the details: the more advanced a project becomes,
the more noticeable the limitations of Arduino are - despite the fact that it is
still a valuable platform for learning.
Also, Arduino libraries are very uneven. Some contain weak code that overuses `delay()`,
while others are genuinely impressive pieces of functionality. However, most of
them tend to share one fundamental problem: multithreading.
Then again, there is Zephyr - a professional framework, well recognized in the
industry, supporting a huge range of hardware and offering multithreading as a
standard feature. The problem is that it does not support my favorite MCU family,
RP2040/RP2350, particularly well.
There are certainly other platforms and environments too. A lifetime would probably
not be enough to properly evaluate all of them... But it is certainly ;) enough time
to reinvent my own wheel, in a sense, and build everything the way I want it -
while maintaining high quality standards, including rigorous test gates,
hardware-based testing, and many other things you can read about further in
the project documentation. :)

Among the many features provided by JaszczurHAL, the following are worth noting:

- FreeRTOS support (V11.3.0);
- a consistent API for portable application code;
- a mock implementation for deterministic tests on a development computer;
- drivers shared across supported platforms, with safeguards for
  multithreaded use;
- optional modules enabled through `HAL_ENABLE_*` compile-time flags;
- connectivity, security, and storage modules for applications such as
  network-connected devices;
- tools for common tasks: timers, a PID controller, a watchdog, and utility
  functions.

It is obvious that it is not possible to erase all differences between all MCUs
and create a fully universal API.
However, it is possible to hide the vast majority of those differences and build
a stable, solid foundation that makes many of them much less relevant.
Even in the age of AI translating code between platforms, this still matters -
because tokens are worth saving too. ;)

The project is already useful in practice, though some areas are still a
work in progress (WIP). This is a hobby project, so development has to fit
around earning a living - and having a life. :)

<a id="is-this-used-by-anything-real"></a>

## Is it used in real projects?

Yes - in several of my more demanding projects.

A good example is [Fiesta](https://github.com/jaszczurtd/Fiesta), my personal
car retrofit project. It consists of several closely integrated modules.
The ECU is probably the most demanding: it uses JaszczurHAL on two cores to
control a VP37 injection pump, communicate with the rest of the system over
CAN, handle OBD diagnostics, and perform other low-level tasks.

There are also smaller, but still nontrivial, projects:

- [doomConsole](https://github.com/jaszczurtd/doomConsole) - a Doom port with
  sound, a TFT display, and support for an 8BitDo Bluetooth gamepad;
- [Ford-Mondeo-MK-DPF-Tracker](https://github.com/jaszczurtd/Ford-Mondeo-MK-DPF-Tracker)
  - a device that tracks DPF regeneration cycles;
- [lights-timer](https://github.com/jaszczurtd/lights-timer) - aquarium
  lighting controlled through an Android app.

## Quick start

To explore the [HAL API](doc/en/JaszczurHAL_API.md) and see how the same code
runs on different platforms, start with the
[examples](examples/README.md).

To create your own project for VS Code, use the project generator:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output your-example-project-name
```

The generated project defaults to `rp2040/pico`. Select a different target
and board with `--target` and `--board`, or use the `Project: Select board`
task in VS Code. The project includes ready-to-use tasks for building,
uploading firmware, and monitoring the serial port.

For generator options, the first upload to a new board, and the full task
reference, see the [VS Code guide](vscode/README.md).

## Examples

The `examples/` directory contains applications that show how to use related
HAL modules together. Each example includes a portable `app.c` or `app.cpp`
and a `hal_project_config.h` configuration file.

All examples use the same application functions: `app_start()`, `app_task0()`,
and the optional `app_task1()`, enabled by `HAL_ENABLE_APP_TASK1`.
How these functions run depends on the platform:

- On RP, `app_task1()` runs on the second core.
- On bare-metal STM32G474, `app_task1()` is called cooperatively rather than
  running on a separate core.
- On ESP32-S3, the application functions run under the FreeRTOS scheduler
  already started by ESP-IDF. Tasks 0 and 1 default to cores 0 and 1,
  respectively; this assignment can be changed.

Requirements, build matrices, and the targets supported by each example are
listed in the [examples guide](examples/README.md). It also explains the
rule of extending an existing project or variant before adding another
example directory.

<a id="supported-targets-and-modules-quick-overview"></a>

## Supported platforms

RP2040 and RP2350 use the official Pico SDK. For STM32G474, the repository
provides a bare-metal implementation and linker configuration. ESP32-S3
uses ESP-IDF.

FreeRTOS is optional on RP and STM32G474, and required by ESP-IDF on ESP32-S3.
The mock implementation provides deterministic tests on a development
computer without the target hardware.

For the available modules and functions, see the
[feature overview](doc/en/features.md).

<a id="module-selection-quick"></a>

## Enabling modules

Optional modules are not compiled by default. Enable only the modules your
application needs by defining the corresponding `HAL_ENABLE_*` flags in
its `hal_project_config.h`:

```c
#pragma once
#define HAL_ENABLE_WIFI
#define HAL_ENABLE_TIME
#define HAL_ENABLE_GPS
```

Configuration checks detect invalid or unsupported flags and incorrect
parameter values.

For available flags, their parameters, and dependency rules, see:

- the [API reference](doc/en/JaszczurHAL_API.md);
- the [module flags guide](doc/api/en/02_module_flags.md);
- the [build-time flags reference](doc/HAL_FLAGS.txt).

<a id="target-selection-example-multiplatform"></a>

## Selecting a target and board

Independently of module selection, JaszczurHAL selects one target
implementation through `src/hal/core/hal_target.h`. Define **exactly one**
of the following flags in `hal_project_config.h`, or pass it to the compiler
with `-D`. This block lists the available options; it is not a configuration
to copy in full:

```c
#define HAL_TARGET_RP2040        // RP2040, Cortex-M0+
#define HAL_TARGET_RP2350_ARM    // RP2350, Cortex-M33
#define HAL_TARGET_RP2350_RISCV  // RP2350, Hazard3 RISC-V
#define HAL_TARGET_STM32G474     // STM32G474
#define HAL_TARGET_ESP32_S3      // ESP32-S3, native ESP-IDF
#define HAL_TARGET_MOCK          // deterministic host test backend
```

If no flag is defined, the target is **detected automatically** from the
toolchain. Only the selected target implementation is compiled, so unused
implementations do not increase the program size.

The standard build scripts use the generated board registry and stable
target and board identifiers. See the
[target and board profiles guide](doc/en/boards_profiles_howto.md) for
selection rules. The [firmware project guide](doc/en/FwProjectWorkflow.md)
explains how targets, boards, and project configuration fit together.

For everyday work in VS Code, you do not need to know how this works
internally. Press `Ctrl+Shift+Alt+1` and select a target from the menu.
For other shortcuts, see the
[keyboard shortcut reference](vscode/README.md#vs-code-keyboard-shortcuts).

<a id="freertos-opt-in"></a>

## FreeRTOS support

On RP and STM32G474, enable FreeRTOS with this compile-time flag:

```c
#define HAL_ENABLE_FREERTOS
```

On ESP32-S3, FreeRTOS is required by ESP-IDF. The target configuration adds
`HAL_ENABLE_FREERTOS` as a required flag.

Applications use the standard FreeRTOS headers and API directly on all
supported platforms. JaszczurHAL handles scheduler startup where needed,
along with optional task-to-core assignment.

RP uses the repository's pinned FreeRTOS-Kernel version with SMP support
for multicore execution. STM32G474 uses the same kernel with the Cortex-M4F
port. ESP32-S3 uses the FreeRTOS version supplied by the pinned ESP-IDF
release and supports the optional second application task.

For kernel versions, ports, and build variants, see the
[library build guide](doc/en/lib_compilation.md) and the
[multicore and FreeRTOS guide](doc/api/en/04_multicore_drivers_migration.md).

<a id="thread-safety-overview"></a>

## Thread and multicore safety

JaszczurHAL is designed for use with multiple threads and cores.
Initialize and release resources (`init` / `create` / `destroy` / `deinit`)
on the same core on which they were created. Mutexes for shared module
instances and individual buses are created atomically on first use, with
safeguards against races during creation.

The mock implementation is intended for deterministic, single-threaded
tests. The optional `JH_ENABLE_FREERTOS_POSIX_TESTS` flag adds tests of the
FreeRTOS scheduler on the development computer.

For exact concurrency guarantees, function signatures, module behavior,
implementation differences, and test coverage, see the
[API reference](doc/en/JaszczurHAL_API.md).

<a id="building-as-a-static-library-a"></a>

## Building a static library (.a)

JaszczurHAL can be built as the static library `libJaszczurHAL.a`.
The [library build guide](doc/en/lib_compilation.md) covers this process,
example application builds, and the separation between library code and
application startup code.

Installed RP and STM32G474 packages include generated feature and board
configuration headers, metadata for the selected board, and the source file
needed for link-compatibility checks. These files allow applications to use
the library directly through compiler commands. Compiling and linking
against an already installed package does not require Python.

<a id="tests-and-quality-gates"></a>

## Tests and quality checks

To run the repository's automated tests and checks, use:

```bash
./runalltests.sh
```

The script runs host unit tests, including FreeRTOS POSIX tests, Clang
ASan/UBSan/libFuzzer checks, Valgrind memcheck, and static analysis. It also
checks for duplicate code and documentation issues, and builds the library
and firmware across a matrix of supported target configurations.

This command requires the toolchain and compilers to already be installed - standard
packages available for both Linux and Windows, which can also be downloaded manually.

JaszczurHAL already provides a ready-to-use script that downloads and installs
all required dependencies, with versions available for both Windows and Linux:

```bash
./runmefirst.sh
```

windows:

```bash
runmefirst.ps1
```

Tests on physical hardware are run separately, following the instructions
for each test setup.

For requirements, configuration, test organization, extension rules,
hardware procedures, and recorded results, see the
[build and test guide](doc/api/en/03_build_tests.md). The
[repository scripts guide](doc/api/en/00_scripts.md) explains how the test
and quality-check scripts work.

<a id="security-and-sbom"></a>

## Dependency security and SBOM

The project records the third-party components it uses and their pinned
versions. It also documents how to report vulnerabilities, check dependencies,
and generate a software bill of materials (SBOM):

- [Vulnerability reporting](SECURITY.md) - reporting, assessment, and project
  maintenance policies.
- [Dependency and tool security](doc/en/security_supply_chain.md) - SBOM
  generation, vulnerability checks, and the CI `security-scan` policy.
- [Third-party component inventory](security/third_party.json) - a manually
  maintained list.
- [CycloneDX SBOM](security/sbom.cdx.json) - a generated inventory based on
  project data.

<a id="vs-code-development-environment"></a>

## Working in VS Code

The tools in `vscode/` let you build and upload firmware and use a serial
monitor from VS Code. Projects call them through a stable command interface:

```text
libraries/JaszczurHAL/vscode/entry/jh-vscode
libraries/JaszczurHAL/vscode/entry/jh-vscode.cmd
```

The tool reads project configuration and selects the active target and board.
For CMake projects, it selects the appropriate build procedure for that
target. It checks device identity before serial uploads and also supports
RP2040 BOOTSEL uploads using UF2 files. STM32 flashing uses OpenOCD;
ESP32-S3 builds and uploads use an ESP-IDF script.

The serial monitor remains active until stopped. IntelliSense is refreshed
from the active toolchain's compilation database.

Detailed guides:

- [VS Code setup](vscode/README.md) - CLI commands, task names, keyboard
  shortcuts, and the project generator.
- [Firmware projects](doc/en/FwProjectWorkflow.md) - project structure and
  configuration, including
  [adding source files](doc/en/FwProjectWorkflow.md#adding-project-source-files).
- [OTA updates](doc/en/OTAWorkflow.md) - network updates on RP and ESP32-S3,
  the first firmware upload, and security capabilities and limitations.
- [Full keyboard shortcut reference](vscode/README.md#vs-code-keyboard-shortcuts).

Opening the JaszczurHAL repository root in VS Code provides a separate set
of static-library tasks through the tracked `.vscode/` configuration.
Global shortcuts build and install the library, remove build files, and
refresh IntelliSense for the target and board profile selected from the
shared registry. Build files are stored under `.build/vscode/library/`.
For details, see the
[library build guide](doc/en/lib_compilation.md#repository-workspace-and-vs-code).

## Debugging with VS Code

Generated Cortex-Debug profiles support RP2040 and RP2350 Arm debugging
over SWD. This requires a Raspberry Pi Debug Probe or a Pico running
Debug Probe/Picoprobe firmware. STM32G474 uses the ST-Link built into the
NUCLEO-G474RE board.

Starting a profile from Run and Debug builds and loads the Debug ELF.
The project tools select OpenOCD and an Arm-capable GDB on Windows and
Linux. For probe connections and configuration, see the
[Windows setup guide](doc/en/windows_setup.md).

Firmware development in VS Code is available on both Linux and native
Windows. Build, upload, monitoring, OTA, and debugging capabilities vary
by RP or STM target; refer to the documentation for each target's scope.

ESP32-S3 supports builds, serial uploads, monitoring, IntelliSense, and OTA
updates using the raw application image. It does not have yet a debug profile
provided by the project tools. OTA hardware validation (Phase 3.5) is also
not yet complete; see the [OTA guide](doc/en/OTAWorkflow.md) for the validation
scope.

The complete repository test and quality-check suite runs on Linux. It
includes Valgrind, static analysis, and host integration tests that require
POSIX. For setup, verification, and features available only on Linux, see
the [Windows setup guide](doc/en/windows_setup.md).

<a id="managed-dependencies"></a>

## Dependency versions and updates

The `third_party/*_version.conf` files specify the versions of Pico SDK,
ESP-IDF, picotool, PMD CPD, the RP2350 RISC-V toolchain, FreeRTOS, BearSSL,
cJSON, LodePNG, TJpgDec, FatFs, Unity, lwIP, littlefs, BTstack, and the
Semtech SX126x driver.

Use these commands to update or verify the components:

```bash
./third_party/update_components.sh
./third_party/update_components.sh --verify-only
```

The first command updates components to the versions pinned by the
repository. The second checks them without updating them.

The [third-party component guide](third_party/README.md) explains dependency
management and provides an update checklist: refresh the component inventory
and SBOM, check affected builds, and run the full test and quality-check
suite.

## Documentation

Key references:

- [Complete documentation index](doc/table_of_contents.md).
- [Feature overview](doc/en/features.md).
- [Repository scripts](doc/api/en/00_scripts.md).
- [API reference](doc/en/JaszczurHAL_API.md).
- [Firmware projects](doc/en/FwProjectWorkflow.md).
- [OTA updates](doc/en/OTAWorkflow.md).
- [Target and board profiles](doc/en/boards_profiles_howto.md).
- [Build-time flags reference](doc/HAL_FLAGS.txt).
- [Static library builds](doc/en/lib_compilation.md).
- [Firmware development in VS Code](vscode/README.md).

## Notes and credits

- SmartTimers is based on [Nettigo Timers](https://github.com/nettigo/Timers),
  a fork of [garthoff/Timers](https://github.com/garthoff/Timers).
- Tests use the project's Unity fork. The selected version is recorded in
  the [Unity version file](third_party/unity_version.conf).
- The shared display implementation (`src/hal/display/drivers/`) was
  rewritten as portable code using HAL. The GFX module (`jh_gfx.*`) adapts
  drawing algorithms from the
  [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library).
  The `ili9341_driver.*`, `st77xx_driver.*`, and `ssd1306_driver.*` drivers
  adapt controller command sequences from the Adafruit ILI9341,
  ST7735-ST7789, and SSD1306 libraries by Limor Fried (Ladyada) for Adafruit
  Industries (BSD-2-Clause). The SSD16xx and UC81xx e-paper protocol handling
  and state machines are based on Zephyr driver logic (Apache-2.0-Clause).
  See the file headers for each module's code origins and attribution.
- Third-party components bundled with the project, ported to supported
  platforms, or adapted locally:
  [cJSON version](third_party/cjson_version.conf),
  [LodePNG version](third_party/lodepng_version.conf),
  [TJpgDec version](third_party/jpeg_version.conf),
  [FatFs version](third_party/fatfs_version.conf),
  [Unity version](third_party/unity_version.conf),
  [FreeRTOS-Kernel version](third_party/freertos_core_version.conf),
  [BearSSL version](third_party/bearssl_version.conf),
  [lwIP version](third_party/lwip_version.conf),
  [littlefs version](third_party/littlefs_version.conf),
  [Semtech SX126x driver version](third_party/sx126x_driver_version.conf),
  [PubSubClient](src/hal/network/mqtt/PubSubClient/),
  [shared WireGuard/lwIP implementation](src/hal/network/wireguard/core/),
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
