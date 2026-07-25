# JaszczurHAL VS Code Entry

This directory contains the shared VS Code firmware workflow for projects that
use JaszczurHAL: build, debug build, upload, UF2 upload, serial monitor,
IntelliSense refresh, board/port helpers, and USB identity cleanup.

The stable public surface is `entry/`. Project `.vscode/tasks.json` files should
call `entry/jh-vscode` and keep project-specific behavior in configuration.
Files under `linux/runtime/` and `windows/runtime/` are implementation details.
For the full firmware project model, see
[`doc/FwProjectWorkflow.md`](../doc/FwProjectWorkflow.md). For the complete
native RP OTA contract, including firewall and recovery, see
[`doc/OTAWorkflow.md`](../doc/OTAWorkflow.md).

## CLI Contract

```text
jh-vscode <action> [options]
```

Implemented actions:

```text
build
build-debug
upload
upload-uf2
upload-ota
ota-discover
monitor
monitor-probe
monitor-any
refresh-intellisense
clean
select-board
sync-board-picker
list-ports
change-port
clear-identity
config-dump
```

Compatibility note: `debug` is accepted as an alias for `build-debug` only for
early migration work. New tasks should use `build-debug`.

`change-port` selects a serial port interactively or through `--port` and saves
it as user-local `uploadPort` in `.vscode/jaszczurhal.local.json`.

`sync-board-picker` refreshes the `boardSelection` input and its automatic
folder-open task from the current `boards/` registry. Generated projects run it
when a trusted workspace opens. VS Code may require one-time approval through
`Tasks: Manage Automatic Tasks`; `Project: Select board` remains the dynamic
terminal fallback.

Common options:

```text
--project <path>       Firmware module directory.
--target <id>          Override active target family for this invocation.
--board <id>           Override active board within the target.
--variant <id>         Select an example variant declared by the manifest.
--selection <t:b>      Persist target/board selection; GUI labels are accepted.
--interactive          Prompt for target/board selection in the terminal.
--port <port>          Override configured upload/monitor port.
--host <address>       Bypass OTA discovery and use this device address.
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

`build`, `build-debug`, `upload`, `upload-uf2`, `upload-ota`, `ota-discover`,
`refresh-intellisense`, `clean`, `change-port`, and `clear-identity` require
`--project`. Actions that touch a device must fail before accessing serial
ports, BOOTSEL disks, or build artifacts when the target module is ambiguous.

Firmware projects use `toolchain: "cmake"` and the shared JaszczurHAL
dispatcher. `jh-vscode` resolves the active target/board, configures CMake, and
runs the maintained firmware targets:

```text
firmware
firmware_debug
firmware_upload
firmware_compile_db
```

By default, `jh-vscode` configures CMake with `-S <project>`. Generated and
migrated projects set `cmake.sourceDir` in `.vscode/jaszczurhal.project.json`
to the shared multi-target dispatcher, for example
`${project}/../../libraries/JaszczurHAL/cmake/jh_firmware_project`, and pass the
module directory as `JH_PROJECT_DIR`.

The `rp2040`, `rp2350-arm`, and `rp2350-riscv` targets build directly with the
official Pico SDK. RP firmware exposes HAL-owned USB CDC. With a selected
serial port, normal `upload` releases the project monitor,
performs a 1200-bps DTR touch, waits for the single BOOTSEL drive and copies the
UF2. STM32G474 uses the registry's native target recipe, supports bare-metal
and FreeRTOS firmware variants, and uploads through OpenOCD.

## Adding Project Source Files

Source discovery and the complete `JH_PROJECT_SOURCES` contract are defined in
[Adding Project Source Files](../doc/FwProjectWorkflow.md#adding-project-source-files).
That document is the only source for project layout and manifest examples.

## New Project Generator

Use `tools/create-vscode-example.py` to create a standalone, CMake-first VS Code
firmware project next to your other firmware repositories:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output /home/user/projects/jaszczurhal-vscode-example
```

The generated project contains a small blink application, project-local
`.vscode/` files, and a `hal_project_config.h` with `HAL_PROVIDE_APP_ENTRY`.
It does **not** carry a project-local firmware `CMakeLists.txt`; the manifest
points `cmake.sourceDir` at the shared JaszczurHAL dispatcher and stores the
initial `target`/`board` selection. Use `--target` and `--board` to choose a
non-default initial board:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output /home/user/projects/jaszczurhal-stm32-example \
  --target stm32g474 \
  --board nucleo-g474re
```

The generated project uses `jh-vscode` for the same actions as migrated
projects:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode build-debug --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode select-board --project "$PWD" --interactive
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

After migrating a project, make sure that global shortcuts call the maintained
task labels:

```text
Ctrl+Shift+1  Project: Build
Ctrl+Shift+2  Project: Upload
Ctrl+Shift+3  Project: Serial Monitor
Ctrl+Shift+4  Project: Upload (UF2 / BOOTSEL)
Ctrl+Shift+5  Project: Debug Probe Monitor
Ctrl+Shift+6  Project: Refresh IntelliSense
Ctrl+Shift+7  Project: Clean
Ctrl+Shift+8  Project: Upload (OTA)
Ctrl+Shift+9  Project: Config Dump
Ctrl+Shift+Alt+1  Project: Select board (GUI)
Ctrl+Shift+Alt+2  Project: Select board
Ctrl+Shift+Alt+3  Project: Discover OTA devices
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

For identity-enabled Pico projects, an implicitly configured monitor port may
follow the single CDC device matching the project's verified USB identity when
the saved path disappears or the kernel assigns a different `ttyACM` number.
This also covers replacing a board with another unit running the same firmware.
The monitor waits instead of guessing when zero or multiple devices match. An
explicit `--port` remains pinned to the requested path.

For identity-enabled serial uploads, `upload` verifies the selected serial port
before running the upload backend. A port is considered verified when its
`/dev/serial/by-id` name matches the configured identity. First flashing a clean
serial-only board must be an explicit operation with
`--allow-unverified-port --port <port>`. Default VS Code tasks must not pass
this flag.

## Configuration Precedence

Target/board selection, manifest overlays, settings fallback, and local state
precedence are defined only in
[Target And Configuration Resolution](../doc/FwProjectWorkflow.md#target-and-configuration-resolution).

## Minimal Project Manifest

The maintained manifest example and overlay rules live in
[Minimal Manifest](../doc/FwProjectWorkflow.md#minimal-manifest).
Machine validation uses `schema/jh_vscode_project.schema.json`.

## USB Identity

USB identity means firmware descriptors visible to the host as
manufacturer/product, for example in `lsusb`, `dmesg`, VS Code Serial Monitor,
and `/dev/serial/by-id`.

For native RP builds, identity is injected through CMake cache entries:

```text
-DJH_USB_MANUFACTURER="Jaszczur"
-DJH_USB_PRODUCT="Router Reset"
```

If identity is disabled or incomplete, the build uses JaszczurHAL's default USB
descriptors.

`clear-identity` is a separate action. It requires `--project`, builds the
neutral firmware from `neutral_fw/rp_native/` through the selected native Pico
SDK target and board, omits custom USB identity cache entries, and flashes a
verified serial target or one unambiguous BOOTSEL device.

`upload-uf2` is RP-family/UF2-only and intentionally keeps manual BOOTSEL simple:
it builds the project, requires exactly one BOOTSEL drive or `RPI-RP2` block
device, mounts it with `udisksctl` when needed, copies the UF2, and refuses to
guess when multiple BOOTSEL drives are visible. Use target-neutral `upload` for
STM32/OpenOCD.

For native RP targets, target-neutral `upload` uses the configured/verified CDC
port for the 1200-bps reset and then follows the same single-drive UF2 safety
rules. When the configured CDC path is stale because the selected board is
already in BOOTSEL, `upload` falls back to the single visible BOOTSEL device
instead of rejecting the missing serial identity. When a replacement board is
already running compatible firmware, a stale saved path may instead be replaced
by the single CDC port matching the project's verified USB identity. Zero or
multiple identity matches remain an error. An explicit `--port` never uses
either fallback. The first flash still requires manual BOOTSEL because blank
firmware has no CDC reset endpoint.

When `upload` finds this project's persistent serial monitor on the upload
port, it asks the monitor to release the port, keeps a short-lived project
marker while the upload is in progress, and lets the monitor reconnect
automatically after the board returns.

## Native RP OTA

`upload-ota` is the network update path for native `rp2040` and `rp2350-arm`
WiFi board builds with `HAL_ENABLE_OTA`. It builds the project, locates its
`.ota` artifact, signs the image header with the configured password, transfers
it into the device's staging slot and waits for device acceptance. The password
is not embedded in the unsigned build artifact. The `rp2350-riscv` target can
build the OTA container and boot applier, but the current RISC-V board profile
has no CYW43 transport, so it has no operational network upload path.

`ota-discover` broadcasts a JaszczurHAL discovery request and lists hostname,
address, target, port, slot size, image generation and boot mode. Upload
automatically selects one device matching the active target and configured
hostname. If several match, the generated task uses `--interactive`; automation
should set manifest `ota.host` or pass `--host <address>` explicitly.

The `ota` and `artifacts.ota` manifest fields are defined in
[OTA Manifest Configuration](../doc/FwProjectWorkflow.md#ota-manifest-configuration).
Keep secrets outside the tracked manifest with `ota.passwordEnv`. An inline
`ota.password` is intended only for development examples. Empty passwords are
rejected unless `ota.allowEmptyPassword` is explicitly true.
The host callback defaults to TCP/8266. `runmefirst.sh` offers a persistent,
LAN-scoped firewall rule for that port after explicit confirmation.

Firmware integration, first installation, host firewall rules, keyboard
shortcuts, trial confirmation, rollback, recovery, and troubleshooting are
owned by [Native RP OTA Workflow](../doc/OTAWorkflow.md).

The first installation still uses the merged UF2 through BOOTSEL. That image
contains the boot applier and application. Later OTA boots are trials:
application code must call `hal_ota_confirm_boot_ex()` only after its startup
self-tests and required services succeed. Otherwise the boot applier rolls
back after the configured attempt limit. Manual BOOTSEL remains the recovery
path.

The reference application is
[`examples/57_ota`](../examples/57_ota/README.md). Its built-in WiFi transport
supports Pico W/RP2040 and Pico 2 W/RP2350 ARM. The RP2350 RISC-V image and boot
applier are buildable, but the repository does not yet provide a supported
CYW43 network backend for that ISA.

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

## Generated Files And Build Cache

Artifact roots, compile databases, generated adapters, target/board cache
isolation, stale-cache reset, and cache-key ownership are defined only in
[Build Directories And Generated Files](../doc/FwProjectWorkflow.md#build-directories-and-generated-files).
