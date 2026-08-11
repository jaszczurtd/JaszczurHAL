# JaszczurHAL Library Compilation

## TL;DR

```bash
./scripts/build_rp_native_lib.sh --target rp2040
./scripts/build_rp_native_lib.sh --target rp2350-arm
./scripts/build_rp_native_lib.sh --target rp2350-riscv
./scripts/build_stm32_lib.sh
```

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

The public target selectors live in `src/hal/core/hal_target.h`. Define exactly one
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
identifies the physical board profile. The source tree tracks the generated
global registry and fallback under `src/hal/generated/`. Each build generates:

```text
include/generated/
  jh_board_config.h
  jh_link_contract.h
```

The generated contract symbol has the form
`jh_board_contract_<target>_<board>_<featureHash>`. It makes mismatched
libraries, board headers, and feature sets fail during linking. Keep
`libJaszczurHAL.a` together with the generated headers from the same build.

Production feature resolution distinguishes:

- `requestedFeatures`: direct requests collected from CMake definition inputs
  and `hal_project_config.h`;
- `resolvedFeatures`: the sorted transitive registry closure used for source,
  dependency, and link-contract selection.

The resolved board JSON stores both sets and their full closure digest. Its
`features` field remains as an alias of `resolvedFeatures`. The 12-character
`featureHash` is SHA-256 over `hal.profileId` followed by the sorted resolved
closure, with feature names serialized as `=1`. Redundant direct requests that
do not change the closure therefore do not change the archive contract. The
same JSON records `boardCompileDefinitions`; generated CMake exposes them as
`JH_BOARD_COMPILE_DEFINITIONS`, while `jh_board_config.h` materializes them for
direct compiler consumers.

Two conditional rules remain outside registry v1: AT24C256 EEPROM can add I2C,
and GPS can add UART when no serial transport was requested. They remain in
`hal_config.h` and are outside feature-hash equivalence. Target, board,
provider, capability, and tunable checks also remain there.

Validate feature inputs strictly before a release build:

```bash
python3 scripts/generate_hal_features.py --lint --input-root .
python3 scripts/generate_hal_features.py \
  --lint --effective --input-root . \
  --resolution-output .build/effective-feature-resolution.json
```

Both commands fail when they find an invalid configuration. `--report-only`
is available for a temporary migration audit, not for the normal quality gate.

## Installed package and direct compiler use

After configuring and building either embedded static-library entry, create a
complete matching JaszczurHAL installation:

```bash
cmake --install .build/static/<target>/<board> \
  --prefix .build/install/<target>/<board>
```

The relevant installed files are:

```text
include/
  JaszczurHAL.h
  hal/generated/
    jh_hal_features.h
    jh_board_registry.h
    jh_board_fallback_config.h
  generated/
    jh_board_config.h
    jh_link_contract.h
lib/
  libJaszczurHAL.a
share/JaszczurHAL/generated/
  jh_link_contract_reference.c
  jh_board_resolved.json
```

All other public HAL headers are installed under `include/`. Treat this tree as
one unit. For a direct compiler build, add `include/` and `include/generated/`
to the include path, compile with the target selector and the direct requests
recorded in `jh_board_resolved.json`, compile
`share/JaszczurHAL/generated/jh_link_contract_reference.c`, and link that object
with `lib/libJaszczurHAL.a`. For example, the command shape is:

```bash
"${CXX}" <target compile flags> \
  -I<prefix>/include -I<prefix>/include/generated \
  -DHAL_TARGET_<TARGET>=1 -D<REQUESTED_FEATURE>=1 \
  -c app.cpp -o app.o
"${CC}" <target compile flags> \
  -I<prefix>/include -I<prefix>/include/generated \
  -c <prefix>/share/JaszczurHAL/generated/jh_link_contract_reference.c \
  -o jh_link_contract_reference.o
"${CXX}" <target link flags> app.o jh_link_contract_reference.o \
  <prefix>/lib/libJaszczurHAL.a <platform libraries> -o firmware.elf
```

`hal_config.h` includes the installed generated feature header, so the direct
compiler receives the same resolved closure without running Python. The
installed `jh_board_config.h` also provides every board/provider definition
listed in `jh_board_resolved.json.boardCompileDefinitions`, including radio
backend, bus, stack, and pin selections. Pass only the target selector and the
recorded direct feature requests on the command line; do not repeat those
board-owned definitions with `-D` options. The generated reference uses a
GCC/Clang `constructor, used` root, so the board/feature contract remains live
under `--gc-sections` when the supported linker script retains constructor
arrays. The target SDK, startup objects, linker script, and platform libraries
remain part of the normal target toolchain contract.

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

# RP2040 / Pico with an example application
./scripts/build_rp_native_lib.sh \
  --target rp2040 \
  --board pico \
  --example 01_core_runtime

# RP2350 ARM
./scripts/build_rp_native_lib.sh --target rp2350-arm

# RP2350 RISC-V
./scripts/build_rp_native_lib.sh --target rp2350-riscv

# Native FreeRTOS SMP
./scripts/build_rp_native_lib.sh --target rp2040 --freertos

# Linkable static library only, without firmware probes
./scripts/build_rp_native_lib.sh --target rp2040 --library-only
```

The main options are:

| Option | Meaning |
|---|---|
| `--target NAME` | `rp2040`, `rp2350-arm`, or `rp2350-riscv` |
| `--board NAME` | Board profile compatible with the selected target |
| `--example NAME` | Build `examples/NAME` as firmware |
| `--example-source FILE` | Select one source from a multi-profile example (repeatable) |
| `--freertos` | Enable the pinned FreeRTOS SMP kernel |
| `--library-only` | Build only the linkable `libJaszczurHAL.a` target, without firmware probes |
| `-p`, `--project-config DIR` | Directory containing `hal_project_config.h` |
| `-D KEY=VALUE` | Additional HAL definition; repeatable |
| `--sdk-dir PATH` | Pico SDK checkout |
| `--toolchain PATH` | Cross-toolchain root |
| `--picotool-dir PATH` | `picotool` source checkout |
| `-o`, `--output DIR` | Build directory below `.build/` |
| `--clean` | Recreate the selected build directory |
| `-j`, `--jobs N` | Parallel build jobs |

The default output is `.build/static/<target>/<board>/`. A default build
verifies the static library and complete ELF/BIN/UF2 probe set; a
`--library-only` build verifies only the archive:

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
When `HAL_ENABLE_FREERTOS` is selected, direct CMake invokes
`scripts/component_manager.py` to prepare or verify FreeRTOS-Kernel. An
external `JH_FREERTOS_KERNEL_DIR` is verified and never replaced. After the
other dependencies are present, the equivalent basic RP2040 configuration is:

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
-DJH_RP_NATIVE_APP_DIR="$PWD/examples/01_core_runtime" \
-DHAL_PROJECT_CONFIG_DIR="$PWD/examples/01_core_runtime"
```

The application supplies `app_start()`, `app_task0()`, and optionally
`app_task1()`. `src/hal_app_entry.cpp` owns `main()` and maps these hooks to
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

The same backend also compiles with the host compiler for sanity checks and the
clang-tidy STM32 compile database. That mode leaves `JH_STM32G474_HW` undefined
and is opt-in, so a missing cross toolchain cannot silently produce a host
library instead of firmware:

```bash
cmake -S stm32_lib -B .build/manual/stm32g474-host \
  -DJH_STM32_HOST_SANITY=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build .build/manual/stm32g474-host --parallel
```

Without either `CMAKE_TOOLCHAIN_FILE` or `JH_STM32_HOST_SANITY` the
configuration stops and names both options. Generated board files stay inside
the CMake build tree, so keep the build directory under a `.build` root.

Pass project features through `EXTRA_HAL_DEFINES` or use
`scripts/build_stm32_lib.sh -D ...`. `HAL_ENABLE_FREERTOS` selects the pinned
kernel integration. Direct CMake invokes `scripts/component_manager.py` to
prepare or verify the kernel. The shell helper invokes
`scripts/ensure_freertos_kernel.sh` for `--freertos` or an explicit
`-D HAL_ENABLE_FREERTOS`; a feature found only in `hal_project_config.h` is
prepared by the CMake fallback. The wrapper delegates to the same manager. An
external `JH_FREERTOS_KERNEL_DIR` is verified and never replaced. Bare-metal
firmware calls the generated HAL application entry in a cooperative loop;
FreeRTOS firmware uses scheduler-managed tasks.

The firmware link must include the generated contract reference object and use
the matching linker configuration. Its constructor root keeps the reference
live when `--gc-sections` is enabled; a missing or mismatched archive therefore
still fails with the expected undefined contract symbol. See
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
