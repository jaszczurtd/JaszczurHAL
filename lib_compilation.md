# JaszczurHAL Linkable Library Compilation (`.a`)

JaszczurHAL can be compiled into a linkable static library `libJaszczurHAL.a`
for the RP2040 (ARM Cortex-M0+) target using the Arduino toolchain.

> **Note:** This option **does not replace** the standard workflow where
> arduino-cli compiles JaszczurHAL sources together with your project.
> The precompiled `.a` is an additional possibility - useful when you want to
> speed up the compile cycle, use JaszczurHAL in a CMake-based project, or
> link the library manually outside the arduino-cli ecosystem.
> Both approaches (joint compilation and `.a` linking) use the same source
> files and configuration flags.

## Prerequisites

- Arduino RP2040 core installed:
  ```bash
  arduino-cli core install rp2040:rp2040
  ```
- CMake >= 3.16
- Bash (Linux / macOS / WSL)

## Quick Build (Automatic)

If you don't have arduino-cli installed:

```bash
mkdir -p ~/bin

curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=~/bin sh
echo 'export PATH="$HOME/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

arduino-cli version
```

Then add RP2040 index and core:

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install rp2040:rp2040

arduino-cli core list
ls -l ~/.arduino15/packages/rp2040
```

From repository root:

```bash
./build_arduino_lib.sh
```

The script auto-detects the toolchain (`arm-none-eabi-gcc`) and the Arduino
core from the default location `~/.arduino15/packages/rp2040/`. When finished,
the output (`libJaszczurHAL.a`) is placed in `build_arduino/`.

## Script Options

| Option | Default | Description |
|---|---|---|
| `-r`, `--root PATH` | `~/.arduino15/packages/rp2040` | Arduino RP2040 package root |
| `-b`, `--board VARIANT` | `rpipico` | Board variant (e.g. `rpipicow`, `rpipico2`) |
| `-c`, `--chip CHIP` | `rp2040` | Target chip (`rp2040` / `rp2350`) |
| `-p`, `--project-config DIR` | - | Directory containing `hal_project_config.h` |
| `-D KEY=VALUE` | - | Extra compile definitions (repeatable) |
| `-o`, `--output DIR` | `./build_arduino` | Output directory |
| `--clean` | - | Remove build directory before building |
| `-j`, `--jobs N` | `nproc` | Parallel build jobs |

## Examples

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

## Manual Build (CMake)

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
| `EXTRA_HAL_DEFINES` | - | Semicolon-separated list of extra defines |
| `HAL_PROJECT_CONFIG_DIR` | - | Path to directory with `hal_project_config.h` |

## Linking With Your Project

```bash
arm-none-eabi-g++ \
  -march=armv6-m -mcpu=cortex-m0plus -mthumb \
  -I /path/to/JaszczurHAL/src \
  main.cpp \
  -L ./build_arduino -lJaszczurHAL \
  -o firmware.elf
```

## Build File Structure

```text
arduino_lib/
  CMakeLists.txt             # CMake config (target JaszczurHAL STATIC)
  toolchain_rp2040.cmake     # ARM cross-compilation toolchain
build_arduino_lib.sh         # automated build script
build_arduino/               # output directory (gitignored)
  libJaszczurHAL.a           # resulting static library
```
