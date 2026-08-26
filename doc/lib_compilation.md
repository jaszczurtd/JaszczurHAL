# JaszczurHAL Library Compilation

## TL;DR

```bash
./scripts/build_rp_native_lib.sh --target rp2040
./scripts/build_rp_native_lib.sh --target rp2350-arm
./scripts/build_rp_native_lib.sh --target rp2350-riscv
./scripts/build_stm32_lib.sh
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 --clean
```

> **Part of [JaszczurHAL API Reference](JaszczurHAL_API.md)**

JaszczurHAL uses CMake for host, RP, and STM32 builds. The ESP32-S3 path invokes
the pinned ESP-IDF build system through a controlled Python runner. Embedded
builds select a target and a physical board from the declarative registry described in
[Target and board profiles](boards_profiles_howto.md).

| Target | Default board | Build entry | Backend selector |
|---|---|---|---|
| Host mock | - | repository-root CMake | `HAL_TARGET_MOCK` |
| RP2040 | `pico` | `rp_native_lib/` | `HAL_TARGET_RP2040` |
| RP2350 ARM | `pico2` | `rp_native_lib/` | `HAL_TARGET_RP2350_ARM` |
| RP2350 RISC-V | `pico2` | `rp_native_lib/` | `HAL_TARGET_RP2350_RISCV` |
| STM32G474 | `nucleo-g474re` | `stm32_lib/` | `HAL_TARGET_STM32G474` |
| ESP32-S3 | `waveshare-esp32-s3-zero` | controlled ESP-IDF component build | `HAL_TARGET_ESP32_S3` |

Repository-produced artifacts stay below `.build/`. The helper scripts reject
an output path outside this directory.

## Target and board compatibility

The public target selectors live in `src/hal/core/hal_target.h`. Define exactly one
selector when a toolchain does not provide enough information for automatic
detection:

```c
#define HAL_TARGET_RP2040
#define HAL_TARGET_RP2350_ARM
#define HAL_TARGET_RP2350_RISCV
#define HAL_TARGET_STM32G474
#define HAL_TARGET_ESP32_S3
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

The generated compatibility symbol has the form
`jh_board_contract_<target>_<board>_<featureHash>`. It makes mismatched
libraries, board headers, and feature sets fail during linking. Keep
`libJaszczurHAL.a` together with the generated headers from the same build.

Production feature resolution distinguishes:

- `requestedFeatures`: direct requests collected from CMake definition inputs
  and `hal_project_config.h`;
- `resolvedFeatures`: the sorted transitive registry closure used for source,
  dependency, and link-signature selection.

Feature records may also declare additive `buildEffects`. Generated CMake data
selects feature-owned sources and managed BearSSL, LittleFS, or SX126x source
manifests for RP and STM32. ESP-IDF consumes portable source effects from the
same registry and adds only its ESP32-specific backend files locally. Board
facts, target adapters, flash layout, and special firmware images remain owned
by their respective build recipes.

An exact target may add a required feature. ESP32-S3 always adds
`HAL_ENABLE_FREERTOS` with target provenance because ESP-IDF starts its
scheduler before `app_main()`.

The resolved board JSON stores both sets and their full closure digest. Its
`features` field remains as an alias of `resolvedFeatures`. The 12-character
`featureHash` is SHA-256 over `hal.profileId` followed by the sorted resolved
closure, with feature names serialized as `=1`. Redundant direct requests that
do not change the closure therefore do not change the archive signature. The
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

When the recorded direct requests include `HAL_ENABLE_STACK_PROTECTOR`, add
`-fstack-protector-strong` to every application C/C++ compilation. The native
firmware CMake recipes propagate this automatically. The installed archive
already contains the matching `__stack_chk_guard` / `__stack_chk_fail` runtime;
do not provide a second stack-protector runtime.

`hal_config.h` includes the installed generated feature header, so the direct
compiler receives the same resolved closure without running Python. The
installed `jh_board_config.h` also provides every board/provider definition
listed in `jh_board_resolved.json.boardCompileDefinitions`, including radio
backend, bus, stack, and pin selections. Pass only the target selector and the
recorded direct feature requests on the command line; do not repeat those
board-owned definitions with `-D` options. The generated reference uses a
GCC/Clang `constructor, used` root, so the board/feature signature remains live
under `--gc-sections` when the supported linker script retains constructor
arrays. The target SDK, startup objects, linker script, and platform libraries
remain part of the normal target toolchain requirements.

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

The helper attaches the HAL, generated board metadata, selected Pico SDK
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

The firmware link must include the generated link-signature reference object and use
the matching linker configuration. Its constructor root keeps the reference
live when `--gc-sections` is enabled; a missing or mismatched archive therefore
still fails with the expected undefined compatibility symbol. See
[STM32G474 memory map](../stm32_lib/MEMORY_MAP.md) for flash, SRAM, persistent
storage, and OTA reservations.

## ESP32-S3 with ESP-IDF

The ESP32-S3 build is a firmware-project flow, not an installed
`libJaszczurHAL.a` package. The production entrypoint is
`scripts/build_esp_idf.py`; it accepts `build`, `artifacts`, and `flash`:

```bash
# Clean build using the target's default board.
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 --board waveshare-esp32-s3-zero --clean

# Revalidate an existing build without compiling.
python3 scripts/build_esp_idf.py artifacts \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 --board waveshare-esp32-s3-zero

# Revalidate, then flash an application project at its manifest offsets.
python3 scripts/build_esp_idf.py flash \
  --project path/to/esp32-project \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --port /dev/serial/by-id/<Espressif-USB-Serial-JTAG-device>
```

`tests/fixtures/esp32s3_phase3` is the compile/link fixture used by CI and
Gate 7. It selects every ESP32-S3 backend delivered through Phase 3 and proves
feature resolution, component selection, compilation, linking, partition
generation, and artifact publication. It is not a runtime hardware fixture.

The default build directory is
`<project>/.build/esp-idf/esp32s3/waveshare-esp32-s3-zero/`. `--output` may
select another location below the project or repository `.build` root.
Repeatable `--source` arguments replace automatic discovery; without them, the
runner includes supported source files in the project root and recursively
under `src/`. Repeatable `--feature` and `--define` arguments extend project
configuration. `--idf-dir` or `JH_ESP_IDF_DIR` selects an externally managed
checkout only after its exact pin and tools pass verification.

The runner generates board-derived flash/PSRAM `sdkconfig` defaults, builds the
project sources with a small JaszczurHAL integration component, and validates
the result before publishing `jh_esp_idf_artifacts.json`. The manifest contains
relative paths for the ELF, MAP, application BIN, bootloader, partition table,
compile database, generated board/link metadata, and logs. Its ordered
`flashImages` retain each offset, size, and SHA-256. Configuration provenance
includes the final `sdkconfig` digest and selected partition profile; toolchain
provenance includes the pinned ESP-IDF version/commit, actual compiler, CMake,
Ninja, IDF Python and esptool versions, and the ESP-IDF `tools.json` digest.

The target always resolves `HAL_ENABLE_FREERTOS` and accepts the delivered
Phase 2 peripheral flags plus the Phase 3 network/service graph. System,
synchronization, GPIO, ADC, simple PWM, serial/debug, and timer sources are part
of the baseline component. Requested or transitively resolved features outside
that descriptor allowlist fail with `[JH-CFG-UNSUPPORTED]`.
The generated project CMake file carries the resolved feature list, component
source list, and public/private ESP-IDF component dependencies; the component
recipe consumes those generated lists instead of maintaining a second source
graph.
`scripts/build_esp_idf_phase0.py` remains a compatibility wrapper for the
isolated Phase 0 fixture.

## Repository workspace and VS Code

Open the JaszczurHAL repository root as the VS Code folder to use the
static-library workflow. It is separate from the firmware-project workflow and
uses the global task labels already shared by JaszczurHAL consumers:

| Shortcut | Repository task |
|---|---|
| `Ctrl+Shift+1` | `Project: Build` |
| `Ctrl+Shift+6` | `Project: Refresh IntelliSense` |
| `Ctrl+Shift+7` | `Project: Clean` |
| `Ctrl+Shift+0` | `Project: Install library` |
| `Ctrl+Shift+Alt+1` | `Project: Select board (GUI)` |
| `Ctrl+Shift+Alt+2` | `Project: Select board` |

The initial profile is `rp2040:pico`. Selection reads target and board data
from `boards/` and is stored in the gitignored
`.vscode/jaszczurhal.library.local.json`. Supported profiles include host mock,
all three native RP target families, and STM32G474. Build artifacts stay in:

```text
.build/vscode/library/<target>/<board>/
```

`Project: Build` produces the active linkable archive and selects its generated
compile database for cpptools. `Project: Refresh IntelliSense` performs the
same incremental build explicitly before rewriting the gitignored
`.vscode/c_cpp_properties.json`. Each production build contains
`libJaszczurHAL.a`; the mock profile contains `libhal_mock.a`.

`Project: Install library` builds the active production profile and installs
its archive, public headers, generated board headers, and link-signature data to
`.build/install/<target>/<board>/`. The mock target has no installation
installation interface. `Project: Clean` removes only these two directories for the active
profile and its matching managed IntelliSense file. It preserves other builds,
managed tools, and dependency sources.

The tracked root `.vscode` files are derived from the board registry. Verify or
regenerate all tracked generated artifacts after a registry or workspace-task
change:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Upload, serial-monitor, and debug-probe shortcuts remain firmware-only and are
intentionally undefined when the repository root is open.

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
