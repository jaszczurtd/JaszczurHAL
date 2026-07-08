# JaszczurHAL VS Code Entry

This directory contains the shared VS Code firmware workflow for projects that
use JaszczurHAL: build, debug build, upload, UF2 upload, serial monitor,
IntelliSense refresh, board/port helpers, and USB identity cleanup.

The stable public surface is `entry/`. Project `.vscode/tasks.json` files should
call `entry/jh-vscode` and keep project-specific behavior in configuration.
Files under `linux/runtime/` and `windows/runtime/` are implementation details.

## CLI Contract

```text
jh-vscode <action> [options]
```

Supported actions in the initial contract:

```text
build
build-debug
upload
upload-uf2
monitor
monitor-probe
monitor-any
refresh-intellisense
clean
select-board
list-ports
change-port
clear-identity
config-dump
```

Compatibility note: `debug` is accepted as an alias for `build-debug` only for
early migration work. New tasks should use `build-debug`.

Common options:

```text
--project <path>       Firmware module directory.
--fqbn <fqbn>          Override configured board FQBN for this invocation.
--port <port>          Override configured upload/monitor port.
--baud <baud>          Serial monitor baud rate, default 115200.
--lock-policy <mode>   Serial monitor lock policy: wait, replace-own, replace-any.
--allow-unverified-port
                       Expert-only serial upload to an explicitly selected port
                       that does not match configured USB identity.
--verbose              Enable verbose output.
--json                 Emit machine-readable output where supported.
--help                 Show help.
--version              Show tool version.
```

For module actions, `--project` means the firmware module directory. It does not
mean the repository root. Examples:

```text
jh-vscode build --project /home/user/projects/router-reset/reseter
jh-vscode build --project /home/user/projects/Fiesta/src/Clocks
jh-vscode clear-identity --project /home/user/projects/Fiesta/src/ECU
```

`build`, `build-debug`, `upload`, `upload-uf2`, `refresh-intellisense`, `clean`,
and `clear-identity` require `--project`. Actions that touch a device must fail
before accessing serial ports, BOOTSEL disks, or build artifacts when the target
module is ambiguous.

Arduino CLI mode requires a real project sketch file named `<module>.ino`.
Projects that do not keep a checked-in `.ino` should use `toolchain: "cmake"`
and generate the small Arduino compatibility sketch from their own
`CMakeLists.txt`, following the existing Ford/TimerNTP model. In that mode
`jh-vscode` only configures and runs CMake targets such as:

```text
firmware
firmware_debug
firmware_upload
firmware_compile_db
```

By default, `jh-vscode` configures CMake with `-S <project>`. Projects that
keep a shared firmware CMake entry outside the module directory can set
`cmake.sourceDir` in `.vscode/jaszczurhal.project.json`, for example the
JaszczurHAL multi-target dispatcher
`${project}/../../libraries/JaszczurHAL/cmake/jh_firmware_project`.

The generated sketch belongs under the CMake build directory, not under the
shared `jh-vscode` runtime. This keeps project-specific build layout visible in
the project and avoids hidden Python-side source staging.

## New Project Generator

Use `tools/create-vscode-example.py` to create a standalone, CMake-first VS Code
firmware project next to your other firmware repositories:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output /home/user/projects/jaszczurhal-vscode-example
```

The generated project contains a small blink application, project-local
`.vscode/` files, a `hal_project_config.h` with `HAL_PROVIDE_APP_ENTRY`, and a
`CMakeLists.txt` that generates the Arduino compatibility sketch under
`.build/cmake/sketch/<module>`. It uses `jh-vscode` for the same actions as
migrated projects:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode build-debug --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project "$PWD"
```

The full generated project should live outside `libraries/JaszczurHAL/vscode/`.
The `vscode/examples/` directory remains a place for lightweight configuration
snippets, not a checked-in firmware project.

## VS Code Keyboard Shortcuts

Project `.vscode/keybindings.reference.json` files are references only. VS Code
does not load them automatically and there is no repository-local keyboard
shortcut activation step. Shortcuts work only when the matching entries are
present in the real VS Code user file:

```text
~/.config/Code/User/keybindings.json
```

After migrating a project, make sure that global shortcuts call the canonical
task labels:

```text
Ctrl+Shift+1  Project: Build
Ctrl+Shift+2  Project: Upload
Ctrl+Shift+3  Project: Serial Monitor
Ctrl+Shift+4  Project: Upload (UF2 / BOOTSEL)
Ctrl+Shift+5  Project: Debug Probe Monitor
Ctrl+Shift+6  Project: Refresh IntelliSense
Ctrl+Shift+7  Project: Clean
```

Old bindings such as `Project: Monitor (persistent)` or
`Project: Monitor (Debug Probe)` will open the "Show all tasks" prompt once
the compatibility aliases are removed from project `tasks.json`.

If `Ctrl+Shift+3` does not start the monitor, inspect the real user
`keybindings.json` first. The correct binding is:

```json
{
    "key": "ctrl+shift+3",
    "command": "workbench.action.tasks.runTask",
    "args": "Project: Serial Monitor"
}
```

After editing the user keybindings file, reload the VS Code window if the old
binding is still cached.

After a successful `build`, `upload`, or `upload-uf2`, `jh-vscode` prints a
compact ELF memory map overview when a `firmware.elf` artifact is available.
The overview is derived from `arm-none-eabi-objdump -h`, groups allocated
sections by FLASH/XIP, SRAM, PSRAM, and OTHER, and shows VMA/LMA placement,
section sizes, and short notes. Set `JH_VSCODE_MEMORY_OVERVIEW=0` to suppress
this extra console output.

The serial monitor defaults to `--lock-policy wait`. `replace-own` may stop only
another JaszczurHAL monitor for the same project. `replace-any` is an explicit
emergency option and should not be used in default VS Code tasks.

For identity-enabled projects, `upload` verifies the selected serial port before
running `arduino-cli --upload`. A port is considered verified when its
`/dev/serial/by-id` name matches the configured identity. First flashing a clean
board must be an explicit operation with `--allow-unverified-port --port <port>`.
Default VS Code tasks must not pass this flag.

## Configuration Precedence

Configuration is loaded relative to `--project` in this order:

1. Explicit CLI flags.
2. `.vscode/jaszczurhal.project.json`.
3. `.vscode/settings.json` keys under `jaszczurhal.*`.
4. `.vscode/settings.json` keys under `arduino.*`.
5. Legacy `.vscode/arduino.json`, only for projects that still need it.
6. Development fallback from the directory name, with a warning.

For the same semantic value, `jaszczurhal.*` wins over `arduino.*`.

Stable project data should move to `.vscode/jaszczurhal.project.json` during
migration. Developer-local preferences, such as a temporary serial port or a
local `arduino-cli` path, may remain in `.vscode/settings.json`.

## Minimal Project Manifest

The manifest intentionally starts small. The current schema lives in
`schema/jh_vscode_project.schema.json`.

```json
{
  "project": "router-reset",
  "module": "reseter",
  "toolchain": "arduino-cli",
  "fqbn": "rp2040:rp2040:rpipicow",
  "buildDir": "${project}/.build",
  "identity": {
    "enabled": true,
    "usbManufacturer": "Jaszczur",
    "usbProduct": "Router Reset",
    "byIdHint": "Router_Reset"
  },
  "artifacts": {
    "elf": "${buildDir}/${module}.ino.elf",
    "uf2": "${buildDir}/${module}.ino.uf2"
  },
  "upload": {
    "strategy": "serial"
  }
}
```

## USB Identity

USB identity means firmware descriptors visible to the host as
manufacturer/product, for example in `lsusb`, `dmesg`, VS Code Serial Monitor,
and `/dev/serial/by-id`.

For Arduino-Pico builds, identity is injected through build properties:

```text
--build-property build.usb_manufacturer="Jaszczur"
--build-property build.usb_product="Router Reset"
```

If identity is disabled or incomplete, the build must not pass custom USB
manufacturer/product properties.

`clear-identity` is a separate action. It requires `--project`, builds the
neutral firmware from `neutral_fw/`, does not pass custom USB identity build
properties, and flashes only a verified serial target.

`upload-uf2` intentionally keeps manual BOOTSEL simple: it builds the project,
requires exactly one BOOTSEL drive or `RPI-RP2` block device, mounts it with
`udisksctl` when needed, copies the UF2, and refuses to guess when multiple
BOOTSEL drives are visible.

When `upload` finds this project's persistent serial monitor on the upload
port, it asks the monitor to release the port, keeps a short-lived project
marker while the upload is in progress, and lets the monitor reconnect
automatically after the board returns.

## Exit Codes

```text
0   Success.
1   Generic failure.
2   Invalid CLI usage.
3   Missing or invalid project configuration.
4   Ambiguous or unsafe device selection.
5   Build failed.
6   Upload failed.
7   Monitor failed.
8   Unsupported action or platform path.
```

## Generated Files

`refresh-intellisense` uses compile database output as the source of truth:

```text
<buildDir>/compile_commands_patched.json
```

`.vscode/c_cpp_properties.json` is treated as a generated adapter for VS Code
cpptools. It should not be the long-term hand-edited contract of a project.
