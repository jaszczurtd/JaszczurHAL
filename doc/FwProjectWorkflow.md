# Firmware Project Workflow

This document defines the dispatcher-backed firmware project model used by
JaszczurHAL projects and checked-in examples. It covers the tracked manifest,
target and board resolution, project source discovery, generated files, cache
ownership, and build/upload integration.

Use [JaszczurHAL VS Code Entry](../vscode/README.md) for CLI actions and device
safeguards, [Target and board profiles](boards_profiles_howto.md) for descriptor
fields and generated contracts, and [Native RP OTA Workflow](OTAWorkflow.md)
for the complete network-update path.

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

The tracked manifest selects `toolchain: "cmake"` and points
`cmake.sourceDir` at `libraries/JaszczurHAL/cmake/jh_firmware_project`.
`JH_PROJECT_DIR` identifies the application directory. The shared dispatcher
selects the target recipe and compiles the project sources together with
JaszczurHAL.

Generate a working standalone project with:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output my-device --target rp2040 --board pico
```

Generated `tasks.json` contains GUI and terminal board selection, OTA upload
and discovery, and `Project: Sync board picker`. The synchronization task runs
on `folderOpen`, reads the current JaszczurHAL board registry, and updates the
tracked GUI options only when they changed. VS Code requires a trusted
workspace and may request one-time approval for automatic tasks. The terminal
`Project: Select board` task always reads the registry at invocation time.
Every generated task uses `jaszczurhal.vscodeEntry` on Unix and the
`jaszczurhal.vscodeEntryWindows` platform override on Windows. The two settings
select the adjacent `jh-vscode` and `jh-vscode.cmd` launchers, which execute one
shared Python runtime.

Check all tracked VS Code files, regenerate the shared snippets, or regenerate
every checked-in example project with:

```bash
scripts/examples_dispatcher.py check-template
scripts/examples_dispatcher.py generate-template
scripts/examples_dispatcher.py generate
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
  `stm32g474`, or `mock`.
- **Board**: stable physical profile ID such as `pico`, `picow`, `pico2`,
  `pico2w`, `pico-rm2`, `rp2040-zero`, `rp2040-plus-4mb`, or
  `nucleo-g474re`.
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
OTA settings belong in the manifest.

Inspect the complete resolved view before diagnosing a build or upload:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  config-dump --project "$PWD"
```

## Target matrix

| Target | ISA | Default board | Firmware format | Upload |
|---|---|---|---|---|
| `rp2040` | Cortex-M0+ | `pico` | ELF/BIN/HEX/UF2/MAP | verified CDC to BOOTSEL, or direct BOOTSEL |
| `rp2350-arm` | Cortex-M33 | `pico2` | ELF/BIN/HEX/UF2/MAP | verified CDC to BOOTSEL, or direct BOOTSEL |
| `rp2350-riscv` | Hazard3 RISC-V | `pico2` | ELF/BIN/HEX/UF2/MAP | verified CDC to BOOTSEL, or direct BOOTSEL |
| `stm32g474` | Cortex-M4F | `nucleo-g474re` | ELF/BIN/HEX/MAP | OpenOCD |
| `mock` | host | `host-mock` | host executable/library | none |

The board registry validates target compatibility and supplies provider
platform, physical flash size, GPIO domain, board components, capabilities,
and upload defaults. Unknown target/board pairs fail before the compiler runs.

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

Generated outputs include:

- resolved board CMake/header/JSON and link-contract translation units;
- raw `compile_commands.json` in the CMake tree and stable
  `compile_commands_patched.json` in `buildDir`;
- `.vscode/c_cpp_properties.json`;
- ELF/BIN/HEX/UF2/MAP or ELF/BIN/HEX/MAP target artifacts;
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
OpenOCD upload target.

Upload releases the project's persistent serial monitor and lets it reconnect
after enumeration. Ambiguous BOOTSEL volumes or serial identities stop the
action.

## OTA manifest configuration

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

`ota.broadcast` selects the UDP discovery destination. `ota.host` pins a
device address. `ota.listenPort` selects the host TCP callback listener; it
defaults to `8266` so it matches the persistent LAN-scoped firewall rule
prepared by `runmefirst.sh`. An explicit zero requests an ephemeral port.
`ota.passwordEnv` keeps the host secret outside the tracked manifest.

The device hostname, UDP port, and password must match firmware configuration.
See [Native RP OTA Workflow](OTAWorkflow.md) for provisioning, tasks,
authentication, host firewall rules, trial confirmation, rollback, and
recovery.

## Examples and variants

Example manifests may declare `example.targets` and `example.variants`.
Variants can override module name, sources, feature definitions, supported
targets, and CMake cache entries.

```bash
scripts/examples_dispatcher.py list
scripts/examples_dispatcher.py build --target rp2040 --example 01_blink
```

The generated example manifests are the build inputs used by the quality gate.
See [JaszczurHAL Examples](../examples/README.md).
