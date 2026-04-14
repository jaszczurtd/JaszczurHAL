# JaszczurHAL

Author: Marcin 'Jaszczur' Kielesinski

JaszczurHAL is a hardware abstraction layer and utility library for embedded projects.

Today the most complete backend targets RP2040 boards through Arduino-pico, but the long-term 
goal is to bring more targets, like STM32.

## Why this exists

Typical embedded projects start as quick written code and later become harder to evolve
because hardware access is mixed with business logic.

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

The internal header:

```cpp
#include <hal/hal.h>
```

is still available for advanced/internal usage.

## What you get (high-level)

- Portable HAL modules (`hal_gpio_*`, `hal_i2c_*`, `hal_can_*`, `hal_display_*`, ...)
- Optional integrations controlled by `HAL_DISABLE_*` flags
- Bundled third-party drivers (for example display/CAN/GPS stacks), compiled only when related HAL modules are enabled
- Mock backend for deterministic host/unit tests
- Utility modules (`tools`, `SmartTimers`, `pidController`, `multicoreWatchdog`)
- C soft-timer wrapper API with table-based setup/tick helpers (`hal_soft_timer_*`)
- Optional bundled JSON utilities (`HAL_ENABLE_CJSON`)

Full module-by-module API and behavior are documented in `JaszczurHAL_API.md`.

## Library structure

```text
src/
  JaszczurHAL.h            # primary public include
  HAL_FLAGS.txt            # HAL_DISABLE_* / HAL_ENABLE_* summary
  libConfig.h              # backward-compat include
  tools.h, tools_c.h       # utility aggregators (C++ / C)
  arduino_host_stubs/      # host-build Arduino compatibility headers
  hal/                     # HAL headers + common wrappers + backend dispatch
    impl/
      arduino/             # Arduino/RP2040 backend
      .mock/               # deterministic host/test backend
      drivers/             # bundled third-party drivers
  utils/                   # helper modules and bundled optional utilities
tests/                     # host unit tests (CMake + Unity)
vscode-templates/          # ready-to-use VS Code project configurations
  windows/                 # Windows template (Python + Arduino CLI)
  linux/                   # Linux/macOS template (Bash)
```

Detailed per-file layout is maintained in `JaszczurHAL_API.md` (`## Library structure`).

## Quick start

```cpp
#include <JaszczurHAL.h>

void setup() {
    hal_debug_init(115200);
    hal_gpio_set_mode(25, HAL_GPIO_OUTPUT);
}

void loop() {
    hal_gpio_write(25, true);
    hal_delay_ms(200);
    hal_gpio_write(25, false);
    hal_delay_ms(200);
}
```

## Soft timer table quick example

```c
#include <hal/hal_soft_timer.h>
#include <hal/hal_system.h>

static hal_soft_timer_t timerFast = NULL;
static hal_soft_timer_t timerSlow = NULL;

static void onFast(void) {
  // fast periodic work
}

static void onSlow(void) {
  // slow periodic work
}

static const hal_soft_timer_table_entry_t timers[] = {
  { &timerFast, onFast, 50 },
  { &timerSlow, onSlow, 1000 }
};

void setup(void) {
  bool ok = hal_soft_timer_setup_table(timers,
                     COUNTOF(timers),
                     hal_watchdog_feed,
                     2);
  if (!ok) {
    hal_derr("timer table setup failed");
  }
}

void loop(void) {
  (void)hal_soft_timer_tick_table(timers, COUNTOF(timers));
}
```

`hal_soft_timer_setup_table(...)` and `hal_soft_timer_tick_table(...)` return `false` for invalid input (`table == NULL` or `count == 0`) and log via `hal_derr(...)`.

## Module selection (quick)

To exclude optional subsystems, define `HAL_DISABLE_*` flags in a project-local
`hal_project_config.h`:

```c
#pragma once
#define HAL_DISABLE_WIFI
#define HAL_DISABLE_TIME
#define HAL_DISABLE_GPS
```

For the complete flag matrix, dependency propagation rules, and `HAL_ENABLE_*` options,
see:

- `JaszczurHAL_API.md`
- `src/HAL_FLAGS.txt`

## Host tests (quick)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Detailed suite coverage, mock behavior notes, and testing workflow are in
`JaszczurHAL_API.md`.

## VS Code Development Environment

`vscode-templates/` contains ready-to-use VS Code project configurations for Arduino development on Windows and Linux/macOS:

### Platform-Specific Templates

- **[Windows](vscode-templates/windows/)** — Complete setup with Python build orchestration, Arduino CLI integration, debugging, and serial monitor
  - Python-based build system for cross-platform consistency
  - Smart serial monitor with auto-reconnection (VID:PID aware)
  - Interactive board/options selector
  - Cortex-Debug integration for live debugging

- **[Linux/macOS](vscode-templates/linux/)** — Bash-based build and deployment scripts
  - Lightweight bash implementation
  - Compatible with standard GNU toolchains
  - Same feature set as Windows (build, upload, debug, monitor)

### Quick Start

1. Choose your platform: [Windows](vscode-templates/windows/) or [Linux](vscode-templates/linux/)
2. Copy the template to your project directory
3. Configure environment variables (Arduino CLI path, serial port)
4. Start building with `Ctrl+Shift+1` (or `Ctrl+Shift+2` to upload)

See [vscode-templates/README.md](vscode-templates/README.md) for detailed setup, or visit your platform-specific folder.

### Features

- ✅ One-key build, upload, and debugging
- ✅ Persistent serial monitor (auto-reconnect on device replug)
- ✅ Cortex-Debug live debugging with breakpoints
- ✅ IntelliSense with full RP2040/RP2350 API
- ✅ Support for 6+ Raspberry Pi Pico variants
- ✅ UF2 bootloader upload mode
- ✅ Board selection with custom clock/optimization settings

## Building as a static library (.a)

JaszczurHAL can be compiled into a linkable static library `libJaszczurHAL.a`
for the RP2040 (ARM Cortex-M0+) target using the Arduino toolchain.

> **Note:** This option **does not replace** the standard workflow where
> arduino-cli compiles JaszczurHAL sources together with your project.
> The precompiled `.a` is an additional possibility — useful when you want to
> speed up the compile cycle, use JaszczurHAL in a CMake-based project, or
> link the library manually outside the arduino-cli ecosystem.
> Both approaches (joint compilation and `.a` linking) use the same source
> files and configuration flags.

### Prerequisites

- Arduino RP2040 core installed:
  ```bash
  arduino-cli core install rp2040:rp2040
  ```
- CMake >= 3.16
- Bash (Linux / macOS / WSL)

### Quick build (automatic)

From the repository root:

```bash
./build_arduino_lib.sh
```

The script auto-detects the toolchain (`arm-none-eabi-gcc`) and the Arduino
core from the default location `~/.arduino15/packages/rp2040/`. When finished,
the output (`libJaszczurHAL.a`) is placed in `build_arduino/`.

### Script options

| Option | Default | Description |
|---|---|---|
| `-r`, `--root PATH` | `~/.arduino15/packages/rp2040` | Arduino RP2040 package root |
| `-b`, `--board VARIANT` | `rpipico` | Board variant (e.g. `rpipicow`, `rpipico2`) |
| `-c`, `--chip CHIP` | `rp2040` | Target chip (`rp2040` / `rp2350`) |
| `-p`, `--project-config DIR` | — | Directory containing `hal_project_config.h` |
| `-D KEY=VALUE` | — | Extra compile definitions (repeatable) |
| `-o`, `--output DIR` | `./build_arduino` | Output directory |
| `--clean` | — | Remove build directory before building |
| `-j`, `--jobs N` | `nproc` | Parallel build jobs |

### Examples

Default build (Raspberry Pi Pico, ILI9341):
```bash
./build_arduino_lib.sh
```

With a project configuration and disabled modules:
```bash
./build_arduino_lib.sh \
  -p /path/to/project \
  -D HAL_DISABLE_WIFI \
  -D HAL_DISABLE_GPS \
  -D HAL_DISABLE_THERMOCOUPLE
```

For Pico W with an ST7789 display:
```bash
./build_arduino_lib.sh \
  --board rpipicow \
  -D PICO_W \
  -D HAL_DISPLAY_ST7789
```

Clean rebuild with an explicit output directory:
```bash
./build_arduino_lib.sh --clean -o ./my_build
```

### Manual build (CMake)

For full control over the build process:

```bash
mkdir build_arduino && cd build_arduino

cmake \
  -DCMAKE_TOOLCHAIN_FILE=../arduino_lib/toolchain_rp2040.cmake \
  -DARDUINO_ROOT=~/.arduino15/packages/rp2040 \
  -DARDUINO_CHIP=rp2040 \
  -DARDUINO_VARIANT=rpipico \
  ../arduino_lib

cmake --build . -j$(nproc)
```

Available CMake configuration variables:

| Variable | Default | Description |
|---|---|---|
| `ARDUINO_ROOT` | `~/.arduino15/packages/rp2040` | Arduino package directory |
| `ARDUINO_CHIP` | `rp2040` | Target chip |
| `ARDUINO_VARIANT` | `rpipico` | Board variant |
| `BOARD_NAME` | `RASPBERRY_PI_PICO` | Arduino board macro name |
| `ARDUINO_F_CPU` | `125000000` | CPU frequency (Hz) |
| `HAL_DISPLAY_DRIVER` | `HAL_DISPLAY_ILI9341` | TFT display driver |
| `EXTRA_HAL_DEFINES` | — | Semicolon-separated list of extra defines |
| `HAL_PROJECT_CONFIG_DIR` | — | Path to directory with `hal_project_config.h` |

### Linking with your project

```bash
arm-none-eabi-g++ \
  -march=armv6-m -mcpu=cortex-m0plus -mthumb \
  -I /path/to/JaszczurHAL/src \
  main.cpp \
  -L ./build_arduino -lJaszczurHAL \
  -o firmware.elf
```

### Build file structure

```text
arduino_lib/
  CMakeLists.txt             # CMake config (target JaszczurHAL STATIC)
  toolchain_rp2040.cmake     # ARM cross-compilation toolchain
build_arduino_lib.sh         # automated build script
build_arduino/               # output directory (gitignored)
  libJaszczurHAL.a           # resulting static library
```

## Documentation

Primary docs:

- API reference: `JaszczurHAL_API.md`
- Changelog: `CHANGELOG.md`
- Build-time flags summary: `src/HAL_FLAGS.txt`
- VS Code setup (Windows & Linux): `vscode-templates/README.md`
  - Windows template: `vscode-templates/windows/README.md`
  - Linux template: `vscode-templates/linux/README.md`

`JaszczurHAL_API.md` is the canonical source for detailed API signatures,
module semantics, multicore/thread-safety policy, driver inventory/licenses,
examples, and host-test coverage.

## Notes and credits

- `SmartTimers` is based on Nettigo Timers: https://github.com/nettigo/Timers
  (fork of https://github.com/garthoff/Timers)
- Unity test framework sources are bundled in `src/`:
  https://github.com/ThrowTheSwitch/Unity
- `cJSON`/`cJSON_Utils` are bundled and optional via `HAL_ENABLE_CJSON`:
  https://github.com/DaveGamble/cJSON
- Bundled driver authors (from upstream LICENSE/README files in `src/hal/impl/arduino/drivers/`):
- `ADS1X15` - Rob Tillaart
- `Adafruit_BusIO` - Adafruit Industries
- `Adafruit_GFX_Library` - Limor Fried (Ladyada) for Adafruit Industries
- `Adafruit_ILI9341` - Limor Fried (Ladyada) for Adafruit Industries
- `Adafruit_MCP9600` - Kevin Townsend and Limor Fried for Adafruit Industries
- `Adafruit_NeoPixel` - Phil "Paint Your Dragon" Burgess (with contributions by PJRC and Michael Miller)
- `Adafruit_SSD1306` - Limor Fried (Ladyada), with contributions by Michael Gregg and Andrew Canaday
- `Adafruit_ST7735_and_ST7789_Library` - Limor Fried (Ladyada) for Adafruit Industries
- `Adafruit_Zero_DMA_Library` - Phil "PaintYourDragon" Burgess for Adafruit Industries (with ASF-derived parts from Atmel Corporation)
- `MAX6675` - Limor Fried for Adafruit Industries
- `MCP2515` - Loovee / Seeed Technology, with contributions by Cory J. Fowler
- `TinyGPSPlus` - Mikal Hart
