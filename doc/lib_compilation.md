# JaszczurHAL Library Compilation

> **Part of [JaszczurHAL API Reference](JaszczurHAL_API.md)**

JaszczurHAL uses CMake for host tests and for every supported embedded target.
Embedded builds select a target and a physical board from the declarative
registry described in
[Target and board profiles](boards_profiles_howto.md).

| Target | Default board | Build entry | Backend selector |
|---|---|---|---|
| Host mock | - | repository-root CMake | `HAL_TARGET_MOCK` |
| RP2040 | `pico` | `rp_native_lib/` | `HAL_TARGET_RP2040` |
| RP2350 ARM | `pico2` | `rp_native_lib/` | `HAL_TARGET_RP2350_ARM` |
| RP2350 RISC-V | `pico2` | `rp_native_lib/` | `HAL_TARGET_RP2350_RISCV` |
| STM32G474 | `nucleo-g474re` | `stm32_lib/` | `HAL_TARGET_STM32G474` |

Repository-produced artifacts stay below `.build/`. The helper scripts reject
an output path outside this directory.

## Target and board contract

The public target selectors live in `src/hal/hal_target.h`. Define exactly one
selector when a toolchain does not provide enough information for automatic
detection:

```c
#define HAL_TARGET_RP2040
#define HAL_TARGET_RP2350_ARM
#define HAL_TARGET_RP2350_RISCV
#define HAL_TARGET_STM32G474
#define HAL_TARGET_MOCK
```

`JH_TARGET` identifies the processor and execution platform. `JH_BOARD`
identifies the physical board profile. The build generates:

```text
include/generated/
  jh_board_config.h
  jh_board_registry.h
  jh_link_contract.h
```

The generated contract symbol has the form
`jh_board_contract_<target>_<board>_<featureHash>`. It makes mismatched
libraries, board headers, and feature sets fail during linking. Keep
`libJaszczurHAL.a` together with the generated headers from the same build.

## Host mock

The repository-root project builds the deterministic mock backend and its test
executables:

```bash
cmake -S . -B .build/host
cmake --build .build/host --parallel
ctest --test-dir .build/host --output-on-failure
```

The host build needs a native C/C++ toolchain and CMake. It does not need an
embedded SDK or cross compiler.

## RP2040 and RP2350

RP builds use the pinned official Pico SDK, the generated board profile, and
the HAL-owned application entry.

### Helper script

From the repository root:

```bash
# RP2040 / Pico
./scripts/build_rp_native_lib.sh

# RP2040 / Pico W with an example application
./scripts/build_rp_native_lib.sh \
  --target rp2040 \
  --board picow \
  --example 56_http_https_client

# RP2350 ARM
./scripts/build_rp_native_lib.sh --target rp2350-arm

# RP2350 RISC-V
./scripts/build_rp_native_lib.sh --target rp2350-riscv

# Native FreeRTOS SMP
./scripts/build_rp_native_lib.sh --target rp2040 --freertos
```

The main options are:

| Option | Meaning |
|---|---|
| `--target NAME` | `rp2040`, `rp2350-arm`, or `rp2350-riscv` |
| `--board NAME` | Board profile compatible with the selected target |
| `--example NAME` | Build `examples/NAME` as firmware |
| `--freertos` | Enable the pinned FreeRTOS SMP kernel |
| `-p`, `--project-config DIR` | Directory containing `hal_project_config.h` |
| `-D KEY=VALUE` | Additional HAL definition; repeatable |
| `--sdk-dir PATH` | Pico SDK checkout |
| `--toolchain PATH` | Cross-toolchain root |
| `--picotool-dir PATH` | `picotool` source checkout |
| `-o`, `--output DIR` | Build directory below `.build/` |
| `--clean` | Recreate the selected build directory |
| `-j`, `--jobs N` | Parallel build jobs |

The default output is `.build/static/<target>/<board>/`. Every build verifies
the static library and complete ELF/BIN/UF2 probe set:

```text
.build/static/<target>/<board>/
  libJaszczurHAL.a
  include/generated/
  jh_rp_native_artifact_probe.{elf,bin,uf2}
  jh_rp_native_core1_probe.{elf,bin,uf2}
  jh_rp_native_firmware.{elf,bin,uf2}  # with --example
```

The core-1 probe verifies the application-entry and multicore symbols. In a
bare-metal build, `app_task1()` runs on Pico SDK core 1. In a FreeRTOS build,
the HAL creates affinity-bound tasks and starts the scheduler.

### Direct CMake build

The helper prepares pinned dependencies and supplies the cache variables.
After those dependencies are present, the equivalent basic RP2040
configuration is:

```bash
cmake -S rp_native_lib -B .build/manual/rp2040-pico \
  -DPICO_SDK_PATH="$PWD/third_party/pico-sdk" \
  -DJH_PICOTOOL_EXECUTABLE="$PWD/.build/tools/picotool/picotool" \
  -DJH_TARGET=rp2040 \
  -DJH_BOARD=pico
cmake --build .build/manual/rp2040-pico --parallel
```

For an application directory, add:

```bash
-DJH_RP_NATIVE_APP_DIR="$PWD/examples/01_blink" \
-DHAL_PROJECT_CONFIG_DIR="$PWD/examples/01_blink"
```

The application supplies `app_start()`, `app_task0()`, and optionally
`app_task1()`. `src/hal/hal_app_entry.cpp` owns `main()` and maps these hooks to
the selected bare-metal or FreeRTOS execution model.

### Embedding the RP CMake support

Dispatcher-backed firmware projects use `cmake/targets/rp-native.cmake`. A
custom Pico SDK CMake project can use the same integration:

```cmake
include(path/to/JaszczurHAL/cmake/jh_rp_native_sdk.cmake)

add_executable(firmware
    app.cpp
)
jh_add_rp_native_firmware(firmware)
```

The helper attaches the HAL, generated board contract, selected Pico SDK
libraries, linker layout, application entry, and ELF/BIN/UF2 post-processing.

Flash layout, persistent storage, OTA slots, and RAM ownership are documented
in [RP memory map](../rp_native_lib/MEMORY_MAP.md).

## STM32G474

The STM32G474 build produces a static library for the generated board profile:

```bash
# Bare-metal
./scripts/build_stm32_lib.sh

# FreeRTOS
./scripts/build_stm32_lib.sh --freertos

# Project configuration and extra features
./scripts/build_stm32_lib.sh \
  --board nucleo-g474re \
  -p /path/to/firmware \
  -D HAL_ENABLE_MCP2515 \
  -D HAL_ENABLE_LITTLEFS
```

The default output is:

```text
.build/static/stm32g474/nucleo-g474re/
  libJaszczurHAL.a
  include/generated/
```

Direct CMake configuration uses the supplied toolchain:

```bash
cmake -S stm32_lib -B .build/manual/stm32g474-nucleo \
  -DCMAKE_TOOLCHAIN_FILE=stm32_lib/toolchain_stm32g474.cmake \
  -DJH_TARGET=stm32g474 \
  -DJH_BOARD=nucleo-g474re
cmake --build .build/manual/stm32g474-nucleo --parallel
```

Pass project features through `EXTRA_HAL_DEFINES` or use
`scripts/build_stm32_lib.sh -D ...`. `HAL_ENABLE_FREERTOS` selects the pinned
kernel integration. Bare-metal firmware calls the generated HAL application
entry in a cooperative loop; FreeRTOS firmware uses scheduler-managed tasks.

The firmware link must retain the generated contract object and use the
matching linker configuration. See
[STM32G474 memory map](../stm32_lib/MEMORY_MAP.md) for flash, SRAM, persistent
storage, and OTA reservations.

## Firmware projects and VS Code

Create, inspect, and build a project through the maintained workflow:

```bash
./vscode/tools/create-vscode-example.py --output /path/to/project
./vscode/entry/jh-vscode config-dump --project /path/to/project
./vscode/entry/jh-vscode build --project /path/to/project
```

Generated projects expose board-aware build, upload, monitor, debug, OTA, and
test tasks. Details are in
[Firmware Project Workflow](FwProjectWorkflow.md) and
[VS Code integration](../vscode/README.md).
