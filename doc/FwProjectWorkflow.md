# Firmware Project Workflow

This document describes the dispatcher-backed firmware project model used by
JaszczurHAL VS Code projects and checked-in examples. It is the practical
workflow companion to the API reference: use it when creating or migrating a
firmware project, adding source files, changing target/board selection, or
debugging build-directory/cache behavior.

## Scope

A dispatcher-backed firmware project:

- has a project-local `.vscode/jaszczurhal.project.json`,
- calls the stable `vscode/entry/jh-vscode` entrypoint from VS Code tasks or
  shell commands,
- sets `toolchain: "cmake"`,
- points `cmake.sourceDir` at
  `libraries/JaszczurHAL/cmake/jh_firmware_project`,
- passes the firmware directory as `JH_PROJECT_DIR`,
- normally has no project-local firmware `CMakeLists.txt` and no hand-written
  `.ino` file.

Legacy Arduino projects can still use the older Arduino CLI path, but new and
migrated multi-target projects should use the dispatcher.

## Core Terms

- **Project directory** is the path passed as `--project`. It is also the usual
  value of `JH_PROJECT_DIR`.
- **Manifest** is `.vscode/jaszczurhal.project.json`. It is tracked and should
  describe stable project behavior.
- **Local selection** is `.vscode/jaszczurhal.local.json`. It stores the
  developer's current target/board selection and should be gitignored.
- **Target family** is a backend family id such as `rp2040` or `stm32g474`.
- **Board** is a selectable board/profile inside the target family, such as
  `pico`, `picow`, or `nucleo-g474re`.
- **Target registry** is `vscode/targets/<target>.json`. It supplies family and
  board defaults, including CMake cache defaults and upload strategy.
- **`JH_TARGET`** is the CMake dispatcher cache value selected from the active
  target family.
- **`HAL_TARGET_*`** macros are HAL compile-time backend selectors. They are
  normally auto-detected from the selected toolchain/backend; projects should
  only define them manually for special builds.

## Target And Configuration Resolution

Target selection and manifest merging are separate concepts.

The active target/board pair is selected in this order:

1. Explicit CLI override, for example `--target stm32g474 --board nucleo-g474re`.
2. Gitignored `.vscode/jaszczurhal.local.json`, written by `select-board`.
3. Tracked manifest `target` / `board`.
4. Default `rp2040` and the registry default board for that target.

After the active pair is known, the effective dispatcher configuration is built
from low to high precedence:

1. Target-registry family defaults and selected-board cache.
2. Base `.vscode/jaszczurhal.project.json`.
3. Active `targetProfiles.<target>` overlay from the manifest.
4. Final active target/board pinning, including `cmake.cache.JH_TARGET`.
5. Per-invocation CLI flags such as `--port`, `--fqbn`, `--verbose`, or
   `--allow-unverified-port`.

`.vscode/settings.json` is a compatibility/fill-in source, not a target-profile
layer. It may provide local values such as tool paths, upload port, sketchbook,
verbosity, or legacy Arduino settings. Stable project identity, source layout,
target profiles, and dispatcher cache values belong in the manifest.

For keys exposed through both `jaszczurhal.*` and `arduino.*` settings,
`jaszczurhal.*` wins. Legacy `.vscode/arduino.json` is only a fallback for
projects that still need it.

Use this command to inspect what the dispatcher will actually use:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode config-dump --project "$PWD"
```

## Minimal Manifest Shape

```json
{
  "project": "my-device",
  "module": "tracker",
  "toolchain": "cmake",
  "target": "rp2040",
  "board": "pico",
  "buildDir": "${project}/.build",
  "cmakeBuildDir": "${project}/.build/cmake",
  "cmake": {
    "sourceDir": "${project}/../libraries/JaszczurHAL/cmake/jh_firmware_project",
    "cache": {
      "JH_PROJECT_DIR": "${project}",
      "JH_MODULE_NAME": "tracker",
      "ARDUINO_LIBRARIES": "${project}/../libraries"
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

For target-specific differences, keep the common values in the base manifest
and add only the changed values under `targetProfiles`:

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

The selected registry target still pins `JH_TARGET`, so a stale hard-coded
`JH_TARGET` entry in a manifest cannot silently keep building the wrong backend.

## Adding Project Source Files

For a flat project layout, place `*.c`, `*.cpp`, `*.h`, and `*.hpp` files
directly in `JH_PROJECT_DIR`. The dispatcher discovers those files
automatically on the next CMake configure/build.

```text
tracker/
  app.cpp
  hal_project_config.h
  gps_filter.c
  gps_filter.h
```

For source files in subdirectories, define the complete explicit source list in
`.vscode/jaszczurhal.project.json`:

```json
{
  "cmake": {
    "cache": {
      "JH_PROJECT_SOURCES": "app.cpp;hal_project_config.h;paradygmat/filter.c;paradygmat/filter.h;paradygmat/state.cpp;paradygmat/state.h"
    }
  }
}
```

`JH_PROJECT_SOURCES` is a semicolon-separated CMake list stored as a JSON
string. Relative paths resolve from `JH_PROJECT_DIR`. When this option is
present, it replaces automatic source discovery, so list the whole firmware
project, not just the new subdirectory.

Prefer project-root-relative includes from code outside the subdirectory:

```c
#include "paradygmat/filter.h"
```

Run `Project: Build` after changing the list. If a stale generated RP2040
sketch still appears to use the old source set, run `Project: Clean` once and
build again.

## Build Directories And Generated Files

`buildDir` is the project artifact root, usually `${project}/.build`.
`cmakeBuildDir` is the base CMake cache directory, usually
`${project}/.build/cmake`.

When an active target is resolved, `jh-vscode` isolates CMake caches by target
and board:

```text
.build/cmake/<target>/<board>/
```

For example:

```text
.build/cmake/rp2040/pico/
.build/cmake/stm32g474/nucleo-g474re/
```

This prevents an RP2040 Arduino-Pico CMake cache and an STM32 cross-toolchain
cache from sharing one directory.

If the cached CMake source directory no longer matches the requested
`cmake.sourceDir`, `jh-vscode` resets the stale cache when it is safely inside
the project directory. This handles common migration errors such as an old
project-local CMake source being replaced by the shared dispatcher source.

Generated files include:

- `.build/compile_commands_patched.json` from `Project: Refresh IntelliSense`,
- `.vscode/c_cpp_properties.json` as a generated VS Code cpptools adapter,
- target-specific generated RP2040 sketch files under the CMake build tree.

These files are generated artifacts. Keep stable project behavior in the
manifest, not in generated IntelliSense output.

## Upload And Debug Build

`Project: Upload` is target-neutral. It uses the active target's upload backend:

- RP2040/RP2350 commonly uses the registry `uf2` strategy, with serial identity
  checks where serial upload is configured.
- STM32G474 uses the OpenOCD-backed CMake `firmware_upload` target.

`Project: Upload (UF2 / BOOTSEL)` and `upload-uf2` are RP2040/UF2-only. Use
them for manual BOOTSEL flashing, especially the first flash of a blank RP2040
board. They are not the STM32 upload path.

`Project: Build (Debug)` / `build-debug` builds the dispatcher
`firmware_debug` target. It does not, by itself, guarantee a complete debugger
launch configuration for every backend. The checked-in generic Cortex-Debug
launch template is currently RP2040-oriented; STM32 debug sessions should be
documented or configured per project until a shared STM32 launch profile exists.

## Board Selection

`Project: Select board (GUI)` and `Project: Select board` both write the same
local target/board state. The GUI task uses a VS Code pick list; the terminal
task should use `--interactive` so the CLI can prompt in the terminal.

The selector intentionally does not rewrite the tracked manifest. Commit the
manifest default when a project should start on a different board for everyone;
use local selection for personal hardware on a workstation.

## Examples And Variants

Each numbered directory in `examples/` is also a dispatcher-backed firmware
project. The examples quality gate builds those manifests through
`scripts/examples_dispatcher.py`, which in turn calls `vscode/entry/jh-vscode`.

Example manifests may declare `example.targets` and `example.variants`.
Variants can override module name, source list, extra defines, target support,
and CMake cache entries. The examples runner builds the base example plus every
variant supported by the selected target.

## Reserved Or Experimental Fields

The manifest schema is intentionally a little ahead of the fully stable public
surface. Treat these as reserved unless a project-specific migration explicitly
uses them:

- `hooks.*` fields are parsed as configuration data but are not a guaranteed
  executed hook system.
- `upload.strategy` values `custom` and `esptool` are schema-level reservation
  points; the stable implemented dispatcher paths are currently RP2040 UF2 /
  serial-style upload and STM32 OpenOCD.
- ESP32 is listed as a skeleton target until its HAL backend and build recipe
  are completed.
- `change-port` is a reserved CLI action and should not be used by default
  project tasks.

## Quality Gates

Use the same dispatcher path locally and in CI:

```bash
# Build all RP2040 examples.
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"

# Build all STM32G474 examples.
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"

# Run the full repository gate.
./runalltests.sh
```

The repository pre-commit hook normalizes staged text files at commit time. It
does not format unstaged files and it is not an editor-on-save formatter. To
make punctuation/whitespace normalization visible before committing, stage the
files first and run the hook through Git or execute `.githooks/pre-commit`.
