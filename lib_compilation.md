# JaszczurHAL Library Compilation

JaszczurHAL can be built in three different ways, depending on the target:

| Target | Build entry | Output | Backend switch |
|---|---|---|---|
| Host / mock tests | repository-root CMake | `build/libhal_mock.a` + tests | `HAL_TARGET_MOCK` |
| RP2040 / RP2350 through Arduino-pico | `arduino_lib/` or `build_arduino_lib.sh` | `build_arduino/libJaszczurHAL.a` | `HAL_TARGET_RP2040` |
| STM32G474 bare-metal | `stm32_lib/` | `build_stm32/libJaszczurHAL.a` | `HAL_TARGET_STM32G474` |

The canonical target selection lives in `src/hal/hal_target.h`. Define exactly
one of:

```c
#define HAL_TARGET_RP2040
#define HAL_TARGET_STM32G474
#define HAL_TARGET_MOCK
```

If none is defined, the library auto-detects the target from the toolchain:

- arduino-pico / RP2040 SDK -> `HAL_TARGET_RP2040`
- `STM32G474xx` / explicit STM32 build -> `HAL_TARGET_STM32G474`
- host compiler -> `HAL_TARGET_MOCK`

Selecting two targets at once is a compile-time error.

> **Note:** Static-library builds do not replace the normal workflow where
> arduino-cli or a firmware project compiles JaszczurHAL sources together with
> application code. They are useful for faster rebuilds, CMake-based projects,
> host tests, or manual linking.

---

## Host / Mock Build

The repository-root CMake project builds the deterministic mock backend and the
Unity test executables.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Output:

```text
build/
  libhal_mock.a
  tests/test_*
```

This target is for host-side tests and simulation. It does not require Arduino,
STM32 headers, or a cross compiler.

---

## RP2040 / RP2350 Arduino Static Library

The Arduino build compiles JaszczurHAL into `libJaszczurHAL.a` using the
earlephilhower Arduino-pico toolchain.

### Prerequisites

- Arduino RP2040 core installed:

  ```bash
  arduino-cli config init
  arduino-cli config add board_manager.additional_urls \
    https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
  arduino-cli core update-index
  arduino-cli core install rp2040:rp2040
  ```

- CMake >= 3.16
- Bash (Linux / macOS / WSL)

### Automatic Build

From the repository root:

```bash
./build_arduino_lib.sh
```

The script auto-detects the latest Arduino-pico core and `pqt-gcc` toolchain
under `~/.arduino15/packages/rp2040/`. The output is:

```text
build_arduino/libJaszczurHAL.a
```

Script options:

| Option | Default | Description |
|---|---|---|
| `-r`, `--root PATH` | `~/.arduino15/packages/rp2040` | Arduino RP2040 package root |
| `-b`, `--board VARIANT` | `rpipico` | Board variant, e.g. `rpipicow`, `rpipico2` |
| `-c`, `--chip CHIP` | `rp2040` | Target chip: `rp2040` / `rp2350` |
| `-p`, `--project-config DIR` | - | Directory containing `hal_project_config.h` |
| `-D DEFINE` | - | Extra compile definition, e.g. `HAL_ENABLE_WIFI` or `KEY=VALUE`; repeatable |
| `-o`, `--output DIR` | `./build_arduino` | Output directory |
| `--clean` | - | Remove build directory before building |
| `-j`, `--jobs N` | `nproc` | Parallel build jobs |

Examples:

```bash
# Default Raspberry Pi Pico build
./build_arduino_lib.sh

# Project-local config plus explicit modules
./build_arduino_lib.sh \
  -p /path/to/project \
  -D HAL_ENABLE_WIFI \
  -D HAL_ENABLE_GPS \
  -D HAL_ENABLE_MCP9600

# Pico W
./build_arduino_lib.sh \
  --board rpipicow \
  -D HAL_ENABLE_WIFI

# Clean rebuild into a custom directory
./build_arduino_lib.sh --clean -o ./my_build
```

### Manual CMake Build

```bash
cmake -S arduino_lib -B build_arduino \
  -DCMAKE_TOOLCHAIN_FILE=arduino_lib/toolchain_rp2040.cmake \
  -DARDUINO_ROOT=~/.arduino15/packages/rp2040 \
  -DARDUINO_CHIP=rp2040 \
  -DARDUINO_VARIANT=rpipico

cmake --build build_arduino -j$(nproc)
```

Useful CMake cache variables:

| Variable | Default | Description |
|---|---|---|
| `ARDUINO_ROOT` | `~/.arduino15/packages/rp2040` | Arduino package directory |
| `ARDUINO_CHIP` | `rp2040` | Target chip |
| `ARDUINO_VARIANT` | `rpipico` | Board variant directory |
| `BOARD_NAME` | `RASPBERRY_PI_PICO` | Arduino board macro name |
| `ARDUINO_F_CPU` | `125000000` | CPU frequency in Hz |
| `HAL_DISPLAY_DRIVER` | `HAL_DISPLAY_ILI9341` | TFT display driver macro |
| `EXTRA_HAL_DEFINES` | - | Semicolon-separated extra definitions |
| `HAL_PROJECT_CONFIG_DIR` | - | Directory with `hal_project_config.h` |

### Linking With an RP2040 Project

```bash
arm-none-eabi-g++ \
  -march=armv6-m -mcpu=cortex-m0plus -mthumb \
  -I /path/to/JaszczurHAL/src \
  main.cpp \
  -L ./build_arduino -lJaszczurHAL \
  -o firmware.elf
```

Real firmware projects usually also need the same Arduino core, variant,
platform libraries, startup/runtime objects, and linker flags used by
arduino-pico. For sketches, the simpler route remains `arduino-cli compile`.

---

## STM32G474 Static Library

The STM32 build compiles the bare-metal STM32G474 backend into
`libJaszczurHAL.a`.

### Prerequisites

- CMake >= 3.16
- GNU Arm Embedded toolchain (`arm-none-eabi-gcc`, `arm-none-eabi-g++`,
  `arm-none-eabi-ar`, `arm-none-eabi-ranlib`)

On Debian / Linux Mint:

```bash
sudo apt install cmake gcc-arm-none-eabi binutils-arm-none-eabi
```

### Manual CMake Build

```bash
cmake -S stm32_lib -B build_stm32 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32_lib/toolchain_stm32g474.cmake"

cmake --build build_stm32 -j$(nproc)
```

Output:

```text
build_stm32/libJaszczurHAL.a
```

Optional cache variables:

| Variable | Default | Description |
|---|---|---|
| `ARM_GCC_PREFIX` | `arm-none-eabi` | GNU Arm toolchain prefix |
| `STM32_CPU` | `cortex-m4` | CPU passed to `-mcpu=` |
| `STM32_FPU` | `fpv4-sp-d16` | FPU passed to `-mfpu=` |
| `STM32_FLOAT_ABI` | `hard` | Float ABI |
| `EXTRA_HAL_DEFINES` | - | Semicolon-separated extra HAL definitions |
| `HAL_PROJECT_CONFIG_DIR` | - | Directory with `hal_project_config.h` |

Example with extra modules:

```bash
cmake -S stm32_lib -B build_stm32 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32_lib/toolchain_stm32g474.cmake" \
  -DEXTRA_HAL_DEFINES="HAL_ENABLE_CAN"

cmake --build build_stm32 -j$(nproc)
```

The initial STM32 profile currently enables the backend pieces that exist in
`stm32_lib/CMakeLists.txt`, including I2C, SPI, UART, DAC, PCNT, digipot
backends, MCP9600/MAX6675 thermocouple backends, ADS1115 external ADC,
OneWire/DS18B20, and GPS over UART. Additional modules should be enabled only
once their STM32G474 backend exists.

### Linking With an STM32G474 Project

```bash
arm-none-eabi-g++ \
  -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
  -I /path/to/JaszczurHAL/src \
  main.o startup_stm32g474.o system_stm32g474.o \
  -L ./build_stm32 -lJaszczurHAL \
  -T /path/to/STM32G474RETx_FLASH.ld \
  -Wl,--gc-sections \
  -o firmware.elf
```

For complete firmware examples, prefer the checked-in STM32 build scripts:

```text
examples/portable_blink/g474/build.sh
examples/g474_i2c_scan/build.sh
examples/g474_adc_read/build.sh
examples/g474_gps/build.sh
```

They include startup files, linker script, target definitions, and HAL include
paths needed for flashable ELF/BIN/HEX outputs.

---

## Build File Structure

```text
CMakeLists.txt
  Host/mock test build; creates libhal_mock.a and tests.

arduino_lib/
  CMakeLists.txt
  toolchain_rp2040.cmake

build_arduino_lib.sh
  Convenience wrapper around arduino_lib/.

stm32_lib/
  CMakeLists.txt
  toolchain_stm32g474.cmake
  STM32G474RETx_FLASH.ld

examples/
  portable_blink/       # one app, RP2040 + STM32G474
  g474_*                # STM32G474 hardware-verification examples
```
