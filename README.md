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
- [Ford-Mondeo-MK-DPF-Tracker](https://github.com/jaszczurtd/Ford-Mondeo-MK-DPF-Tracker)- a device that tracks DPF regeneration cycles;
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

The [examples guide](examples/README.md) lists build commands, assemblies,
and platform limits. Applications use `app_start()`, `app_task0()`, and
optionally `app_task1()`; the [application API](src/hal/core/hal_app.h)
describes their execution on each platform.

<a id="supported-targets-and-modules-quick-overview"></a>

## Supported platforms

RP2040 and RP2350 use Pico SDK, STM32G474 uses the repository's bare-metal
port, and ESP32-S3 uses ESP-IDF. Original ESP32 support is experimental.
The mock backend runs deterministic tests on a development computer.
See the [feature overview](doc/en/features.md) for modules and limitations.

<a id="module-selection-quick"></a>

## Enabling modules

Enable optional modules in the application's `hal_project_config.h`:

```c
#pragma once
#define HAL_ENABLE_WIFI
#define HAL_ENABLE_TIME
#define HAL_ENABLE_GPS
```

The hardware configuration must support the selected features. Invalid flags,
parameters, or dependencies stop the build. See the
[module flags guide](doc/api/en/02_module_flags.md) and
[build-time flags reference](doc/HAL_FLAGS.txt).

<a id="target-selection-example-multiplatform"></a>

## Selecting a target and board

The build tools select the target and board. Projects with `device_tree.json`
select an assembly that describes the board and its connected devices.
In VS Code, use `Ctrl+Shift+Alt+1` to choose one. The
[firmware project guide](doc/en/FwProjectWorkflow.md) explains configuration,
and the [board guide](doc/en/boards_profiles_howto.md) covers profiles and
migration from removed composite boards.

For direct compiler use, define exactly one `HAL_TARGET_*` selector;
otherwise HAL detects it from the toolchain. The options are
`HAL_TARGET_RP2040`, `HAL_TARGET_RP2350_ARM`, `HAL_TARGET_RP2350_RISCV`,
`HAL_TARGET_STM32G474`, `HAL_TARGET_ESP32_S3`, `HAL_TARGET_ESP32`, and
`HAL_TARGET_MOCK`. Only the selected implementation is compiled.

<a id="freertos-opt-in"></a>

## FreeRTOS support

On RP and STM32G474, `HAL_ENABLE_FREERTOS` enables the pinned FreeRTOS kernel;
RP supports SMP and STM32 uses the Cortex-M4F port. ESP-IDF provides and starts
FreeRTOS on the ESP32 family. Applications use the standard FreeRTOS API.
See the [multicore and FreeRTOS guide](doc/api/en/04_multicore_drivers_migration.md)
for task placement, startup, and concurrency rules.

<a id="thread-safety-overview"></a>

## Thread and multicore safety

Hardware modules support concurrent use where their API documents it.
Create, initialize, and release resources on their owner core. Check each
module's [API reference](doc/en/JaszczurHAL_API.md) for locking, ISR limits,
and exceptions. Mock tests are single-threaded; optional FreeRTOS POSIX tests
exercise scheduler behavior on the host.

<a id="building-as-a-static-library-a"></a>

## Building a static library (.a)

The [library build guide](doc/en/lib_compilation.md) covers target-specific
builds and installation. RP and STM32 packages contain `libJaszczurHAL.a`,
matching generated headers, metadata, and a link-signature reference.
Keep these files together. Compiling against an installed package does not
require Python; the target SDK, startup code, and linker setup are still needed.

<a id="tests-and-quality-gates"></a>

## Tests and quality checks

Install dependencies with `./runmefirst.sh` on Linux or `./runmefirst.ps1`
in PowerShell on Windows. Follow the [Windows setup guide](doc/en/windows_setup.md)
for its supported build and debugging workflows.

The complete quality gate runs on Linux:

```bash
./runalltests.sh
```

It runs host tests, sanitizers, Valgrind, static analysis, documentation checks,
and target builds. Linux CI uses the same stages except local-only example
builds; Windows has separate native checks. Before a push,
`./runalltests.sh --commit` checks a clean checkout of the last commit.
Hardware tests require a separate, explicit procedure. See the
[build and test guide](doc/api/en/03_build_tests.md) for requirements and scope.

<a id="security-and-sbom"></a>

## Dependency security and SBOM

See [SECURITY.md](SECURITY.md) for vulnerability reporting and the
[dependency security guide](doc/en/security_supply_chain.md) for checks and
SBOM maintenance. The [component inventory](security/third_party.json) records
managed dependencies and points each pinned one at its `*_version.conf`, which
supplies the version and commit; [sbom.cdx.json](security/sbom.cdx.json) is
generated from project data.

<a id="vs-code-development-environment"></a>

## Working in VS Code

The [VS Code guide](vscode/README.md) covers project generation, build and upload
tasks, serial monitoring, IntelliSense, and keyboard shortcuts. The
[firmware guide](doc/en/FwProjectWorkflow.md#adding-project-source-files)
explains how to add sources. Opening the HAL repository itself provides
[static-library tasks](doc/en/lib_compilation.md#repository-workspace-and-vs-code).
Network updates and their security limits are described in the
[OTA guide](doc/en/OTAWorkflow.md).

## Debugging with VS Code

Generated Cortex-Debug profiles support RP2040/RP2350 Arm through a Debug
Probe or Picoprobe, and STM32G474 through Nucleo's ST-Link. Starting a profile
builds and loads the Debug ELF. See [setup and wiring](doc/en/windows_setup.md).
ESP32-S3 has no generated debug profile; its build, upload, and OTA support
have the limits described in the platform guides.

<a id="managed-dependencies"></a>

## Dependency versions and updates

Pinned versions live in `third_party/*_version.conf`.
`./third_party/update_components.sh` installs those versions;
`--verify-only` checks them without updating. The
[component guide](third_party/README.md) covers updates, the inventory and
SBOM, affected builds, and required validation.

## Documentation

Start with the [documentation index](doc/table_of_contents.md),
[feature overview](doc/en/features.md), or [API reference](doc/en/JaszczurHAL_API.md).
The [scripts guide](doc/api/en/00_scripts.md) describes repository tooling.

## Is this project built with AI?

This is an important question, especially these days. And yes, I do use AI in this project.
For what exactly?

* Documentation - obviously. Without AI, I would not have been able to prepare such detailed documentation on my own, including Doxygen comments.
* Build system - the vast majority of the Python-based build system was written by AI. This is an area I personally do not enjoy, and one I am happy to delegate to agents.
* Unit tests - covering the main paths and usage scenarios. Many of them are based on real issues and regressions I encountered while working on the project.
* Refactoring - AI did a lot of work refactoring sources and adapting my old code, or third-party Arduino code, to the requirements of the JaszczurHAL API, including multithreading support, stability improvements, and similar areas.
* Around 60% of the C/C++ code is still code I wrote manually, not counting the API code that forms the HAL skeleton, which was already created with AI assistance.
* CI configuration, scripts for managing sanitizers, fuzzing, Valgrind, and static analysis.

This is still a massive amount of work for a project of this scale. If someone believes that something like this can be created by simple prompting, without knowledge and without actually looking at the code - I wish them good luck and all the best. :)

And the amount of fighting I had to do with AI just to make the documentation readable at all - that part is mine. ;)

## Notes and credits

- SmartTimers is based on [Nettigo Timers](https://github.com/nettigo/Timers),
  a fork of [garthoff/Timers](https://github.com/garthoff/Timers).
- Tests use the project's Unity fork. The selected version is the Unity pin
  of the [jhbase component](third_party/jhbase_version.conf).
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
  [Unity version through jhbase](third_party/jhbase_version.conf),
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
