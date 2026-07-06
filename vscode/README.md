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
properties, and flashes only a verified target. A manually attached BOOTSEL disk
is not considered verified for multi-module projects.

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

