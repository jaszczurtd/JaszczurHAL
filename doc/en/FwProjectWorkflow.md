# Firmware Project Workflow

*Also available in [Polish](../pl/FwProjectWorkflow.md).*

This document defines the dispatcher-backed firmware project model used by
JaszczurHAL projects and checked-in examples. It covers the tracked manifest,
target and board resolution, project source discovery, generated files, cache
ownership, and build/upload integration.

Use [JaszczurHAL VS Code Entry](../../vscode/README.md) for CLI actions and device
safeguards, [Target and board profiles](boards_profiles_howto.md) for descriptor
fields and generated metadata, and [Native OTA Workflow](OTAWorkflow.md) for
the target-specific network-update paths.

## Project layout

A firmware project normally contains:

```text
my-device/
  app.c or app.cpp
  hal_project_config.h
  .vscode/
    jaszczurhal.project.json
    settings.json
    tasks.json
    launch.json
    keybindings.reference.json
    extensions.json
```

`extensions.json` recommends the extensions the project files depend on:
`ms-vscode.cpptools` for IntelliSense, `ms-vscode.cmake-tools` as the
`C_Cpp.default.configurationProvider` set in `settings.json`,
`marus25.cortex-debug` for the `launch.json` debug configuration and
`ms-vscode.vscode-serial-monitor` alongside the `jh-vscode` monitor actions.
VS Code offers to install missing entries when the folder is opened.

Generated launch files provide explicit OpenOCD interface and target scripts.
On Windows, run `jh-vscode debug-tools
--project <path> --json` and set the reported OpenOCD executable plus Arm
toolchain directory in the Cortex-Debug user settings; the extension resolves
GDB from that directory. These machine-local paths stay out of tracked project
files.

On Debian/Ubuntu-like Linux hosts, `runmefirst.sh` installs `gdb-multiarch` and
generated settings select it through `cortex-debug.gdbPath.linux`. The
STM32G474 profile uses `board/st_nucleo_g4.cfg` with connect-under-reset so the
on-board ST-Link can recover a running target before GDB attaches.

RP and STM32 projects select `toolchain: "cmake"` and point `cmake.sourceDir`
at `libraries/JaszczurHAL/cmake/jh_firmware_project`. `JH_PROJECT_DIR`
identifies the application directory. ESP32 and ESP32-S3 projects select
`toolchain: "esp-idf"`; their target registry entry supplies the production
runner and artifact-manifest path. The shared entrypoint selects the provider
without requiring a project-local CMake recipe.

Generate a working standalone project with:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output my-device --target rp2040 --board pico
```

Generated `tasks.json` contains GUI and terminal board selection, OTA upload
and discovery, and `Project: Sync board picker`. The synchronization task runs
on `folderOpen`, reads the current JaszczurHAL board registry, and updates the
tracked GUI options only when they changed. The same task creates or repairs
the generated RP2040, RP2350 ARM, and STM32G474/ST-Link debugger profiles in
`launch.json`, using the manifest ELF artifact while preserving configurations
owned by the consumer. VS Code requires a trusted workspace and may request
one-time approval for automatic tasks. The terminal `Project: Select board`
task always reads the registry at invocation time.
Every generated task uses `jaszczurhal.vscodeEntry` on Unix and the
`jaszczurhal.vscodeEntryWindows` platform override on Windows. The two settings
select the adjacent `jh-vscode` and `jh-vscode.cmd` launchers, which execute one
shared Python runtime.

Check or regenerate every tracked repository artifact, including the shared
snippets and checked-in example projects, with:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Recommended extensions can be checked without changing the VS Code profile:

```bash
python3 vscode/tools/manage_vscode_extensions.py
```

Passing `--install` requests confirmation before installing missing entries.
Automation can use `--install --yes` after obtaining consent.

## Core terms

- **Project directory**: path passed to `--project` and normally stored as
  `JH_PROJECT_DIR`.
- **Manifest**: tracked `.vscode/jaszczurhal.project.json`.
- **Local state**: gitignored `.vscode/jaszczurhal.local.json`, containing a
  developer's selected target, board, and serial port.
- **Target**: stable build ID: `rp2040`, `rp2350-arm`, `rp2350-riscv`,
  `stm32g474`, `esp32`, `esp32s3`, or `mock`.
- **Board**: stable physical profile ID such as `pico`, `picow`, `pico2`,
  `pico2w`, `pico-rm2`, `rp2040-zero`, `rp2040-plus-4mb`,
  `nucleo-g474re`, `esp32-devkitc-v4`, or `waveshare-esp32-s3-zero`.
- **Board registry**: generated tooling view of `boards/targets/*.json`,
  `boards/profiles/*.json`, and `boards/capabilities.json`.
- **`JH_TARGET` / `JH_BOARD`**: CMake cache values selected by the dispatcher
  before SDK/toolchain import.
- **`HAL_TARGET_*`**: compile-time HAL backend selector generated or inferred
  from the resolved build target.

## Target and configuration resolution

The active target/board pair is selected in this order:

1. invocation overrides such as `--target rp2040 --board picow`;
2. `.vscode/jaszczurhal.local.json`;
3. tracked manifest `target` and `board`;
4. registry default `rp2040/pico`.

The effective configuration then merges from low to high precedence:

1. target and board registry defaults;
2. base manifest;
3. active `targetProfiles.<target>` overlay;
4. resolved `JH_TARGET` and `JH_BOARD`;
5. action-specific options such as `--port`, `--host`, `--verbose`, and
   `--allow-unverified-port`.

`.vscode/settings.json` supplies editor-local paths and display preferences.
Stable identity, build cache, source layout, target profiles, artifacts, and
OTA settings belong in the manifest. Standalone projects generated by
`create-vscode-example.py` additionally copy the initial dispatcher cache into
`cmake.configureSettings` so VS Code CMake Tools can configure the shared
dispatcher without going through `jh-vscode`.

Inspect the complete resolved view before diagnosing a build or upload:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  config-dump --project "$PWD"
```

The dump includes a `featureResolution` object with `registryDigest`,
`requestedFeatures`, `resolvedFeatures`, `resolvedFeaturesDigest`, and
per-request `provenance`. This view reflects the active target profile and
variant after all manifest overlays have been applied.

## Target matrix

| Target | ISA | Default board | Firmware format | Upload |
|---|---|---|---|---|
| `rp2040` | Cortex-M0+ | `pico` | ELF/BIN/HEX/UF2/MAP | verified CDC to BOOTSEL, or direct BOOTSEL |
| `rp2350-arm` | Cortex-M33 | `pico2` | ELF/BIN/HEX/UF2/MAP | verified CDC to BOOTSEL, or direct BOOTSEL |
| `rp2350-riscv` | Hazard3 RISC-V | `pico2` | ELF/BIN/HEX/UF2/MAP | verified CDC to BOOTSEL, or direct BOOTSEL |
| `stm32g474` | Cortex-M4F | `nucleo-g474re` | ELF/BIN/HEX/MAP | OpenOCD |
| `esp32` | dual-core Xtensa LX6 | `esp32-devkitc-v4` | ELF/MAP plus bootloader, partition-table, and application BIN images | ESP-IDF flash through the verified USB-UART bridge |
| `esp32s3` | dual-core Xtensa LX7 | `waveshare-esp32-s3-zero` | ELF/MAP plus bootloader, partition-table, and application BIN images | ESP-IDF flash through verified USB Serial/JTAG |
| `mock` | host | `host-mock` | host executable/library | none |

The board registry validates target compatibility and supplies provider
platform, physical flash/PSRAM facts, GPIO domain, board components,
capabilities, programmer identity, and upload defaults. Unknown target/board
pairs fail before the compiler runs.

## Minimal manifest

```json
{
  "project": "my-device",
  "module": "tracker",
  "toolchain": "cmake",
  "target": "rp2040",
  "board": "pico",
  "buildDir": "${project}/.build",
  "cmakeBuildDir": "${buildDir}/cmake",
  "cmake": {
    "sourceDir": "${project}/../libraries/JaszczurHAL/cmake/jh_firmware_project",
    "cache": {
      "JH_PROJECT_DIR": "${project}",
      "JH_MODULE_NAME": "tracker"
    }
  },
  "identity": {
    "enabled": true,
    "usbManufacturer": "Jaszczur",
    "usbProduct": "My Device",
    "byIdHint": "My_Device"
  }
}
```

Firmware builds use Ninja unless `cmake.generator` selects another CMake
generator explicitly. The runtime always enables the compile database and
passes its current Python interpreter to CMake. Native Windows keeps the CMake
working tree below the short root recorded by `runmefirst.ps1`; the manifest's
`buildDir` remains the stable location for final artifacts and
`compile_commands_patched.json`.

Keep common values in the base manifest and express target-specific changes as
small overlays:

```json
{
  "targetProfiles": {
    "stm32g474": {
      "board": "nucleo-g474re",
      "cmake": {
        "cache": {
          "JH_EXTRA_DEFINES": "APP_STM32_BUILD=1"
        }
      }
    }
  }
}
```

The resolved registry values always pin the final `JH_TARGET` and `JH_BOARD`.

An ESP32-S3 project uses the smaller provider-specific manifest shape:

```json
{
  "project": "my-device",
  "module": "tracker",
  "toolchain": "esp-idf",
  "target": "esp32s3",
  "board": "waveshare-esp32-s3-zero",
  "buildDir": "${project}/.build/esp32s3"
}
```

The target/board registry adds the runner, artifact manifest, upload strategy,
required FreeRTOS feature, and exact `303a:1001` programmer identity. Do not
copy those facts into the project manifest.

## Adding project source files

The shared CMake project automatically discovers `*.c`, `*.cpp`, `*.h`, and
`*.hpp` directly under `JH_PROJECT_DIR`.

```text
tracker/
  app.cpp
  hal_project_config.h
  gps_filter.c
  gps_filter.h
```

Projects with source subdirectories declare the complete list:

```json
{
  "cmake": {
    "cache": {
      "JH_PROJECT_SOURCES": "app.cpp;hal_project_config.h;filters/gps.c;filters/gps.h"
    }
  }
}
```

`JH_PROJECT_SOURCES` is a semicolon-separated list relative to
`JH_PROJECT_DIR`. It replaces root discovery.

Additional shared files can be appended with `JH_EXTRA_SOURCES`:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_SOURCES": "../common/product_identity.cpp"
    }
  }
}
```

The dispatcher normalizes and de-duplicates resolved paths.

The ESP-IDF runner discovers C, C++, and assembly sources directly under the
project directory and recursively below `src/`. Direct runner calls may replace
discovery with repeatable `--source <relative-path>` arguments. All source
paths must remain inside the project.

## Feature and runtime configuration

Project-owned feature flags live in `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_WIFI
#define HAL_ENABLE_MQTT
#define HAL_ENABLE_APP_TASK1
```

`JH_EXTRA_DEFINES` is useful for target profiles, build variants, and CI:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_DEFINES": "HAL_ENABLE_FREERTOS;APP_DIAGNOSTICS=1"
    }
  }
}
```

Feature requests accept `HAL_ENABLE_X` and `HAL_ENABLE_X=1`. The dispatcher
and `jh-vscode` reject `HAL_ENABLE_X=0` and other explicit values with
`[JH-CFG-VALUE]` after resolving the active target profile and example variant.
Omit a feature symbol to disable it. Non-feature tunables such as
`APP_DIAGNOSTICS=0` keep their normal value semantics. In definition-list
inputs, every `HAL_ENABLE_*` entry must be a standalone simple token separated
with semicolons. Whitespace does not separate multiple feature definitions,
and CMake generator expressions are rejected.

The `esp32s3` descriptor supports target-required `HAL_ENABLE_FREERTOS`, the
delivered Phase 2 peripheral flags, and the Phase 3 network/service flags. The
set includes APP_TASK1, UART, I2C controller/target, SPI, PWM_FREQ, RGB_LED,
PCNT, STACK_GUARD, BLE, WiFi, TCP/UDP, BSD sockets, TLS, HTTP
client/server/files, WebSocket server, MQTT, time, OTA, and WireGuard. Simple PWM and the core
system/synchronization/GPIO/ADC/serial/timer sources belong to its baseline
component. The production runner rejects a requested feature or any dependency
that resolves outside the descriptor allowlist with `[JH-CFG-UNSUPPORTED]`.

The initial `esp32` descriptor is intentionally narrower. It supports the
required FreeRTOS runtime and `HAL_ENABLE_BLUETOOTH_GAMEPAD`, which selects
Bluedroid, BR/EDR, and ESP HID Host. Features delivered only on ESP32-S3,
including the public BLE API, are rejected during preflight.

For a Fiesta-convention `firmware_entry.h`, `FIESTA_ENABLE_CORE1=1` must be
paired with `HAL_ENABLE_APP_TASK1` in `hal_project_config.h` or another normal
feature input. This keeps the generated entry adapter, requested/resolved
feature sets, and link signature identical.

The feature registry computes one target-independent transitive closure for
all production consumers. The generated C header defines implied feature
macros, CMake uses the resolved set for source and dependency selection, and
board generation uses it for `featureHash` and the link signature. `jh-vscode`
uses the same closure for preflight and OTA eligibility while preserving the
direct requests passed to CMake. Define `HAL_CONFIG_VERBOSE` to emit the
generated report of every active registered feature during compilation.

Rules whose results depend on tunables, provider choice, board capabilities,
or the active target remain in `hal_config.h`. These include the EEPROM-type
I2C implication, the GPS transport default, backend/provider validation, board
capability checks, and target-specific constraints.

The build loads `hal_project_config.h` before target auto-detection and before
derived target/board selectors exist. Keep that file macro-only: it may define
raw `HAL_TARGET_*`, `HAL_BOARD_PROFILE_*`, `HAL_ENABLE_*`, and tuning macros,
but it must not include JaszczurHAL headers or branch on `HAL_TARGET_IS_*` /
`HAL_BOARD_IS_*`. Feature definitions used for source selection must be
unconditional `#define HAL_ENABLE_X` or `#define HAL_ENABLE_X 1`; the only
supported conditional form is a same-symbol `#ifndef HAL_ENABLE_X` guard. Do
not place feature definitions under any other `#if`/`#ifdef`, including raw or
derived target/board branches, because the early collector reads the file
textually.

Physical board selection remains in `target` and `board`. Application wiring,
USB identity, secrets, partition policy, and feature selection remain
project-owned.

## Build directories and generated files

External firmware projects own `${project}/.build`. Checked-in examples and
hardware fixtures use the JaszczurHAL root for stable artifacts:

```text
.build/examples/<example>/
.build/hardware/<fixture>/
```

On Unix, each project CMake cache is isolated by target and board below the
manifest `cmakeBuildDir`:

```text
<cmakeBuildDir>/<target>/<board>/
```

This prevents toolchains, provider platforms, board-generated headers, and
linker layouts from sharing one cache. Native Windows instead uses the short
bootstrap root:

```text
<BuildRoot>/<project-name>-<path-hash>/cmake/<target>/<board>/
```

The raw `compile_commands.json` follows that CMake tree. The runtime writes
`compile_commands_patched.json` to the stable manifest `buildDir` and refreshes
the selected target's firmware artifacts after every build, including a Ninja
no-op after switching between previously configured targets.

`jh-vscode` tracks manifest-owned cache keys in
`.jh-vscode-cache-keys.json`. A removed key is unset on the next configure.
When the requested CMake source directory changes, a stale cache located
inside the managed artifact root is recreated.

ESP-IDF projects use `buildDir` directly as the IDF build tree; it must remain
below either the project or JaszczurHAL repository `.build` root. The production
runner owns the generated project configuration and SDK configuration inside
that tree and never writes a second board registry.

Generated outputs include:

- resolved board CMake/header/JSON and link-signature translation units;
- raw `compile_commands.json` in the CMake tree and stable
  `compile_commands_patched.json` in `buildDir`;
- `.vscode/c_cpp_properties.json`;
- ELF/BIN/HEX/UF2/MAP or ELF/BIN/HEX/MAP target artifacts;
- for ESP-IDF, `jh_esp_idf_artifacts.json`, the application ELF/MAP/BIN,
  bootloader and partition-table images, `sdkconfig`, build log, generated
  board/link metadata, toolchain provenance, and the raw compile database;
- OTA container and merged recovery UF2 when OTA is enabled.

Tracked configuration remains in the manifest and `hal_project_config.h`.

## Build and upload actions

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode upload --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode monitor --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode clean --project "$PWD"
```

`Project: Upload` selects the registry upload strategy. RP targets use
identity-verified USB CDC followed by BOOTSEL/UF2 when firmware is running; a
blank board uses `Project: Upload (UF2 / BOOTSEL)`. STM32G474 delegates to the
OpenOCD upload target. ESP32-S3 performs a validated production build,
checks every path in the multi-image manifest, and passes the verified serial
port to the ESP-IDF flash action. Its board profile supplies USB VID/PID
`303a:1001`; zero, stale, mismatching, or multiple matching devices fail closed.
`--allow-unverified-port` is an explicit escape hatch and must be paired with
`--port`.

Upload releases the project's persistent serial monitor and lets it reconnect
after enumeration. Ambiguous BOOTSEL volumes or serial identities stop the
action.

For ESP32-S3, `Project: Serial Monitor` follows the single board matching the
registry programmer identity when no explicit port is pinned. `Project:
Refresh IntelliSense` consumes the Xtensa compile commands emitted by ESP-IDF
without substituting an Arm IntelliSense mode. `build-debug` and managed
Cortex-Debug profiles are not provided for ESP32-S3.

## OTA manifest configuration

For RP CMake projects, the manifest publishes the generated container and its
build metadata alongside the shared OTA endpoint settings:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_DEFINES": "HAL_ENABLE_OTA",
      "JH_OTA_GENERATION": 7,
      "JH_OTA_VERSION": "1.4.0"
    }
  },
  "artifacts": {
    "ota": "${buildDir}/firmware.ota"
  },
  "ota": {
    "hostname": "tracker-office",
    "port": 8266,
    "listenPort": 8266,
    "passwordEnv": "TRACKER_OTA_PASSWORD"
  }
}
```

ESP-IDF projects omit the RP-specific `cmake` and `artifacts.ota` entries. Their
production build manifest identifies the raw application BIN; the `ota` object
above remains the shared host endpoint and authentication configuration.

`ota.broadcast` selects the UDP discovery destination. `ota.host` pins a
device address. `ota.listenPort` selects the host TCP callback listener; it
defaults to `8266` so it matches the persistent LAN-scoped firewall rule
prepared by `runmefirst.sh`. An explicit zero requests an ephemeral port.
`ota.passwordEnv` keeps the host secret outside the tracked manifest.

The device hostname, UDP port, and password must match firmware configuration.
See [Native OTA Workflow](OTAWorkflow.md) for target-specific artifacts,
provisioning, tasks, authentication, host firewall rules, trial confirmation,
rollback, and recovery. RP uploads sign the generated JaszczurHAL container;
ESP-IDF uploads validate the production artifact manifest and transfer its raw
application BIN without converting it into the RP container format.

## Examples and variants

Example manifests may declare `example.targets` and `example.variants`.
Variants can override module name, sources, feature definitions, supported
targets, and CMake cache entries.

```bash
scripts/examples_dispatcher.py list
scripts/examples_dispatcher.py build --target rp2040 --example 01_core_runtime
```

The generated example manifests are the build inputs used by the quality gate.
See [JaszczurHAL Examples](../../examples/README.md).
