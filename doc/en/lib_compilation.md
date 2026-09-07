<a id="jaszczurhal-library-compilation"></a>

# Building JaszczurHAL

*Also available in [Polish](../pl/lib_compilation.md).*

<a id="tldr"></a>

## Essential commands

```bash
./scripts/build_rp_native_lib.sh --target rp2040
./scripts/build_rp_native_lib.sh --target rp2350-arm
./scripts/build_rp_native_lib.sh --target rp2350-riscv
./scripts/build_stm32_lib.sh
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 --clean
```

> **Part of [JaszczurHAL API Reference](JaszczurHAL_API.md)**

JaszczurHAL uses CMake for host, RP, and STM32 builds. For ESP32-S3, a repository-managed Python script runs the pinned ESP-IDF build system. Embedded builds select a target and physical board from the registry described in [Target and board profiles](boards_profiles_howto.md).

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

Feature configuration distinguishes two sets:

- `requestedFeatures`: features specified directly through CMake definitions and `hal_project_config.h`;
- `resolvedFeatures`: the sorted set of requested features and all transitive registry dependencies, used to select sources, dependencies, and the link signature.

Feature entries may include additional `buildEffects`. Generated CMake data selects sources for each feature and repository-managed BearSSL, LittleFS, or SX126x source manifests for RP and STM32. ESP-IDF uses the same entries for portable sources and adds its ESP32-specific files locally. The relevant build scripts define board configuration, target adapters, flash layout, and special firmware images.

A target may require an additional feature. ESP32-S3 always adds `HAL_ENABLE_FREERTOS` and records the target as its source because ESP-IDF starts the scheduler before `app_main()`.

The resolved board JSON stores both feature sets and the full digest of the dependency-resolved set. `features` remains an alias for `resolvedFeatures`. The 12-character `featureHash` is derived from SHA-256 over `hal.profileId` followed by sorted feature names serialized with `=1`. An additional request that leaves the resolved set unchanged also leaves the library signature unchanged.

The JSON also records `boardCompileDefinitions`. CMake exposes them as `JH_BOARD_COMPILE_DEFINITIONS`, and `jh_board_config.h` provides them to projects compiled directly.

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

Both commands fail on invalid configuration. Use `--report-only` for temporary migration audits, not as a replacement for normal quality checks.

## Installed package and direct compiler use

After configuring and building the static library for the selected platform, create a complete installation from that same configuration:

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

Other public HAL headers are installed under `include/`. Treat the entire installation as one package. For direct compiler use, add `include/` and `include/generated/` to the include paths and define the target selector and requested features recorded in `jh_board_resolved.json`. Also compile `share/JaszczurHAL/generated/jh_link_contract_reference.c` and link the resulting object with `lib/libJaszczurHAL.a`. For example:

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

If the requested features include `HAL_ENABLE_STACK_PROTECTOR`, add `-fstack-protector-strong` when compiling every application C/C++ file. Native firmware CMake configurations do this automatically. The installed library already provides `__stack_chk_guard` / `__stack_chk_fail`; do not add a second stack-protection runtime.

`hal_config.h` includes the installed generated feature header, giving direct compiler builds the same dependency set without running Python. `jh_board_config.h` also provides the definitions in `jh_board_resolved.json.boardCompileDefinitions`, including radio implementation, bus, stack, and pin selections. Pass only the target selector and recorded feature requests on the command line; do not repeat board-profile definitions with `-D`.

The generated signature reference uses GCC/Clang `constructor, used` attributes. The compatibility check remains active under `--gc-sections` when the supported linker script retains constructor arrays. The target SDK, startup objects, linker script, and platform libraries are still required.

<a id="host-mock"></a>

## Host tests with the mock implementation

The repository-root project builds the deterministic mock implementation and test executables:

```bash
cmake -S . -B .build/host
cmake --build .build/host --parallel
ctest --test-dir .build/host --output-on-failure
```

The host build needs a native C/C++ toolchain and CMake. It does not need an
embedded SDK or cross compiler.

## RP2040 and RP2350

RP builds use the official Pico SDK version pinned by the repository, the generated board profile, and the application entry point supplied by HAL.

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

The core-1 probe checks application-entry and multicore symbols. In bare-metal configurations, `app_task1()` runs on core 1 provided by Pico SDK. With FreeRTOS, HAL creates tasks with CPU affinity and starts the scheduler.

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

The application provides `app_start()`, `app_task0()`, and optionally `app_task1()`. `src/hal_app_entry.cpp` defines `main()` and invokes these functions according to the selected bare-metal or FreeRTOS execution model.

<a id="embedding-the-rp-cmake-support"></a>

### Using the RP CMake integration in your project

Projects using the shared target-selection workflow include `cmake/targets/rp-native.cmake`. A custom Pico SDK CMake project can use the same integration:

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
in [RP memory map](../../rp_native_lib/MEMORY_MAP.md).

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

The same implementation can be built with the host compiler for basic checks and to generate the STM32 compilation database for clang-tidy. This mode leaves `JH_STM32G474_HW` undefined and must be enabled explicitly. A missing cross toolchain therefore cannot silently create a host library instead of firmware:

```bash
cmake -S stm32_lib -B .build/manual/stm32g474-host \
  -DJH_STM32_HOST_SANITY=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build .build/manual/stm32g474-host --parallel
```

Without either `CMAKE_TOOLCHAIN_FILE` or `JH_STM32_HOST_SANITY` the
configuration stops and names both options. Generated board files stay inside
the CMake build tree, so keep the build directory under a `.build` root.

Pass project features through `EXTRA_HAL_DEFINES` or `scripts/build_stm32_lib.sh -D ...`. `HAL_ENABLE_FREERTOS` enables integration with the pinned kernel. Direct CMake configuration runs `scripts/component_manager.py` to prepare or verify it.

The shell script calls `scripts/ensure_freertos_kernel.sh` for `--freertos` or explicit `-D HAL_ENABLE_FREERTOS`. If the feature is requested only in `hal_project_config.h`, CMake prepares the dependency. Both paths use the same manager. An external `JH_FREERTOS_KERNEL_DIR` is verified, never replaced.

Bare-metal firmware uses a cooperative loop through the generated HAL application entry. FreeRTOS firmware uses scheduler-managed tasks.

The firmware link must include the generated link-signature reference object and use
the matching linker configuration. Its constructor root keeps the reference
live when `--gc-sections` is enabled; a missing or mismatched archive therefore
still fails with the expected undefined compatibility symbol. See
[STM32G474 memory map](../../stm32_lib/MEMORY_MAP.md) for flash, SRAM, persistent
storage, and OTA reservations.

## ESP32-S3 with ESP-IDF

On ESP32-S3, JaszczurHAL is built as part of a firmware project rather than installed as a separate `libJaszczurHAL.a` package. The main script, `scripts/build_esp_idf.py`, supports `build`, `artifacts`, and `flash`:

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

`tests/fixtures/esp32s3_phase3` is a compile/link test configuration used by CI and Gate 8. It enables all ESP32-S3 implementations delivered through Phase 3 and checks feature dependencies, component selection, compilation, linking, partition generation, and artifact publication. It does not test runtime behavior on hardware.

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

ESP32-S3 always enables `HAL_ENABLE_FREERTOS`. It also supports the delivered Phase 2 peripheral flags and Phase 3 network and service features. The baseline component contains system, synchronization, GPIO, ADC, simple PWM, serial, diagnostics, and timers. A directly requested or implied feature outside the descriptor's allowlist fails with `[JH-CFG-UNSUPPORTED]`.

Generated project CMake contains the resolved feature set, source list, and public/private ESP-IDF component dependencies. The component configuration uses these lists instead of maintaining a separate source graph. `scripts/build_esp_idf_phase0.py` remains a compatibility wrapper for the isolated Phase 0 test configuration.

<a id="repository-workspace-and-vs-code"></a>

## Working in the repository with VS Code

To build static libraries in VS Code, open the JaszczurHAL repository root. This workflow is separate from building a firmware project but uses the same task names:

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

`Project: Build` creates the library for the active profile and selects its compilation database for cpptools. `Project: Refresh IntelliSense` performs the same incremental build, then regenerates the gitignored `.vscode/c_cpp_properties.json`. Hardware configurations produce `libJaszczurHAL.a`; the mock produces `libhal_mock.a`.

`Project: Install library` builds the active hardware profile and installs the library, public and generated headers, and signature data to `.build/install/<target>/<board>/`. The mock does not support installation. `Project: Clean` removes only the active profile's build and installation directories and its generated IntelliSense file. Other configurations, managed tools, and dependency sources are preserved.

Version-controlled `.vscode` files in the repository root are generated from the board registry. After changing the registry or tasks, check or regenerate the generated files:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Upload, serial-monitor, and debug-probe shortcuts remain firmware-only and are
intentionally undefined when the repository root is open.

## Firmware projects and VS Code

Create a project, inspect its configuration, and build it with:

```bash
./vscode/tools/create-vscode-example.py --output /path/to/project
./vscode/entry/jh-vscode config-dump --project /path/to/project
./vscode/entry/jh-vscode build --project /path/to/project
```

Generated projects provide build, upload, monitor, debug, OTA, and test tasks configured for the selected board. See [Firmware Project Workflow](FwProjectWorkflow.md) and [VS Code integration](../../vscode/README.md).
