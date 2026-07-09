# JaszczurHAL Library Compilation

> **Part of [JaszczurHAL API Reference](JaszczurHAL_API.md)**

JaszczurHAL can be built in three different ways, depending on the target:

| Target | Build entry | Output | Backend switch |
|---|---|---|---|
| Host / mock tests | repository-root CMake | `build/libhal_mock.a` + tests | `HAL_TARGET_MOCK` |
| RP2040 / RP2350 through Arduino-pico | `rp2040_lib/` or `scripts/build_rp2040_lib.sh` | `build_rp2040/libJaszczurHAL.a` | `HAL_TARGET_RP2040` |
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
./scripts/build_rp2040_lib.sh
```

The script auto-detects the latest Arduino-pico core and `pqt-gcc` toolchain
under `~/.arduino15/packages/rp2040/`. The output is:

```text
build_rp2040/libJaszczurHAL.a
```

Script options:

| Option | Default | Description |
|---|---|---|
| `-r`, `--root PATH` | `~/.arduino15/packages/rp2040` | Arduino RP2040 package root |
| `-b`, `--board VARIANT` | `rpipico` | Board variant, e.g. `rpipicow`, `rpipico2` |
| `-c`, `--chip CHIP` | `rp2040` | Target chip: `rp2040` / `rp2350` |
| `--freertos` | - | Build with arduino-pico FreeRTOS SMP mode and define `HAL_ENABLE_FREERTOS` |
| `-p`, `--project-config DIR` | - | Directory containing `hal_project_config.h` |
| `-D DEFINE` | - | Extra compile definition, e.g. `HAL_ENABLE_WIFI` or `KEY=VALUE`; repeatable |
| `-o`, `--output DIR` | `./build_rp2040` | Output directory |
| `--clean` | - | Remove build directory before building |
| `-j`, `--jobs N` | `nproc` | Parallel build jobs |

Examples:

```bash
# Default Raspberry Pi Pico build
./scripts/build_rp2040_lib.sh

# Project-local config plus explicit modules
./scripts/build_rp2040_lib.sh \
  -p /path/to/project \
  -D HAL_ENABLE_WIFI \
  -D HAL_ENABLE_GPS \
  -D HAL_ENABLE_MCP9600

# Pico W
./scripts/build_rp2040_lib.sh \
  --board rpipicow \
  -D HAL_ENABLE_WIFI

# RP2040 FreeRTOS SMP mode
./scripts/build_rp2040_lib.sh --freertos

# Clean rebuild into a custom directory
./scripts/build_rp2040_lib.sh --clean -o ./my_build
```

### RP2040 FreeRTOS note

`HAL_ENABLE_FREERTOS` is valid on RP2040 only when the Arduino-pico build is
already in its native FreeRTOS mode. In practice that means the core must define
`__FREERTOS`, usually via the board menu `Operating System -> FreeRTOS SMP` or
an equivalent FQBN option such as `os=freertos`.

For the static-library helper, use:

```bash
./scripts/build_rp2040_lib.sh --freertos
```

This passes `ARDUINO_OS=freertos` to CMake, defines `__FREERTOS`, makes the
arduino-pico FreeRTOS wrapper include directory visible, and adds
`HAL_ENABLE_FREERTOS` to the HAL compile definitions. In that mode the RP2040
backend uses FreeRTOS-aware `hal_mutex_*`, `hal_delay_ms()`, and `hal_idle()`
paths while keeping `hal_critical_section_*` as a hard per-core interrupt mask.
Manual CMake users can do the same with:

```bash
cmake -S rp2040_lib -B build_rp2040_freertos \
  -DCMAKE_TOOLCHAIN_FILE=rp2040_lib/toolchain_rp2040.cmake \
  -DARDUINO_ROOT=~/.arduino15/packages/rp2040 \
  -DARDUINO_CHIP=rp2040 \
  -DARDUINO_VARIANT=rpipico \
  -DARDUINO_OS=freertos \
  -DEXTRA_HAL_DEFINES="HAL_ENABLE_FREERTOS"

cmake --build build_rp2040_freertos -j$(nproc)
```

The local `third_party/FreeRTOS-Kernel` tree is not compiled for the current
RP2040 backend. Defining `HAL_ENABLE_FREERTOS` in a normal non-FreeRTOS
RP2040 static-library build intentionally produces a clear compile-time error.

### Manual CMake Build

```bash
cmake -S rp2040_lib -B build_rp2040 \
  -DCMAKE_TOOLCHAIN_FILE=rp2040_lib/toolchain_rp2040.cmake \
  -DARDUINO_ROOT=~/.arduino15/packages/rp2040 \
  -DARDUINO_CHIP=rp2040 \
  -DARDUINO_VARIANT=rpipico

cmake --build build_rp2040 -j$(nproc)
```

Useful CMake cache variables:

| Variable | Default | Description |
|---|---|---|
| `ARDUINO_ROOT` | `~/.arduino15/packages/rp2040` | Arduino package directory |
| `ARDUINO_CHIP` | `rp2040` | Target chip |
| `ARDUINO_VARIANT` | `rpipico` | Board variant directory |
| `ARDUINO_OS` | - | Arduino-pico OS option; use `freertos` to define `__FREERTOS` |
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
  -L ./build_rp2040 -lJaszczurHAL \
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
| `JH_FREERTOS_KERNEL_DIR` | `third_party/FreeRTOS-Kernel` from `freertos_core_version.conf` | Path to the local FreeRTOS-Kernel checkout |

Example with extra modules:

```bash
cmake -S stm32_lib -B build_stm32 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32_lib/toolchain_stm32g474.cmake" \
  -DEXTRA_HAL_DEFINES="HAL_ENABLE_STM32G474_FDCAN"

cmake --build build_stm32 -j$(nproc)
```

The initial STM32 profile currently enables the backend pieces that exist in
`stm32_lib/CMakeLists.txt`: I2C, I2C-slave, SPI, UART, DAC, DMA-backed DACless,
PCNT, the
MCP401X/MAX5395 digipot backends, MCP9600/MAX6675 thermocouple backends,
BH1750 ambient-light, TSC2007 and STMPE610 touch controllers, the IR small
decoder, ADS1115 external ADC, OneWire/DS18B20, the MCP2515 external CAN
backend, GPS over UART, and the HD44780 character LCD. Other CAN backends are
**not** default: MCP251XFD (`HAL_ENABLE_MCP251XFD`) and native STM32G474 FDCAN
(`HAL_ENABLE_STM32G474_FDCAN`) must be added via `-DEXTRA_HAL_DEFINES` as shown
above. Additional modules should be enabled only once their STM32G474 backend
exists.

### STM32G474 FreeRTOS note

`HAL_ENABLE_FREERTOS` is valid on STM32G474 only when the pinned
`FreeRTOS-Kernel` dependency is available and the include path provides both
`<FreeRTOS.h>` and a target `FreeRTOSConfig.h`. The repo/ref/default directory
are recorded in `freertos_core_version.conf`.

The STM32 CMake integration now compiles the explicit kernel source list
(`tasks.c`, `queue.c`, `list.c`, `timers.c`, `event_groups.c`,
`stream_buffer.c`, `portable/GCC/ARM_CM4F/port.c`, and
`portable/MemMang/heap_4.c`), adds the target `FreeRTOSConfig.h`, and lets the
FreeRTOS port own SVC/PendSV/SysTick.

```bash
./scripts/build_stm32_lib.sh --clean --freertos
```

The script runs `scripts/ensure_freertos_kernel.sh` before CMake configuration,
so a fresh checkout can fetch `third_party/FreeRTOS-Kernel` automatically. CMake
users opt in with the public HAL flag:

```bash
cmake -S stm32_lib -B build_stm32_freertos \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32_lib/toolchain_stm32g474.cmake" \
  -DEXTRA_HAL_DEFINES="HAL_ENABLE_FREERTOS"
```

If the kernel is not under `third_party/FreeRTOS-Kernel`, pass:

```bash
./scripts/build_stm32_lib.sh --freertos --freertos-kernel /path/to/FreeRTOS-Kernel
```

or set `JH_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel`.

This stage provides native FreeRTOS API availability and kernel linkage.
STM32 HAL runtime primitives such as `hal_mutex_*`, `hal_delay_ms()`, and
`hal_idle()` are FreeRTOS-aware in task context, with fallback delays before the
scheduler and from ISR/critical contexts. If `HAL_PROVIDE_APP_ENTRY` is present,
STM32 FreeRTOS builds call `app_start()`, create `app_task0()` and optional
`app_task1()` FreeRTOS tasks, and then start the scheduler. Override the
HAL-provided task configuration with `HAL_FREERTOS_TASK0_STACK`,
`HAL_FREERTOS_TASK1_STACK`, `HAL_FREERTOS_TASK0_PRIORITY`, and
`HAL_FREERTOS_TASK1_PRIORITY`; stack values are FreeRTOS stack words.
Module-level lazy singleton mutexes and broader task-safety claims remain
tracked in
[`Thread-SafetyAudit.md`](Thread-SafetyAudit.md).

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

When enabling `HAL_ENABLE_LITTLEFS` on STM32G474, reserve a LittleFS flash
partition in both the C/C++ compile definitions and linker symbols. The
checked-in STM32 CMake helpers do this automatically with a 64 KB default when
`HAL_ENABLE_LITTLEFS` is present in their define list. Manual builds should add
matching options, for example:

```bash
-DHAL_ENABLE_LITTLEFS \
-DHAL_STM32_FLASH_LITTLEFS_SIZE=65536u \
-Wl,--defsym=HAL_STM32_FLASH_LITTLEFS_SIZE=65536
```

The reservation sits before the EEPROM/KV flash pages and reduces the space
available for application code.

For complete firmware examples, use the dispatcher-backed example manifests.
Every numbered `examples/NN_*` directory can be opened directly in VS Code and
builds through `vscode/entry/jh-vscode` plus `cmake/jh_firmware_project`.
The quality-gate runner builds the same manifests:

```bash
# STM32G474 example firmware
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"

# RP2040 example firmware
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"
```

Examples are the numbered directories under `examples/` (e.g. `01_blink`,
`38_stm32g474_fdcan_native`). `runalltests.sh` builds them as gate 7.

## Standalone VS Code Firmware Project

For a complete target-selectable VS Code firmware project, use the generator
from the shared `jh-vscode` layer instead of copying legacy wrapper scripts or
project-local CMake recipes:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output jaszczurhal-vscode-example
```

To start on a non-default board, pass the initial target and board:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output jaszczurhal-stm32-example \
  --target stm32g474 \
  --board nucleo-g474re
```

The generated project should live outside the JaszczurHAL repository, for
example next to the other firmware projects. It contains a blink app,
`.vscode/jaszczurhal.project.json`, `.vscode/tasks.json`, a project-local
`hal_project_config.h`, and no project-local firmware `CMakeLists.txt`. The
manifest points `cmake.sourceDir` at
`libraries/JaszczurHAL/cmake/jh_firmware_project`; that dispatcher selects the
active backend from the resolved `target`/`board` and target registry. The
complete manifest/source/target/upload model is described in
[`FwProjectWorkflow.md`](FwProjectWorkflow.md).

The process of adding new files to a project that uses JaszczurHAL is described in
[`FwProjectWorkflow.md`](FwProjectWorkflow.md#adding-project-source-files). In short,
flat projects can add `*.c`, `*.cpp`, `*.h`, and `*.hpp` files directly under
`JH_PROJECT_DIR`; subdirectory layouts should set the complete
semicolon-separated `JH_PROJECT_SOURCES` list in `.vscode/jaszczurhal.project.json`.

Build-related commands then go through the shared entrypoint:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode build-debug --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode select-board --project "$PWD" --interactive
```

`Project: Upload` is target-neutral: RP2040 uses the registry upload strategy
(`uf2` by default, or verified serial when configured); STM32G474 uses the
OpenOCD upload target. First flashing a blank RP2040 board can use BOOTSEL/UF2.
An explicit serial port upload still requires `--port` plus
`--allow-unverified-port` when the identity cannot be verified.

---

## Build File Structure

```text
CMakeLists.txt
  Host/mock test build; creates libhal_mock.a and tests.

rp2040_lib/
  CMakeLists.txt
  toolchain_rp2040.cmake

stm32_lib/
  CMakeLists.txt
  toolchain_stm32g474.cmake
  STM32G474RETx_FLASH.ld

scripts/
  build_rp2040_lib.sh
    Convenience wrapper around rp2040_lib/.

  build_stm32_lib.sh
    Convenience wrapper around stm32_lib/.

  ensure_freertos_kernel.sh
    Shared helper for fetching/verifying the pinned third_party/FreeRTOS-Kernel
    checkout from freertos_core_version.conf.

examples/
  CMakeLists.txt        # compatibility wrapper around scripts/examples_dispatcher.py
  01_blink/ ... 41_*/     # numbered, self-contained example apps

cmake/
  jh_firmware_project/CMakeLists.txt
    Shared firmware dispatcher used by generated and migrated VS Code projects.
  targets/rp2040.cmake
  targets/stm32g474.cmake

vscode/
  entry/jh-vscode
    Stable build/upload/monitor/select-board entrypoint for firmware projects.
  targets/*.json
    Target and board registry consumed by jh-vscode and the generator.
```
